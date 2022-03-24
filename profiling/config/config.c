#include "config.h"

#include <profiling/datadog-profiling.h>

void datadog_php_profiling_config_default_ctor(
    datadog_php_profiling_config *config) {
  datadog_php_profiling_config tmp = {
      .profiling_enabled = false,
      .profiling_experimental_cpu_enabled = false,
      .profiling_log_level = DATADOG_PHP_LOG_OFF,
      .endpoint = ddprof_ffi_EndpointV3_agent(
          DDPROF_FFI_CHARSLICE_C("http://localhost:8126")),
      .env = DATADOG_PHP_STRING_VIEW_INIT,
      .service = DATADOG_PHP_STRING_VIEW_INIT,
      .version = DATADOG_PHP_STRING_VIEW_INIT,
  };
  *config = tmp;
}

static ddprof_ffi_CharSlice
profiling_config_endpoint_str(datadog_php_arena *arena,
                              const datadog_php_profiling_env *env) {
  ddprof_ffi_CharSlice default_endpoint = DDPROF_FFI_CHARSLICE_C("http://localhost:8126");

  // todo: DD_SITE + DD_API_KEY

  // prioritize URL over HOST + PORT
  datadog_php_string_view url = env->trace_agent_url;
  if (url.len) {
    return (ddprof_ffi_CharSlice){.ptr = url.ptr, .len = url.len};
  }

  datadog_php_string_view env_host = env->agent_host;
  const char *host =
      env_host.len && env_host.ptr[0] ? env_host.ptr : "localhost";

  datadog_php_string_view env_port = env->trace_agent_port;
  const char *port = env_port.len && env_port.ptr[0] ? env_port.ptr : "8126";

  // todo: can I log here?
  int size = snprintf(NULL, 0, "http://%s:%s", host, port);
  if (size <= 0) {
    return default_endpoint;
  }

  static const uint32_t align = _Alignof(max_align_t);
  uint8_t *buffer = datadog_php_arena_alloc(arena, size + 1, align);
  if (!buffer) {
    return default_endpoint;
  }

  int result = snprintf((char *)buffer, size + 1, "http://%s:%s", host, port);
  if (result != size) {
    return default_endpoint;
  }

  return (ddprof_ffi_CharSlice){
      .ptr = buffer,
      .len = size,
  };
}

static ddprof_ffi_EndpointV3
profiling_config_endpoint(datadog_php_arena *arena,
                          const datadog_php_profiling_env *env) {
  return ddprof_ffi_EndpointV3_agent(profiling_config_endpoint_str(arena, env));
}

void datadog_php_profiling_config_ctor(datadog_php_profiling_config *config,
                                       datadog_php_arena *arena,
                                       const datadog_php_profiling_env *env) {
  config->profiling_enabled =
      datadog_php_string_view_is_boolean_true(env->profiling_enabled);

  config->profiling_experimental_cpu_enabled =
      datadog_php_string_view_is_boolean_true(
          env->profiling_experimental_cpu_enabled);

  config->profiling_log_level =
      datadog_php_log_level_detect(env->profiling_log_level);

  config->endpoint = profiling_config_endpoint(arena, env);
  config->env = env->env;
  config->service = env->service;
  config->version = env->version;
}
