#include "datadog-profiling.h"
#include "php_datadog-profiling.h"

#include <Zend/zend.h>
#include <Zend/zend_portability.h>
#include <main/SAPI.h>
#include <php.h>
#include <stdbool.h>
#include <uv.h>

// must come after php.h
#include <ext/standard/html.h>
#include <ext/standard/info.h>
#include <ext/standard/php_random.h>

#include "components/log/log.h"
#include "components/sapi/sapi.h"
#include "components/string_view/string_view.h"
#include "config/config.h"
#include "context.h"
#include "env/env.h"
#include "plugins/log_plugin/log_plugin.h"
#include "plugins/recorder_plugin/recorder_plugin.h"
#include "plugins/stack_collector_plugin/stack_collector_plugin.h"

// These type names are long, let's shorten them up
typedef datadog_php_log_level log_level_t;
typedef datadog_php_sapi sapi_t;
typedef datadog_php_string_view string_view_t;

static uv_once_t first_activate_once = UV_ONCE_INIT;
static uint8_t profiling_env_storage[4096];
static datadog_php_profiling_env profiling_env;
static datadog_php_profiling_config profiling_config;
static datadog_php_uuid runtime_id = DATADOG_PHP_UUID_INIT;

static void datadog_info_print_esc_view(datadog_php_string_view str);
static void datadog_info_print_esc(const char *str);
static void datadog_info_print(const char *);

static void diagnose_endpoint(ddprof_ffi_EndpointV3 endpoint) {
  const char *col_a = "Profiling Agent Endpoint";
  if (endpoint.tag != DDPROF_FFI_ENDPOINT_V3_AGENT) {
    // todo: print agentless endpoint info
    datadog_profiling_info_diagnostics_row(col_a, "(agentless)");
    return;
  }

  string_view_t url = {
      .len = endpoint.agent.len,
      .ptr = (const char *)endpoint.agent.ptr,
  };

  if (sapi_module.phpinfo_as_text) {
    // Ensure it's null terminated for this API.
    zend_string *col_b = zend_string_init(url.ptr, url.len, 0);
    php_info_print_table_row(2, col_a, col_b->val);
    zend_string_release(col_b);
    return;
  }
  datadog_info_print("<tr><td class='e'>");
  datadog_info_print_esc(col_a);
  datadog_info_print("</td><td class='v'>");
  datadog_info_print_esc_view(url);
  datadog_info_print("</td></tr>\n");
}

static void
datadog_php_config_diagnose(const datadog_php_profiling_config *config) {
  const char *yes = "true", *no = "false";
  php_info_print_table_colspan_header(2, "Profiling Inferred Configuration");
  datadog_profiling_info_diagnostics_row("Profiling Enabled",
                                         config->profiling_enabled ? yes : no);
  datadog_profiling_info_diagnostics_row(
      "Experimental CPU Profiling Enabled",
      config->profiling_experimental_cpu_enabled ? yes : no);

  datadog_profiling_info_diagnostics_row(
      "Profiling Log Level",
      datadog_php_log_level_to_str(config->profiling_log_level));
  diagnose_endpoint(config->endpoint);

  datadog_profiling_info_diagnostics_row("Application's Environment (DD_ENV)",
                                         config->env.ptr);
  datadog_profiling_info_diagnostics_row("Application's Service (DD_SERVICE)",
                                         config->service.ptr);
  datadog_profiling_info_diagnostics_row("Application's Version (DD_VERSION)",
                                         config->version.ptr);
}

/**
 * Diagnose issues such as being unable to reach the agent.
 */
void datadog_profiling_diagnostics(void) {
  php_info_print_table_start();
  datadog_php_config_diagnose(&profiling_config);
  datadog_php_recorder_plugin_diagnose(&profiling_config);
  php_info_print_table_end();
}

ZEND_TLS bool datadog_profiling_enabled;

static void diagnose_profiling_enabled(bool enabled) {
  const char *string = NULL;

  if (enabled) {
    string = "[Datadog Profiling] Profiling is enabled.";
  } else {
    /* This message might not be logged. I've see-sawed back-and-forth on
     * whether *_ENABLED=off + *_LOG_LEVEL=info should actually do anything. On
     * one hand, the profiler is off and shouldn't do anything. On the other,
     * if the log level is set to something, reminding people that it's set to
     * off may be useful.
     */
    string = "[Datadog Profiling] Profiling is disabled.";
  }

  string_view_t message = {strlen(string), string};
  prof_logger.log(DATADOG_PHP_LOG_INFO, message);
}

static void sapi_diagnose(sapi_t sapi, datadog_php_string_view pretty_name) {

  switch (sapi) {
  case DATADOG_PHP_SAPI_APACHE2HANDLER:
  case DATADOG_PHP_SAPI_CLI:
  case DATADOG_PHP_SAPI_CLI_SERVER:
  case DATADOG_PHP_SAPI_CGI_FCGI:
  case DATADOG_PHP_SAPI_FPM_FCGI: {
    const char *msg = "[Datadog Profiling] Detected SAPI: ";
    string_view_t messages[3] = {
        {strlen(msg), msg},
        pretty_name,
        {1, "."},
    };
    log_level_t log_level = DATADOG_PHP_LOG_DEBUG;
    prof_logger.logv(log_level, 3, messages);
    break;
  }

  case DATADOG_PHP_SAPI_UNKNOWN:
  default: {
    const char *msg = "[Datadog Profiling] SAPI not detected: ";
    log_level_t log_level = DATADOG_PHP_LOG_WARN;
    string_view_t messages[3] = {
        {strlen(msg), msg},
        pretty_name,
        {1, "."},
    };
    prof_logger.logv(log_level, 3, messages);
  }
  }
}

int datadog_profiling_startup(zend_extension *extension) {
  datadog_php_stack_collector_startup(extension);

  return SUCCESS;
}

static void datadog_profiling_first_activate(void) {
  datadog_php_profiling_config_default_ctor(&profiling_config);
  datadog_php_arena *arena = datadog_php_arena_new(sizeof profiling_env_storage,
                                                   profiling_env_storage);
  if (arena &&
      datadog_php_profiling_getenvs(&profiling_env, &sapi_module, arena)) {
    datadog_php_profiling_config_ctor(&profiling_config, arena, &profiling_env);
  } else {
    /* Env vars can't be inspected since the above branch failed. There's no
     * way to know if the user wants logging. This is a pretty big failure,
     * so print an error message anyway.
     */
    fprintf(
        stderr,
        "[Datadog Profiling] Unable to load configuration. Profiling is disabled.");

    // This should already be false in this case, but let's be sure about it.
    profiling_config.profiling_enabled = false;
  }

  alignas(16) uint8_t data[16];
  if (php_random_bytes_silent(data, sizeof data) == SUCCESS) {
    datadog_php_uuidv4_bytes_ctor(&runtime_id, data);
  }

  datadog_profiling_enabled = profiling_config.profiling_enabled;

  datadog_php_log_plugin_first_activate(&profiling_config);

  // Logging plugin must be initialized before diagnosing things
  diagnose_profiling_enabled(datadog_profiling_enabled);

  datadog_php_string_view module =
      datadog_php_string_view_from_cstr(sapi_module.name);
  sapi_t sapi = datadog_php_sapi_from_name(module);
  sapi_diagnose(sapi,
                datadog_php_string_view_from_cstr(sapi_module.pretty_name));

  datadog_php_recorder_plugin_first_activate(&profiling_config);
  datadog_php_stack_collector_first_activate(&profiling_config);
}

void datadog_profiling_activate(void) {
  uv_once(&first_activate_once, datadog_profiling_first_activate);

  datadog_php_stack_collector_activate();
}

void datadog_profiling_deactivate(void) {
  datadog_php_stack_collector_deactivate();
}

void datadog_profiling_shutdown(zend_extension *extension) {
  datadog_php_recorder_plugin_shutdown(extension);
  datadog_php_log_plugin_shutdown(extension);
}

static void datadog_info_print_esc_view(datadog_php_string_view str) {
  zend_string *zstr = php_escape_html_entities((const unsigned char *)str.ptr,
                                               str.len, 0, ENT_QUOTES, "utf-8");
  (void)php_output_write(ZSTR_VAL(zstr), ZSTR_LEN(zstr));
  zend_string_release(zstr);
}

static void datadog_info_print_esc(const char *str) {
  datadog_info_print_esc_view(datadog_php_string_view_from_cstr(str));
}

static void datadog_info_print(const char *str) {
  (void)php_output_write(str, strlen(str));
}

void datadog_profiling_info_diagnostics_row(const char *col_a,
                                            const char *col_b) {
  if (sapi_module.phpinfo_as_text) {
    php_info_print_table_row(2, col_a, col_b);
    return;
  }
  datadog_info_print("<tr><td class='e'>");
  datadog_info_print_esc(col_a);
  datadog_info_print("</td><td class='v'>");
  datadog_info_print_esc(col_b);
  datadog_info_print("</td></tr>\n");
}

ZEND_API datadog_php_uuid datadog_profiling_runtime_id(void) {
  return runtime_id;
}

static struct ddtrace_profiling_context
datadog_profiling_get_profiling_context_null(void) {
  return (struct ddtrace_profiling_context){0, 0};
}

// Default to null implementation to cut down on the number of edges of caller.
struct ddtrace_profiling_context (*datadog_profiling_get_profiling_context)(
    void) = &datadog_profiling_get_profiling_context_null;

void datadog_profiling_message_handler(int message, void *arg) {
  if (UNEXPECTED(message != ZEND_EXTMSG_NEW_EXTENSION)) {
    // There are currently no other defined messages.
    return;
  }

  zend_extension *extension = (zend_extension *)arg;
  if (extension->name && strcmp(extension->name, "ddtrace") == 0) {
    DL_HANDLE handle = extension->handle;

    struct ddtrace_profiling_context (*get_profiling)(void) =
        DL_FETCH_SYMBOL(handle, "ddtrace_get_profiling_context");
    if (EXPECTED(get_profiling)) {
      datadog_profiling_get_profiling_context = get_profiling;
    }
  }
}

bool datadog_php_string_view_is_boolean_true(string_view_t str) {
  size_t len = str.len;
  if (len > 0 && len < 5) {
    const char *truthy[] = {"1", "on", "yes", "true"};
    // Conveniently, by pure luck len - 1 is the index for that string.
    return memcmp(str.ptr, truthy[len - 1], len) == 0;
  } else {
    return false;
  }
}
