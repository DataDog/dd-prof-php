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

  // Ensure names[i] and values[i] are in sync.
  const char *names[9] = {
      "DD_AGENT_HOST",
      "DD_ENV",
      "DD_PROFILING_ENABLED",
      "DD_PROFILING_EXPERIMENTAL_CPU_ENABLED",
      "DD_PROFILING_LOG_LEVEL",
      "DD_SERVICE",
      "DD_TRACE_AGENT_PORT",
      "DD_TRACE_AGENT_URL",
      "DD_VERSION",
  };

  datadog_php_string_view *values[9] = {
      &env->agent_host,
      &env->env,
      &env->profiling_enabled,
      &env->profiling_experimental_cpu_enabled,
      &env->profiling_log_level,
      &env->service,
      &env->trace_agent_port,
      &env->trace_agent_url,
      &env->version,
  };

#if PHP_VERSION_ID >= 70400
  tsrm_env_lock();
#endif

  /* An alignment of max_align_t should be good enough for any operation this
   * pointer may be passed to, but profiling isn't intentionally relying on it.
   */
  static const uint32_t align = _Alignof(max_align_t);

  size_t i; // declared out-of-loop because of env unlock before returning
  for (i = 0; i != 9; ++i) {
    const char *name = names[i];

    // try the SAPI first
    char *val =
        sapi->getenv ? sapi->getenv((getenv_char *)name, strlen(name)) : NULL;

    // fall back to the OS
    if (!val) {
      val = getenv(name);
    }

    datadog_php_string_view view = datadog_php_string_view_from_cstr(val);
    uint8_t *bytes = datadog_php_arena_alloc(arena, view.len + 1, align);
    if (!bytes) {
      break;
    }

    /* The extra byte is safe to copy, as the getenv functions return a
     * pointer to char with the length encoded by being null terminated.
     */
    memcpy(bytes, view.ptr, view.len + 1);

    values[i]->len = view.len;
    values[i]->ptr = (const char *)bytes;
  }

#if PHP_VERSION_ID >= 70400
  tsrm_env_unlock();
#endif

  return i == 9;
}
