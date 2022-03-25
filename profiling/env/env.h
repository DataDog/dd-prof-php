#ifndef DATADOG_PHP_PROFILING_ENV_H
#define DATADOG_PHP_PROFILING_ENV_H

#include <SAPI.h>
#include <components/arena/arena.h>
#include <components/string_view/string_view.h>
#include <ddprof/ffi.h>

typedef struct datadog_php_profiling_env_s {
  ddprof_ffi_CharSlice agent_host;
  ddprof_ffi_CharSlice env;
  ddprof_ffi_CharSlice profiling_enabled;
  ddprof_ffi_CharSlice profiling_experimental_cpu_enabled;
  ddprof_ffi_CharSlice profiling_log_level;
  ddprof_ffi_CharSlice service;
  ddprof_ffi_CharSlice trace_agent_port;
  ddprof_ffi_CharSlice trace_agent_url;
  ddprof_ffi_CharSlice version;
} datadog_php_profiling_env;

#if __cplusplus
#define C_STATIC(...)
#else
#define C_STATIC(...) static __VA_ARGS__
#endif

__attribute__((nonnull(1, 2, 3))) bool
datadog_php_profiling_getenvs(datadog_php_profiling_env *env,
                              const sapi_module_struct *sapi,
                              datadog_php_arena *arena);

#undef C_STATIC

#endif // DATADOG_PHP_PROFILING_ENV_H
