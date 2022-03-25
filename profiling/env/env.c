#include "env.h"

#include <stddef.h>
#include <stdlib.h>
#include <strings.h>

#if PHP_VERSION_ID >= 80000
typedef const char getenv_char;
#elif PHP_VERSION_ID >= 70000
typedef char getenv_char;
#else
#error Unexpected PHP_VESION_ID for getenv detection.
#endif

__attribute__((nonnull(1, 2, 3))) bool
datadog_php_profiling_getenvs(datadog_php_profiling_env *env,
                              const sapi_module_struct *sapi,
                              datadog_php_arena *arena) {

  struct {
    const char *name;
    ddprof_ffi_CharSlice *value;
  } envs[] = {
      {"DD_AGENT_HOST", &env->agent_host},
      {"DD_ENV", &env->env},
      {"DD_PROFILING_ENABLED", &env->profiling_enabled},
      {"DD_PROFILING_EXPERIMENTAL_CPU_ENABLED",
       &env->profiling_experimental_cpu_enabled},
      {"DD_PROFILING_LOG_LEVEL", &env->profiling_log_level},
      {"DD_SERVICE", &env->service},
      {"DD_TRACE_AGENT_PORT", &env->trace_agent_port},
      {"DD_TRACE_AGENT_URL", &env->trace_agent_url},
      {"DD_VERSION", &env->version},
  };

#if PHP_VERSION_ID >= 70400
  tsrm_env_lock();
#endif

  size_t i; // declared out-of-loop because of env unlock before returning
  size_t n = sizeof envs / sizeof envs[0];
  for (i = 0; i != n; ++i) {
    const char *name = envs[i].name;

    // try the SAPI first
    char *val =
        sapi->getenv ? sapi->getenv((getenv_char *)name, strlen(name)) : NULL;

    // fall back to the OS
    if (!val) {
      val = getenv(name);
    }

    datadog_php_string_view view = datadog_php_string_view_from_cstr(val);
    const char *bytes = datadog_php_arena_alloc_str(arena, view.len, view.ptr);
    if (!bytes) {
      break;
    }

    envs[i].value->len = view.len;
    envs[i].value->ptr = bytes;
  }

#if PHP_VERSION_ID >= 70400
  tsrm_env_unlock();
#endif

  return i == n;
}
