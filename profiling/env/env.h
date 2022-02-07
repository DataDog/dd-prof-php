#ifndef DATADOG_PHP_PROFILING_ENV_H
#define DATADOG_PHP_PROFILING_ENV_H

#include <SAPI.h>
#include <components/arena/arena.h>
#include <components/string_view/string_view.h>

typedef struct datadog_php_profiling_env_s {
  datadog_php_string_view agent_host;
  datadog_php_string_view env;
  datadog_php_string_view profiling_enabled;
  datadog_php_string_view profiling_experimental_cpu_enabled;
  datadog_php_string_view profiling_log_level;
  datadog_php_string_view service;
  datadog_php_string_view trace_agent_port;
  datadog_php_string_view trace_agent_url;
  datadog_php_string_view version;
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
