#pragma once

// Shared WAMR engine lifetime and module loading for std/wasm.

#include <mutex>
#include <string>
#include <vector>

#include "native_wasm_values.hpp"

extern "C" {
wasm_module_inst_t doof_wasm_instance_runtime(const wasm_instance_t* instance);
bool doof_wasm_register_natives(void);
}

namespace doof_wasm {

// WAMR keeps one process-wide engine. Every live store holds a reference, and
// the engine is deleted when the last one is released. Besides freeing the
// runtime when std/wasm is idle, this bounds the per-module bookkeeping the
// C API keeps until engine deletion. The same lock serializes engine,
// store, module, and instance creation and destruction, which touch
// WAMR's global state.
inline std::mutex& engineLock() {
    static std::mutex lock;
    return lock;
}

// Maximum simultaneous wasi-threads threads per instance, including the caller.
constexpr uint32_t maxThreadsPerInstance = 64;

// Owns an engine reference plus a store. Call only while holding engineLock().
class Store {
public:
    Store() {
        const bool freshRuntime = liveStores() == 0;
        engine_ = wasm_engine_new();
        if (engine_ == nullptr) return;
        ++liveStores();
        // The first reference initializes a new runtime, which needs its
        // process-wide settings before any module or cluster exists.
        if (freshRuntime && !configureRuntime()) return;
        store_ = wasm_store_new(engine_);
    }

    ~Store() {
        if (store_ != nullptr) wasm_store_delete(store_);
        if (engine_ != nullptr) {
            wasm_engine_delete(engine_);
            --liveStores();
        }
    }

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    wasm_store_t* get() const { return store_; }

private:
    // Engine references held by std/wasm, the only user of WAMR in the process.
    static size_t& liveStores() {
        static size_t count = 0;
        return count;
    }

    static bool configureRuntime() {
        // Failures are returned as structured errors; WAMR's own log is noise.
        wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
        wasm_runtime_set_max_thread_num(maxThreadsPerInstance);
        return doof_wasm_register_natives();
    }

    wasm_engine_t* engine_ = nullptr;
    wasm_store_t* store_ = nullptr;
};

// The C API reports parse failures without a message, so on failure the
// binary is parsed once more through the runtime API to recover it.
inline std::string loadFailureMessage(const NativeWasmBytes& bytes) {
    std::vector<uint8_t> copy(*bytes);
    char error[128] = {0};
    wasm_module_t module = wasm_runtime_load(copy.data(), static_cast<uint32_t>(copy.size()), error, sizeof(error));
    if (module != nullptr) {
        wasm_runtime_unload(module);
        return "WebAssembly binary could not be loaded";
    }
    return error[0] == '\0' ? "WebAssembly binary could not be loaded" : std::string(error);
}

// Loads a module into `store`, which owns the result. Call while holding engineLock().
inline doof::Result<wasm_module_t*, std::string> loadModule(wasm_store_t* store, const NativeWasmBytes& bytes) {
    if (bytes == nullptr || bytes->empty()) {
        return fail<wasm_module_t*>("parse", "", "WebAssembly binary is empty");
    }
    if (bytes->size() > UINT32_MAX) {
        return fail<wasm_module_t*>("parse", "", "WebAssembly binary is too large");
    }

    wasm_byte_vec_t binary{bytes->size(), reinterpret_cast<wasm_byte_t*>(bytes->data()), bytes->size(), sizeof(wasm_byte_t), nullptr};
    wasm_module_t* module = wasm_module_new(store, &binary);
    if (module == nullptr) {
        return fail<wasm_module_t*>("parse", "", loadFailureMessage(bytes));
    }
    return doof::Success<wasm_module_t*>{module};
}

inline std::string externKindName(wasm_externkind_t kind) {
    switch (kind) {
        case WASM_EXTERN_FUNC: return "function";
        case WASM_EXTERN_GLOBAL: return "global";
        case WASM_EXTERN_TABLE: return "table";
        case WASM_EXTERN_MEMORY: return "memory";
        default: return "unknown";
    }
}

// Appends the flattened descriptor fields shared with index.do:
// direction, module, name, kind, params, results, mutable.
inline void appendDescriptor(
    std::vector<std::string>& out,
    const std::string& direction,
    const std::string& module,
    const std::string& name,
    const wasm_externtype_t* type
) {
    const wasm_externkind_t kind = wasm_externtype_kind(type);
    std::string params;
    std::string results;
    std::string mutability;
    if (kind == WASM_EXTERN_FUNC) {
        const wasm_functype_t* function = wasm_externtype_as_functype_const(type);
        params = joinKinds(wasm_functype_params(function));
        results = joinKinds(wasm_functype_results(function));
    } else if (kind == WASM_EXTERN_GLOBAL) {
        const wasm_globaltype_t* global = wasm_externtype_as_globaltype_const(type);
        results = kindName(wasm_valtype_kind(wasm_globaltype_content(global)));
        mutability = wasm_globaltype_mutability(global) == WASM_VAR ? "mutable" : "";
    }
    out.insert(out.end(), {direction, module, name, externKindName(kind), params, results, mutability});
}

inline doof::Result<NativeWasmStrings, std::string> describe(const NativeWasmBytes& bytes) {
    std::lock_guard<std::mutex> lock(engineLock());
    Store store;
    if (store.get() == nullptr) {
        return fail<NativeWasmStrings>("configure", "", "could not create the Wasm store");
    }
    auto loaded = loadModule(store.get(), bytes);
    if (doof::is_failure(loaded)) return doof::Failure<std::string>{doof::failure_error(loaded)};
    wasm_module_t* module = doof::success_value(loaded);

    auto out = std::make_shared<std::vector<std::string>>();
    wasm_importtype_vec_t imports;
    wasm_module_imports(module, &imports);
    for (size_t index = 0; index < imports.size; ++index) {
        const wasm_importtype_t* import = imports.data[index];
        appendDescriptor(*out, "import", fromName(wasm_importtype_module(import)),
            fromName(wasm_importtype_name(import)), wasm_importtype_type(import));
    }
    wasm_importtype_vec_delete(&imports);

    wasm_exporttype_vec_t exports;
    wasm_module_exports(module, &exports);
    for (size_t index = 0; index < exports.size; ++index) {
        const wasm_exporttype_t* exported = exports.data[index];
        appendDescriptor(*out, "export", "", fromName(wasm_exporttype_name(exported)), wasm_exporttype_type(exported));
    }
    wasm_exporttype_vec_delete(&exports);
    return doof::Success<NativeWasmStrings>{out};
}

} // namespace doof_wasm

inline doof::Result<NativeWasmStrings, std::string> doof_wasm_describe(const NativeWasmBytes& bytes) {
    return doof_wasm::describe(bytes);
}
