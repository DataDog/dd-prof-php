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

extern inline void
datadog_php_profiling_env_default_ctor(datadog_php_profiling_env *env);

typedef struct datadog_php_profiling_getenv_result_s {
  enum {
    DATADOG_PHP_PROFILING_GETENV_OK,
    DATADOG_PHP_PROFILING_GETENV_ERR,
  } tag;
  union {
    datadog_php_string_view ok;
    enum {
      DATADOG_PHP_PROFILING_GETENV_ERR_NOMEM,
      DATADOG_PHP_PROFILING_GETENV_ERR_NOVAL,
    } err;
  };
} datadog_php_profiling_getenv_result;

/**
 * This function fetches the SAPI's env var represented by `name`, falling back
 * to the OS's env var if it's not found. The value is copied.
 * Returns an error result if neither source defines the env var, or if the
 * arena has insufficient capacity.
 *
 * Only call this if the tsrm_env_lock is held.
 */
static __attribute__((nonnull(1, 2, 3))) datadog_php_profiling_getenv_result
datadog_php_profiling_getenv(datadog_php_arena *arena,
                             const sapi_module_struct *sapi, const char *name) {
  /* Try the SAPI first. Note that sapi->getenv doesn't estrdup, it's the
   * helper function sapi_getenv that does that. If you use sapi->getenv
   * directly then you don't have to efree but you do need to copy the env
   * immediately, which we do when storing in the arena.
   */
  char *val =
      sapi->getenv ? sapi->getenv((getenv_char *)name, strlen(name)) : NULL;

  datadog_php_profiling_getenv_result result;
  if (val == NULL) {
    // fall back to the OS
    val = getenv(name);

    if (!val) {
      result.tag = DATADOG_PHP_PROFILING_GETENV_ERR;
      result.err = DATADOG_PHP_PROFILING_GETENV_ERR_NOVAL;
      return result;
    }
  }

  datadog_php_string_view view = datadog_php_string_view_from_cstr(val);

  const char *bytes = datadog_php_arena_alloc_str(arena, view.len, view.ptr);

  if (bytes) {
    result.tag = DATADOG_PHP_PROFILING_GETENV_OK;
    result.ok = (datadog_php_string_view){.ptr = bytes, .len = view.len};
  } else {
    result.tag = DATADOG_PHP_PROFILING_GETENV_ERR;
    result.err = DATADOG_PHP_PROFILING_GETENV_ERR_NOMEM;
  }

  return result;
}

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
      {"DD_PROFILING_LOG_LEVEL", &env->profiling_log_level},
      {"DD_SERVICE", &env->service},
      {"DD_TAGS", &env->tags},
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
    datadog_php_profiling_getenv_result result =
        datadog_php_profiling_getenv(arena, sapi, name);

    if (result.tag == DATADOG_PHP_PROFILING_GETENV_ERR) {
      if (result.err == DATADOG_PHP_PROFILING_GETENV_ERR_NOMEM) {
        break;
      } else if (result.err == DATADOG_PHP_PROFILING_GETENV_ERR_NOVAL) {
        // Default to empty string if there isn't a value.
        result.tag = DATADOG_PHP_PROFILING_GETENV_OK;
        result.ok = datadog_php_string_view_from_cstr("");
      }
    }

    envs[i].value->ptr = result.ok.ptr;
    envs[i].value->len = result.ok.len;
  }

  bool success = i == n;
  if (success) {
    /* There are two env vars for CPU time because I goofed :'(
     * Prefer DD_PROFILING_EXPERIMENTAL_CPU_TIME_ENABLED, because that's what
     * is in the documentation.
     */
    datadog_php_profiling_getenv_result result = datadog_php_profiling_getenv(
        arena, sapi, "DD_PROFILING_EXPERIMENTAL_CPU_TIME_ENABLED");

    ddprof_ffi_CharSlice cpu_enabled = {.ptr = "", .len = 0};
    if (result.tag == DATADOG_PHP_PROFILING_GETENV_ERR) {
      switch (result.err) {
      case DATADOG_PHP_PROFILING_GETENV_ERR_NOMEM:
        success = false;
        break;
      case DATADOG_PHP_PROFILING_GETENV_ERR_NOVAL:
        // fall back to undocumented version that has been around longer.
        result = datadog_php_profiling_getenv(
            arena, sapi, "DD_PROFILING_EXPERIMENTAL_CPU_ENABLED");
        if (result.tag == DATADOG_PHP_PROFILING_GETENV_ERR) {
          if (result.err == DATADOG_PHP_PROFILING_GETENV_ERR_NOVAL) {
            // fall back to empty string, consider it a success
          } else if (result.err == DATADOG_PHP_PROFILING_GETENV_ERR_NOMEM) {
            success = false;
          }
          break;
        }
        cpu_enabled.ptr = result.ok.ptr;
        cpu_enabled.len = result.ok.len;
        break;
      }
    } else {
      cpu_enabled.ptr = result.ok.ptr;
      cpu_enabled.len = result.ok.len;
    }

    env->profiling_experimental_cpu_enabled = cpu_enabled;
  }

#if PHP_VERSION_ID >= 70400
  tsrm_env_unlock();
#endif

  return success;
}
