#pragma once

// NativeWasmInstance linear-memory and global access; included by native_wasm_instance.hpp.

inline doof::Result<uint8_t*, std::string> NativeWasmInstance::checkedRange(int64_t offset, int64_t length) {
    using doof_wasm::fail;
    if (!isOpen()) return fail<uint8_t*>("lifecycle", "", "instance is closed");
    if (offset < 0 || length < 0) return fail<uint8_t*>("memory", "", "offset and length must not be negative");

    wasm_memory_inst_t memory = this->memory();
    if (memory == nullptr) return fail<uint8_t*>("memory", "", "module has no linear memory");

    const uint64_t size = memoryBytes(memory);
    const uint64_t start = static_cast<uint64_t>(offset);
    if (start > size || static_cast<uint64_t>(length) > size - start) {
        return fail<uint8_t*>("memory", "", "memory range is out of bounds");
    }
    return doof::Success<uint8_t*>{static_cast<uint8_t*>(wasm_memory_get_base_address(memory)) + start};
}

inline doof::Result<int64_t, std::string> NativeWasmInstance::memorySize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!isOpen()) return doof_wasm::fail<int64_t>("lifecycle", "", "instance is closed");
    return doof::Success<int64_t>{static_cast<int64_t>(memoryBytes(memory()))};
}

inline doof::Result<NativeWasmBytes, std::string> NativeWasmInstance::readMemory(int64_t offset, int32_t length) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto range = checkedRange(offset, length);
    if (doof::is_failure(range)) return doof::Failure<std::string>{doof::failure_error(range)};

    auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(length));
    if (length > 0) std::memcpy(bytes->data(), doof::success_value(range), bytes->size());
    return doof::Success<NativeWasmBytes>{bytes};
}

inline doof::Result<void, std::string> NativeWasmInstance::writeMemory(int64_t offset, const NativeWasmBytes& bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const int64_t length = bytes == nullptr ? 0 : static_cast<int64_t>(bytes->size());
    auto range = checkedRange(offset, length);
    if (doof::is_failure(range)) return doof::Failure<std::string>{doof::failure_error(range)};

    if (length > 0) std::memcpy(doof::success_value(range), bytes->data(), bytes->size());
    return doof::Success<void>{};
}

// Returns the export named `name` if it has `kind`. `exports` receives the
// instance's export vector, which the caller deletes.
inline wasm_extern_t* NativeWasmInstance::findExport(const std::string& name, wasm_externkind_t kind, wasm_extern_vec_t& exports) {
    wasm_instance_exports(instance_, &exports);
    wasm_exporttype_vec_t types;
    wasm_module_exports(module_, &types);
    wasm_extern_t* found = nullptr;
    for (size_t index = 0; index < types.size && index < exports.size; ++index) {
        if (doof_wasm::fromName(wasm_exporttype_name(types.data[index])) == name &&
            wasm_extern_kind(exports.data[index]) == kind) {
            found = exports.data[index];
            break;
        }
    }
    wasm_exporttype_vec_delete(&types);
    return found;
}

inline doof::Result<NativeWasmValue, std::string> NativeWasmInstance::getGlobal(const std::string& name) {
    using doof_wasm::fail;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!isOpen()) return fail<NativeWasmValue>("lifecycle", name, "instance is closed");

    wasm_extern_vec_t exports;
    wasm_extern_t* found = findExport(name, WASM_EXTERN_GLOBAL, exports);
    doof::Result<NativeWasmValue, std::string> result = fail<NativeWasmValue>("lookup", name, "global is not exported");
    if (found != nullptr) {
        wasm_val_t value;
        wasm_global_get(wasm_extern_as_global(found), &value);
        result = doof_wasm::isNumericKind(value.kind)
            ? doof::Result<NativeWasmValue, std::string>{doof::Success<NativeWasmValue>{doof_wasm::fromWasm(value)}}
            : fail<NativeWasmValue>("global", name, "unsupported global type " + doof_wasm::kindName(value.kind));
    }
    wasm_extern_vec_delete(&exports);
    return result;
}

inline doof::Result<void, std::string> NativeWasmInstance::setGlobal(const std::string& name, const NativeWasmValue& value) {
    using doof_wasm::fail;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!isOpen()) return fail<void>("lifecycle", name, "instance is closed");

    wasm_extern_vec_t exports;
    wasm_extern_t* found = findExport(name, WASM_EXTERN_GLOBAL, exports);
    doof::Result<void, std::string> result = fail<void>("lookup", name, "global is not exported");
    if (found != nullptr) {
        wasm_global_t* global = wasm_extern_as_global(found);
        wasm_globaltype_t* type = wasm_global_type(global);
        const wasm_valkind_t kind = wasm_valtype_kind(wasm_globaltype_content(type));
        const bool isMutable = wasm_globaltype_mutability(type) == WASM_VAR;
        wasm_globaltype_delete(type);

        wasm_val_t converted;
        if (!isMutable) {
            result = fail<void>("global", name, "global is immutable");
        } else if (!doof_wasm::toWasm(value, kind, converted)) {
            result = fail<void>("global", name, "value must be " + doof_wasm::kindName(kind));
        } else {
            wasm_global_set(global, &converted);
            result = doof::Success<void>{};
        }
    }
    wasm_extern_vec_delete(&exports);
    return result;
}
