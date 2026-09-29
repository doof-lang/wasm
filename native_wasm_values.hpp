#pragma once

// Value, type, and error helpers shared by the std/wasm native bridge.

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "doof_runtime.hpp"

extern "C" {
#include "wasm_c_api.h"
#include "wasm_export.h"
}

using NativeWasmValue = std::variant<int32_t, int64_t, float, double>;
using NativeWasmValues = std::shared_ptr<std::vector<NativeWasmValue>>;
using NativeWasmBytes = std::shared_ptr<std::vector<uint8_t>>;
using NativeWasmStrings = std::shared_ptr<std::vector<std::string>>;

namespace doof_wasm {

// Errors cross into Doof as "stage|function|message" and are decoded there.
inline std::string encodeError(const std::string& stage, const std::string& functionName, const std::string& message) {
    return stage + "|" + functionName + "|" + message;
}

template <typename T>
doof::Result<T, std::string> fail(const std::string& stage, const std::string& functionName, const std::string& message) {
    return doof::Failure<std::string>{encodeError(stage, functionName, message)};
}

inline bool isNumericKind(wasm_valkind_t kind) {
    return kind == WASM_I32 || kind == WASM_I64 || kind == WASM_F32 || kind == WASM_F64;
}

inline std::string kindName(wasm_valkind_t kind) {
    switch (kind) {
        case WASM_I32: return "i32";
        case WASM_I64: return "i64";
        case WASM_F32: return "f32";
        case WASM_F64: return "f64";
        case WASM_V128: return "v128";
        case WASM_EXTERNREF: return "externref";
        case WASM_FUNCREF: return "funcref";
        default: return "unknown";
    }
}

// Converts a Doof value to a Wasm value of exactly `kind`; values are never coerced.
inline bool toWasm(const NativeWasmValue& value, wasm_valkind_t kind, wasm_val_t& out) {
    out = wasm_val_t{};
    out.kind = kind;
    switch (kind) {
        case WASM_I32: {
            const auto* typed = std::get_if<int32_t>(&value);
            if (typed == nullptr) return false;
            out.of.i32 = *typed;
            return true;
        }
        case WASM_I64: {
            const auto* typed = std::get_if<int64_t>(&value);
            if (typed == nullptr) return false;
            out.of.i64 = *typed;
            return true;
        }
        case WASM_F32: {
            const auto* typed = std::get_if<float>(&value);
            if (typed == nullptr) return false;
            out.of.f32 = *typed;
            return true;
        }
        case WASM_F64: {
            const auto* typed = std::get_if<double>(&value);
            if (typed == nullptr) return false;
            out.of.f64 = *typed;
            return true;
        }
        default:
            return false;
    }
}

inline NativeWasmValue fromWasm(const wasm_val_t& value) {
    switch (value.kind) {
        case WASM_I64: return value.of.i64;
        case WASM_F32: return value.of.f32;
        case WASM_F64: return value.of.f64;
        default: return value.of.i32;
    }
}

inline std::string fromName(const wasm_name_t* name) {
    if (name == nullptr || name->data == nullptr) return "";
    // WAMR's names count a trailing NUL in their size.
    size_t size = name->size;
    while (size > 0 && name->data[size - 1] == '\0') --size;
    return std::string(name->data, size);
}

inline std::string joinKinds(const wasm_valtype_vec_t* types) {
    std::string joined;
    for (size_t index = 0; types != nullptr && index < types->size; ++index) {
        if (index > 0) joined += ",";
        joined += kindName(wasm_valtype_kind(types->data[index]));
    }
    return joined;
}

// WAMR prefixes runtime exceptions; the stage already conveys that context.
inline std::string stripExceptionPrefix(std::string message) {
    static const std::string prefix = "Exception: ";
    if (message.rfind(prefix, 0) == 0) message.erase(0, prefix.size());
    return message;
}

} // namespace doof_wasm
