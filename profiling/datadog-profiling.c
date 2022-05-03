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
#include "once/once.h"
#include "plugins/log_plugin/log_plugin.h"
#include "plugins/recorder_plugin/recorder_plugin.h"
#include "plugins/stack_collector_plugin/stack_collector_plugin.h"

// These names are long, let's shorten them up.
typedef datadog_php_log_level log_level_t;
typedef datadog_php_sapi sapi_t;
typedef datadog_php_string_view string_view_t;
#define CHARSLICE_C(str) DDPROF_FFI_CHARSLICE_C(str)

/* # Important Notes on Static Storage Initialization
 * It's fine to initialize values in static storage e.g.
 *     static int fd = -1;
 *
 * HOWEVER, they must be re-initialized in minit or startup, e.g.
 *     int datadog_profiling_startup(zend_extension *extension) {
 *         fd = -1;
 *         // ...
 *     }
 *
 * This is because a SAPI may call minit more than once, such as when Apache
 * does a reload: it will call mshutdown and re-run the initialization process
 * without destroying the process, causing minit to be run again BUT static
 * storage initializers would not be rerun!
 */
static struct datadog_php_once_s first_activate_once;
static uint8_t profiling_env_storage[4096];
static datadog_php_profiling_env profiling_env;
static datadog_php_profiling_config profiling_config;
static datadog_php_uuid runtime_id;

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
  php_info_print_table_colspan_header(
      2, "Datadog Profiling Inferred Configuration");
  datadog_profiling_info_diagnostics_row("Version",
                                         PHP_DATADOG_PROFILING_VERSION);
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

zend_result datadog_profiling_startup(zend_extension *extension) {
  // Re-initialize static variables (Apache reload may have occurred)
  int once_ctor_result = datadog_php_once_ctor(&first_activate_once);
  if (once_ctor_result != 0) {
    const char *error = uv_strerror(once_ctor_result);
    zend_error(
        E_WARNING,
        "Datadog Profiling failed to startup; error during datadog_php_once_ctor: %s",
        error);
    return FAILURE;
  }

  memset(profiling_env_storage, 0, sizeof profiling_env_storage);
  datadog_php_profiling_env_default_ctor(&profiling_env);
  datadog_php_profiling_config_default_ctor(&profiling_config);
  datadog_php_uuid_default_ctor(&runtime_id);

  datadog_php_stack_collector_startup(extension);

  return SUCCESS;
}

static void
datadog_php_profiling_enrich_tags(datadog_php_profiling_config *config) {
  // Diagnose configured tags.
  if (config->tags.error_message) {
    struct ddprof_ffi_Vec_u8 *message = config->tags.error_message;
    datadog_php_string_view messages[] = {
        DATADOG_PHP_STRING_VIEW_LITERAL("[Datadog Profiling] "),
        {.ptr = (const char *)message->ptr, .len = message->len},
    };
    size_t n_messages = sizeof messages / sizeof messages[0];
    prof_logger.logv(DATADOG_PHP_LOG_WARN, n_messages, messages);
  }

  // Add static tags and ones based on configuration.
  struct tag {
    ddprof_ffi_CharSlice key, value;
  } tags_[] = {
      {CHARSLICE_C("language"), CHARSLICE_C("php")},
      {CHARSLICE_C("profiler_version"),
       {
           .ptr = PHP_DATADOG_PROFILING_VERSION,
           .len = strlen(PHP_DATADOG_PROFILING_VERSION),
       }},
      {CHARSLICE_C("service"), config->service},
      {CHARSLICE_C("env"), config->env},
      {CHARSLICE_C("version"), config->version},
  };

  for (unsigned i = 0; i != sizeof tags_ / sizeof tags_[0]; ++i) {
    struct tag *tag = &tags_[i];

    /* Tags like service and version come from env vars which might not be set,
     * so skip them if we don't have a value.
     */
    if (tag->value.len == 0 || !tag->value.ptr) {
      datadog_php_string_view messages[] = {
          DATADOG_PHP_STRING_VIEW_LITERAL(
              "[Datadog Profiling] Tag had no value: "),
          {.len = tag->key.len, .ptr = tag->key.ptr},
      };
      size_t n_messages = sizeof messages / sizeof *messages;
      prof_logger.logv(DATADOG_PHP_LOG_DEBUG, n_messages, messages);
      continue;
    }

    struct ddprof_ffi_PushTagResult result =
        ddprof_ffi_Vec_tag_push(&config->tags.tags, tag->key, tag->value);

    if (result.tag == DDPROF_FFI_PUSH_TAG_RESULT_ERR) {
      // All of our own tags should be valid.
      datadog_php_string_view messages[] = {
          DATADOG_PHP_STRING_VIEW_LITERAL(
              "[Datadog Profiling] Internal configuration error: "),
          {
              .len = result.err.len,
              .ptr = (const char *)result.err.ptr,
          },
      };
      size_t n_messages = sizeof messages / sizeof messages[0];
      prof_logger.logv(DATADOG_PHP_LOG_WARN, n_messages, messages);
    }
    ddprof_ffi_PushTagResult_drop(result);
  }
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

  datadog_profiling_enabled = profiling_config.profiling_enabled;

  alignas(16) uint8_t data[16];
  if (datadog_profiling_enabled &&
      php_random_bytes_silent(data, sizeof data) == SUCCESS) {
    datadog_php_uuidv4_bytes_ctor(&runtime_id, data);
  }

  datadog_php_log_plugin_first_activate(&profiling_config);

  // Logging plugin must be initialized before diagnosing things
  diagnose_profiling_enabled(datadog_profiling_enabled);

  // Diagnose DD_TAGS, add some static tags, and some from config.
  datadog_php_profiling_enrich_tags(&profiling_config);

  datadog_php_string_view module =
      datadog_php_string_view_from_cstr(sapi_module.name);
  sapi_t sapi = datadog_php_sapi_from_name(module);
  sapi_diagnose(sapi,
                datadog_php_string_view_from_cstr(sapi_module.pretty_name));

  datadog_php_recorder_plugin_first_activate(&profiling_config);
  datadog_php_stack_collector_first_activate(&profiling_config);
}

void datadog_profiling_activate(void) {
  datadog_php_once(&first_activate_once, datadog_profiling_first_activate);
  datadog_php_stack_collector_activate();
}

void datadog_profiling_deactivate(void) {
  datadog_php_stack_collector_deactivate();
}

void datadog_profiling_shutdown(zend_extension *extension) {
  datadog_php_once_dtor(&first_activate_once);
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
