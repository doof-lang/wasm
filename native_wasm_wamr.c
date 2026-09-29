/*
 * Private bridge into WAMR internals for std/wasm.
 *
 * std/wasm links host functions through the standard wasm-c-api because it
 * binds callbacks per instance, and WAMR's thread manager copies those
 * bindings into every wasi-threads thread. The C API has two gaps that this
 * file fills using the pinned internal layout in vendor/wamr:
 *
 * - It does not expose the runtime instance behind a wasm_instance_t, which
 *   std/wasm needs for its execution environment (stack size, instruction
 *   limit), direct calls, and memory index 0.
 * - Its import vector is positional and must contain an entry for every
 *   import. An imported memory is created by the runtime from its declared
 *   type, and "wasi"."thread-spawn" is already linked to WAMR's native
 *   implementation, so both need inert placeholders that leave them alone.
 *
 * This file is compiled with WAMR's include paths and defines so the struct
 * layouts always match the vendored revision.
 */

#include "wasm_c_api_internal.h"

wasm_module_inst_t
doof_wasm_instance_runtime(const wasm_instance_t *instance)
{
    return instance == NULL ? NULL : instance->inst_comm_rt;
}

/* A function extern without a type is skipped by WAMR's C-API linker, so the
   import keeps whatever the loader resolved for it. */
wasm_extern_t *
doof_wasm_placeholder_function(wasm_store_t *store)
{
    wasm_func_t *func = wasm_runtime_malloc(sizeof(wasm_func_t));
    if (func == NULL)
        return NULL;
    memset(func, 0, sizeof(wasm_func_t));
    func->store = store;
    func->kind = WASM_EXTERN_FUNC;
    func->func_idx_rt = (uint16)-1;
    return (wasm_extern_t *)func;
}

/* Memory externs are ignored by WAMR's C-API linker. */
wasm_extern_t *
doof_wasm_placeholder_memory(wasm_store_t *store)
{
    wasm_memory_t *memory = wasm_runtime_malloc(sizeof(wasm_memory_t));
    if (memory == NULL)
        return NULL;
    memset(memory, 0, sizeof(wasm_memory_t));
    memory->store = store;
    memory->kind = WASM_EXTERN_MEMORY;
    return (wasm_extern_t *)memory;
}

void
doof_wasm_placeholder_delete(wasm_extern_t *placeholder)
{
    if (placeholder != NULL)
        wasm_runtime_free(placeholder);
}

/*
 * WAMR registers "wasi"."thread-spawn" as a classic native, which it calls
 * through the per-architecture invokeNative assembly trampolines. std/wasm
 * builds the portable C invokeNative instead (no assembly in the package),
 * and that cannot pass the 64-bit exec_env pointer correctly. This raw
 * native calls WAMR's own spawn implementation with a proper C prototype and
 * takes precedence because later registrations are searched first.
 */

uint32
get_lib_wasi_threads_export_apis(NativeSymbol **p_lib_wasi_threads_apis);

typedef int32 (*doof_wasm_thread_spawn_fn)(wasm_exec_env_t exec_env,
                                           uint32 start_arg);

static doof_wasm_thread_spawn_fn doof_wasm_thread_spawn = NULL;

static void
doof_wasm_thread_spawn_raw(wasm_exec_env_t exec_env, uint64 *args)
{
    uint32 start_arg = *(uint32 *)&args[0];
    int32 thread_id = doof_wasm_thread_spawn(exec_env, start_arg);
    *(int32 *)&args[0] = thread_id;
}

static NativeSymbol doof_wasm_raw_symbols[] = {
    { "thread-spawn", (void *)doof_wasm_thread_spawn_raw, "(i)i", NULL },
};

/* Call after every fresh runtime initialization, before loading modules. */
bool
doof_wasm_register_natives(void)
{
    NativeSymbol *symbols = NULL;
    uint32 count = get_lib_wasi_threads_export_apis(&symbols), i;

    for (i = 0; i < count; i++) {
        if (strcmp(symbols[i].symbol, "thread-spawn") == 0)
            doof_wasm_thread_spawn = (doof_wasm_thread_spawn_fn)symbols[i].func_ptr;
    }
    if (doof_wasm_thread_spawn == NULL)
        return false;
    return wasm_runtime_register_natives_raw("wasi", doof_wasm_raw_symbols, 1);
}
