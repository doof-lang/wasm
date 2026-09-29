#pragma once

// Host function imports for std/wasm, bound per instance through the wasm-c-api.

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "native_wasm_values.hpp"

namespace doof_wasm {

// Per-thread record of the instances this thread is executing, used to tell
// the calling thread apart from threads spawned by the module through
// wasi-threads, and to reject reentrant calls from host imports.
struct ThreadActivity {
    std::vector<const void*> calling;
    std::vector<const void*> hosting;

    static ThreadActivity& current() {
        static thread_local ThreadActivity activity;
        return activity;
    }

    static bool contains(const std::vector<const void*>& list, const void* instance) {
        return std::find(list.begin(), list.end(), instance) != list.end();
    }
};

// Pushes `instance` onto a ThreadActivity list for the current scope.
class ActivityScope {
public:
    ActivityScope(std::vector<const void*>& list, const void* instance) : list_(list) { list_.push_back(instance); }
    ~ActivityScope() { list_.pop_back(); }
    ActivityScope(const ActivityScope&) = delete;
    ActivityScope& operator=(const ActivityScope&) = delete;

private:
    std::vector<const void*>& list_;
};

// The first host failure raised during a call. Host imports on different
// wasm threads may fail concurrently, so access is locked.
class HostFailure {
public:
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        function_.clear();
        message_.clear();
    }

    void record(const std::string& function, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!function_.empty()) return;
        function_ = function;
        message_ = message;
    }

    bool take(std::string& function, std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (function_.empty()) return false;
        function = function_;
        message = message_;
        return true;
    }

private:
    std::mutex mutex_;
    std::string function_;
    std::string message_;
};

// One entry per imported function of an instance. Entries without a handler
// trap when called, so unresolved imports are reported on first use.
struct ImportBinding {
    std::string module;
    std::string name;
    std::shared_ptr<FunctionImport> import;
    wasm_store_t* store = nullptr;
    const void* owner = nullptr;
    std::shared_ptr<HostFailure> failures;
    // WAMR leaves result slots untyped, so declared result types are kept here.
    std::vector<wasm_valkind_t> resultKinds;

    std::string qualifiedName() const { return module + "." + name; }

    wasm_trap_t* trap(const std::string& message) const {
        wasm_message_t text;
        wasm_name_new_from_string_nt(&text, message.c_str());
        wasm_trap_t* result = wasm_trap_new(store, &text);
        wasm_byte_vec_delete(&text);
        return result;
    }

    wasm_trap_t* hostFailure(const std::string& message) {
        failures->record(qualifiedName(), message);
        return trap(message);
    }

    wasm_trap_t* invoke(const wasm_val_vec_t* arguments, wasm_val_vec_t* results) {
        if (import == nullptr) return trap("missing imported function " + qualifiedName());

        // Doof function values are bound to their actor domain, so handlers
        // may only run on the thread that called into the instance.
        ThreadActivity& activity = ThreadActivity::current();
        if (!ThreadActivity::contains(activity.calling, owner)) {
            return hostFailure("host imports cannot be called from threads spawned by the module");
        }

        auto values = std::make_shared<std::vector<NativeWasmValue>>();
        values->reserve(arguments->size);
        for (size_t index = 0; index < arguments->size; ++index) {
            if (!isNumericKind(arguments->data[index].kind)) {
                return hostFailure("unsupported host import argument type " + kindName(arguments->data[index].kind));
            }
            values->push_back(fromWasm(arguments->data[index]));
        }

        try {
            ActivityScope hosting(activity.hosting, owner);
            auto result = import->handler.call(values);
            if (doof::is_failure(result)) return hostFailure(doof::failure_error(result));

            const auto& returned = doof::success_value(result);
            const size_t suppliedCount = returned == nullptr ? 0 : returned->size();
            if (suppliedCount != resultKinds.size() || results->size != resultKinds.size()) {
                return hostFailure("expected " + std::to_string(resultKinds.size()) +
                    " results, received " + std::to_string(suppliedCount));
            }
            for (size_t index = 0; index < resultKinds.size(); ++index) {
                if (!toWasm((*returned)[index], resultKinds[index], results->data[index])) {
                    return hostFailure("result " + std::to_string(index) + " has the wrong type");
                }
            }
            return nullptr;
        } catch (const doof::Panic& error) {
            return hostFailure(error.what());
        } catch (const std::exception& error) {
            return hostFailure(error.what());
        } catch (...) {
            return hostFailure("host import raised an unknown native error");
        }
    }

    static wasm_trap_t* callback(void* env, const wasm_val_vec_t* arguments, wasm_val_vec_t* results) {
        return static_cast<ImportBinding*>(env)->invoke(arguments, results);
    }
};

using ImportList = std::shared_ptr<std::vector<std::shared_ptr<FunctionImport>>>;

// Checks configured imports for blanks and duplicates before any linking.
inline std::string validateImports(const ImportList& imports) {
    if (imports == nullptr) return "";
    for (size_t index = 0; index < imports->size(); ++index) {
        const auto& import = (*imports)[index];
        if (import == nullptr || import->module.empty() || import->name.empty() || !import->handler) {
            return "invalid host function import";
        }
        for (size_t previous = 0; previous < index; ++previous) {
            const auto& other = (*imports)[previous];
            if (other->module == import->module && other->name == import->name) {
                return "duplicate host function import " + import->module + "." + import->name;
            }
        }
    }
    return "";
}

inline std::shared_ptr<FunctionImport> findImport(const ImportList& imports, const std::string& module, const std::string& name) {
    if (imports == nullptr) return nullptr;
    for (const auto& import : *imports) {
        if (import->module == module && import->name == name) return import;
    }
    return nullptr;
}

} // namespace doof_wasm
