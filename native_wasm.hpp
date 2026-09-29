#pragma once

// std/wasm native bridge over an embedded WAMR interpreter.

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "native_wasm_values.hpp"
#include "native_wasm_engine.hpp"
#include "native_wasm_link.hpp"

inline bool doof_wasm_supports_host_imports() {
    return true;
}

class NativeWasmInstance {
public:
    static doof::Result<std::shared_ptr<NativeWasmInstance>, std::string> instantiate(
        const NativeWasmBytes& bytes,
        int32_t stackSize,
        int64_t memoryLimitBytes,
        int32_t instructionLimit,
        const doof_wasm::ImportList& imports
    ) {
        using Self = std::shared_ptr<NativeWasmInstance>;
        if (stackSize <= 0 || memoryLimitBytes <= 0) {
            return doof_wasm::fail<Self>("configure", "", "resource limits must be greater than zero");
        }
        const std::string invalid = doof_wasm::validateImports(imports);
        if (!invalid.empty()) return doof_wasm::fail<Self>("import", "", invalid);

        // On failure the partially built instance is released after the
        // engine lock is dropped, since release() takes it too.
        auto instance = Self(new NativeWasmInstance());
        std::string error;
        {
            std::lock_guard<std::mutex> lock(doof_wasm::engineLock());
            error = instance->setUp(bytes, stackSize, memoryLimitBytes, imports);
        }
        if (!error.empty()) return doof::Failure<std::string>{error};
        if (instructionLimit >= 0) wasm_runtime_set_instruction_count_limit(instance->execEnv_, instructionLimit);
        return doof::Success<Self>{instance};
    }

    ~NativeWasmInstance() { release(); }

    doof::Result<NativeWasmValues, std::string> call(const std::string& functionName, const NativeWasmValues& arguments);
    doof::Result<NativeWasmValue, std::string> getGlobal(const std::string& name);
    doof::Result<void, std::string> setGlobal(const std::string& name, const NativeWasmValue& value);
    doof::Result<int64_t, std::string> memorySize();
    doof::Result<NativeWasmBytes, std::string> readMemory(int64_t offset, int32_t length);
    doof::Result<void, std::string> writeMemory(int64_t offset, const NativeWasmBytes& bytes);

    doof::Result<void, std::string> close() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (insideHostImport()) {
            return doof_wasm::fail<void>("lifecycle", "", "cannot close an instance from one of its host imports");
        }
        release();
        return doof::Success<void>{};
    }

private:
    NativeWasmInstance() = default;

    std::string setUp(const NativeWasmBytes& bytes, int32_t stackSize, int64_t memoryLimitBytes, const doof_wasm::ImportList& imports);
    std::string instantiateModule(int32_t stackSize, int64_t memoryLimitBytes);

    bool isOpen() const { return runtime_ != nullptr; }

    bool insideHostImport() const {
        return doof_wasm::ThreadActivity::contains(doof_wasm::ThreadActivity::current().hosting, this);
    }

    wasm_memory_inst_t memory() const {
        return runtime_ == nullptr ? nullptr : wasm_runtime_get_default_memory(runtime_);
    }

    static uint64_t memoryBytes(wasm_memory_inst_t memory) {
        return memory == nullptr ? 0 : wasm_memory_get_cur_page_count(memory) * wasm_memory_get_bytes_per_page(memory);
    }

    doof::Result<uint8_t*, std::string> checkedRange(int64_t offset, int64_t length);
    wasm_extern_t* findExport(const std::string& name, wasm_externkind_t kind, wasm_extern_vec_t& exports);

    // Terminates any threads the module spawned, waits for them to exit,
    // then frees the runtime objects.
    void release() {
        if (runtime_ != nullptr) wasm_runtime_terminate(runtime_);
        std::lock_guard<std::mutex> lock(doof_wasm::engineLock());
        runtime_ = nullptr;
        execEnv_ = nullptr;
        instance_ = nullptr;
        module_ = nullptr;
        store_.reset();
        linked_.reset();
    }

    std::recursive_mutex mutex_;
    std::unique_ptr<doof_wasm::Store> store_;
    std::unique_ptr<doof_wasm::LinkedImports> linked_;
    std::shared_ptr<doof_wasm::HostFailure> failures_ = std::make_shared<doof_wasm::HostFailure>();
    wasm_module_t* module_ = nullptr;
    wasm_instance_t* instance_ = nullptr;
    wasm_module_inst_t runtime_ = nullptr;
    wasm_exec_env_t execEnv_ = nullptr;
};

#include "native_wasm_instance.hpp"
