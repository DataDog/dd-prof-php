#ifndef DATADOG_PHP_PROFILING_CONFIG_H
#define DATADOG_PHP_PROFILING_CONFIG_H

#include <components/arena/arena.h>
#include <components/log/log.h>
#include <components/string_view/string_view.h>
#include <ddprof/ffi.h>
#include <profiling/env/env.h>
#include <stdbool.h>

typedef struct datadog_php_profiling_config_s {
  bool profiling_enabled;
  bool profiling_experimental_cpu_enabled;
  datadog_php_log_level profiling_log_level;
  ddprof_ffi_EndpointV3 endpoint;
  ddprof_ffi_CharSlice env;
  ddprof_ffi_CharSlice service;
  ddprof_ffi_ParseTagsResult tags;
  ddprof_ffi_CharSlice version;
} datadog_php_profiling_config;

void datadog_php_profiling_config_default_ctor(
    datadog_php_profiling_config *config);
void datadog_php_profiling_config_ctor(datadog_php_profiling_config *config,
                                       datadog_php_arena *arena,
                                       const datadog_php_profiling_env *env);

#endif // DATADOG_PHP_PROFILING_CONFIG_H
