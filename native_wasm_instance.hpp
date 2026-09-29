#pragma once

// NativeWasmInstance setup and instance operations; included by native_wasm.hpp.

inline std::string NativeWasmInstance::setUp(
    const NativeWasmBytes& bytes,
    int32_t stackSize,
    int64_t memoryLimitBytes,
    const doof_wasm::ImportList& imports
) {
    store_ = std::make_unique<doof_wasm::Store>();
    if (store_->get() == nullptr) {
        return doof_wasm::encodeError("configure", "", "could not create the Wasm store");
    }
    auto loaded = doof_wasm::loadModule(store_->get(), bytes);
    if (doof::is_failure(loaded)) return doof::failure_error(loaded);
    module_ = doof::success_value(loaded);

    linked_ = std::make_unique<doof_wasm::LinkedImports>();
    const std::string linkError = linked_->link(store_->get(), module_, imports, this, failures_);
    if (!linkError.empty()) return doof_wasm::encodeError("import", "", linkError);
    return instantiateModule(stackSize, memoryLimitBytes);
}

inline std::string NativeWasmInstance::instantiateModule(int32_t stackSize, int64_t memoryLimitBytes) {
    const int64_t pages = std::clamp<int64_t>(memoryLimitBytes / 65536, 1, 65536);
    InstantiationArgs args{static_cast<uint32_t>(stackSize), 0, static_cast<uint32_t>(pages)};
    wasm_extern_vec_t externs = linked_->view();
    wasm_trap_t* trap = nullptr;

    // The start function runs here and may call host imports.
    failures_->clear();
    {
        doof_wasm::ActivityScope calling(doof_wasm::ThreadActivity::current().calling, this);
        instance_ = wasm_instance_new_with_args_ex(store_->get(), module_, &externs, &trap, &args);
    }
    if (instance_ == nullptr) {
        std::string message = "could not instantiate the module";
        if (trap != nullptr) {
            wasm_message_t text;
            wasm_trap_message(trap, &text);
            message = std::string(text.data, strnlen(text.data, text.size));
            wasm_byte_vec_delete(&text);
            wasm_trap_delete(trap);
        }
        std::string function;
        std::string hostMessage;
        if (failures_->take(function, hostMessage)) return doof_wasm::encodeError("host", function, hostMessage);

        // WAMR wraps failures as "<api> failed: WASM module instantiate failed: ...".
        const std::string marker = "instantiate failed: ";
        const size_t at = message.rfind(marker);
        if (at != std::string::npos) message.erase(0, at + marker.size());
        const bool trapped = message.rfind("Exception: ", 0) == 0;
        return doof_wasm::encodeError(trapped ? "start" : "instantiate", "", doof_wasm::stripExceptionPrefix(message));
    }

    runtime_ = doof_wasm_instance_runtime(instance_);
    execEnv_ = runtime_ == nullptr ? nullptr : wasm_runtime_get_exec_env_singleton(runtime_);
    if (execEnv_ == nullptr) {
        return doof_wasm::encodeError("instantiate", "", "could not create the execution environment");
    }
    // WAMR never lowers a memory below its declared initial size.
    if (memoryBytes(memory()) > static_cast<uint64_t>(memoryLimitBytes)) {
        return doof_wasm::encodeError("instantiate", "", "initial memory exceeds memoryLimitBytes");
    }
    return "";
}

inline doof::Result<NativeWasmValues, std::string> NativeWasmInstance::call(
    const std::string& functionName,
    const NativeWasmValues& arguments
) {
    using doof_wasm::fail;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!isOpen()) return fail<NativeWasmValues>("lifecycle", functionName, "instance is closed");
    if (insideHostImport()) return fail<NativeWasmValues>("lifecycle", functionName, "reentrant calls are not supported");

    wasm_function_inst_t function = wasm_runtime_lookup_function(runtime_, functionName.c_str());
    if (function == nullptr) return fail<NativeWasmValues>("lookup", functionName, "function is not exported");

    const uint32_t argumentCount = wasm_func_get_param_count(function, runtime_);
    const uint32_t resultCount = wasm_func_get_result_count(function, runtime_);
    const size_t suppliedCount = arguments == nullptr ? 0 : arguments->size();
    if (suppliedCount != argumentCount) {
        return fail<NativeWasmValues>("call", functionName,
            "expected " + std::to_string(argumentCount) + " arguments, received " + std::to_string(suppliedCount));
    }

    std::vector<wasm_valkind_t> argumentKinds(argumentCount);
    std::vector<wasm_valkind_t> resultKinds(resultCount);
    wasm_func_get_param_types(function, runtime_, argumentKinds.data());
    wasm_func_get_result_types(function, runtime_, resultKinds.data());
    for (wasm_valkind_t kind : resultKinds) {
        if (!doof_wasm::isNumericKind(kind)) {
            return fail<NativeWasmValues>("call", functionName, "unsupported result type " + doof_wasm::kindName(kind));
        }
    }

    std::vector<wasm_val_t> values(argumentCount);
    for (uint32_t index = 0; index < argumentCount; ++index) {
        if (!doof_wasm::toWasm((*arguments)[index], argumentKinds[index], values[index])) {
            return fail<NativeWasmValues>("call", functionName,
                "argument " + std::to_string(index) + " must be " + doof_wasm::kindName(argumentKinds[index]));
        }
    }

    std::vector<wasm_val_t> results(resultCount);
    failures_->clear();
    wasm_runtime_clear_exception(runtime_);
    bool succeeded;
    {
        doof_wasm::ActivityScope calling(doof_wasm::ThreadActivity::current().calling, this);
        succeeded = wasm_runtime_call_wasm_a(execEnv_, function, resultCount, results.data(), argumentCount, values.data());
    }
    if (!succeeded) {
        std::string hostFunction;
        std::string hostMessage;
        if (failures_->take(hostFunction, hostMessage)) {
            wasm_runtime_clear_exception(runtime_);
            return fail<NativeWasmValues>("host", hostFunction, hostMessage);
        }
        const char* exception = wasm_runtime_get_exception(runtime_);
        std::string message = exception == nullptr ? "call failed" : doof_wasm::stripExceptionPrefix(exception);
        wasm_runtime_clear_exception(runtime_);
        return fail<NativeWasmValues>("call", functionName, message);
    }

    auto out = std::make_shared<std::vector<NativeWasmValue>>();
    out->reserve(resultCount);
    for (const wasm_val_t& result : results) out->push_back(doof_wasm::fromWasm(result));
    return doof::Success<NativeWasmValues>{out};
}

#include "native_wasm_memory.hpp"
