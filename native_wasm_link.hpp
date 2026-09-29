#pragma once

// Builds the positional wasm-c-api import vector for one std/wasm instance.

#include <memory>
#include <string>
#include <vector>

#include "native_wasm_imports.hpp"

extern "C" {
wasm_extern_t* doof_wasm_placeholder_function(wasm_store_t* store);
wasm_extern_t* doof_wasm_placeholder_memory(wasm_store_t* store);
void doof_wasm_placeholder_delete(wasm_extern_t* placeholder);
}

namespace doof_wasm {

class LinkedImports {
public:
    LinkedImports() = default;
    LinkedImports(const LinkedImports&) = delete;
    LinkedImports& operator=(const LinkedImports&) = delete;
    ~LinkedImports() { release(); }

    // Borrowed view for wasm_instance_new; the externs stay owned here.
    wasm_extern_vec_t view() {
        return wasm_extern_vec_t{externs_.size(), externs_.data(), externs_.size(), sizeof(wasm_extern_t*), nullptr};
    }

    // Call while holding engineLock(), before the owning store is deleted.
    void release() {
        for (wasm_func_t* function : hostFunctions_) wasm_func_delete(function);
        for (wasm_extern_t* placeholder : placeholders_) doof_wasm_placeholder_delete(placeholder);
        hostFunctions_.clear();
        placeholders_.clear();
        externs_.clear();
    }

    // Links every import of `module`; returns an error message or "".
    std::string link(
        wasm_store_t* store,
        wasm_module_t* module,
        const ImportList& configured,
        const void* owner,
        const std::shared_ptr<HostFailure>& failures
    ) {
        wasm_importtype_vec_t imports;
        wasm_module_imports(module, &imports);
        std::string error;
        size_t matched = 0;
        for (size_t index = 0; index < imports.size && error.empty(); ++index) {
            const wasm_importtype_t* import = imports.data[index];
            const std::string moduleName = fromName(wasm_importtype_module(import));
            const std::string name = fromName(wasm_importtype_name(import));
            const wasm_externtype_t* type = wasm_importtype_type(import);
            switch (wasm_externtype_kind(type)) {
                case WASM_EXTERN_FUNC: {
                    auto handler = findImport(configured, moduleName, name);
                    if (handler != nullptr) ++matched;
                    // Imports WAMR already provides natively ("wasi"."thread-spawn")
                    // keep that implementation unless the caller overrides them.
                    if (handler == nullptr && wasm_importtype_is_linked(import)) {
                        error = addPlaceholder(doof_wasm_placeholder_function(store));
                    } else {
                        error = addFunction(store, wasm_externtype_as_functype_const(type), moduleName, name, handler, owner, failures);
                    }
                    break;
                }
                case WASM_EXTERN_MEMORY:
                    // The runtime creates imported (typically shared) memory from its declared type.
                    error = addPlaceholder(doof_wasm_placeholder_memory(store));
                    break;
                default:
                    error = "unsupported " + std::string(wasm_externtype_kind(type) == WASM_EXTERN_GLOBAL ? "global" : "table") +
                        " import " + moduleName + "." + name;
                    break;
            }
        }
        wasm_importtype_vec_delete(&imports);
        if (!error.empty()) return error;

        const size_t configuredCount = configured == nullptr ? 0 : configured->size();
        if (matched != configuredCount) {
            for (const auto& import : *configured) {
                if (!isImported(import)) return "module does not import " + import->module + "." + import->name;
            }
        }
        return "";
    }

private:
    std::string addPlaceholder(wasm_extern_t* placeholder) {
        if (placeholder == nullptr) return "could not allocate an import placeholder";
        placeholders_.push_back(placeholder);
        externs_.push_back(placeholder);
        return "";
    }

    std::string addFunction(
        wasm_store_t* store,
        const wasm_functype_t* type,
        const std::string& moduleName,
        const std::string& name,
        const std::shared_ptr<FunctionImport>& handler,
        const void* owner,
        const std::shared_ptr<HostFailure>& failures
    ) {
        auto binding = std::make_unique<ImportBinding>();
        binding->module = moduleName;
        binding->name = name;
        binding->import = handler;
        binding->store = store;
        binding->owner = owner;
        binding->failures = failures;
        const wasm_valtype_vec_t* results = wasm_functype_results(type);
        // WAMR's interpreters copy host results through a 64-bit buffer, so
        // more than one result would overflow it.
        if (handler != nullptr && results->size > 1) {
            return "host import " + binding->qualifiedName() + " returns " + std::to_string(results->size) +
                " values; host imports may return at most one";
        }
        for (size_t index = 0; index < results->size; ++index) {
            binding->resultKinds.push_back(wasm_valtype_kind(results->data[index]));
        }

        wasm_func_t* function = wasm_func_new_with_env(store, type, &ImportBinding::callback, binding.get(), nullptr);
        if (function == nullptr) return "could not create host function " + binding->qualifiedName();
        hostFunctions_.push_back(function);
        externs_.push_back(wasm_func_as_extern(function));
        linkedNames_.push_back(binding->qualifiedName());
        bindings_.push_back(std::move(binding));
        return "";
    }

    bool isImported(const std::shared_ptr<FunctionImport>& import) const {
        const std::string name = import->module + "." + import->name;
        for (const auto& linked : linkedNames_) {
            if (linked == name) return true;
        }
        return false;
    }

    std::vector<wasm_extern_t*> externs_;
    std::vector<wasm_func_t*> hostFunctions_;
    std::vector<wasm_extern_t*> placeholders_;
    std::vector<std::string> linkedNames_;
    std::vector<std::unique_ptr<ImportBinding>> bindings_;
};

} // namespace doof_wasm
