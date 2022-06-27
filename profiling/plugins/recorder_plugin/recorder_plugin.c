#include "recorder_plugin.h"

#include <datadog-profiling.h>
#include <php_datadog-profiling.h>
#include <plugins/log_plugin/log_plugin.h>

#include <components/arena/arena.h>
#include <components/channel/channel.h>
#include <components/clocks/clocks.h>
#include <components/string_view/string_view.h>
#include <ddprof/ffi.h>
#include <php.h>
#include <stdlib.h>
#include <uv.h>

// must come after php.h
#include <ext/standard/info.h>

#define CHARSLICE_C(str) DDPROF_FFI_CHARSLICE_C(str)

atomic_bool datadog_php_profiling_recorder_enabled = false;
bool datadog_php_profiling_cpu_time_enabled = false;

/* thread_id will point to thread_id_v if the thread is created successfully;
 * null otherwise.
 */
static uv_thread_t thread_id_v, *thread_id = NULL;
static datadog_php_channel channel;
static ddprof_ffi_ProfileExporterV3 *exporter = NULL;
static const datadog_php_profiling_config *global_config = NULL;

typedef struct record_msg_s record_msg;

/**
 * The record_msg_s struct is what is passed over the recorder's channel.
 * Choose the channel's capacity based on the size of this message to limit the
 * amount of memory in the event that the channel is full.
 */
struct record_msg_s {
  datadog_php_record_values record_values;
  int64_t thread_id;
  ddtrace_profiling_context context;
  datadog_php_stack_sample sample;
};

_Static_assert(sizeof(record_msg) > 7168 && sizeof(record_msg) <= 8192,
               "size of record_msg needs to nicely fit in 8KiB");

/* CHANNEL_CAPACITY * sizeof(record_msg) = approx max memory used by channel
 *              256 *              8 KiB = 2048 KiB, or 2 MiB
 * At 1 sample per 10 milliseconds, that's 2.56 seconds worth of data that can
 * be kept in the channel at one time.
 */
static const uint16_t CHANNEL_CAPACITY = UINT16_C(256);

/* Currently, the recorder thread will be blocked while it is uploading a
 * profile so it cannot pull items out of the channel. Balance the timeout with
 * this in mind, but also realize that network requests may take a while.
 * TODO: move uploading to another thread.
 */
static const uint64_t UPLOAD_TIMEOUT_MS = 10000;

__attribute__((nonnull)) bool datadog_php_recorder_plugin_record(
    datadog_php_record_values record_values, int64_t tid,
    const datadog_php_stack_sample *sample, ddtrace_profiling_context context) {
  if (!datadog_php_profiling_recorder_enabled) {
    const char *str =
        "[Datadog Profiling] Sample dropped because profiling has been disabled.";
    datadog_php_string_view msg = {strlen(str), str};
    prof_logger.log(DATADOG_PHP_LOG_WARN, msg);
    return false;
  }

  /* todo: will it be better if we hoist the record_msg allocation to the caller
   *       and make this function only a thin, type-safe wrapper around the
   *       channel?
   */
  record_msg *message = malloc(sizeof(record_msg));
  if (message) {
    message->record_values = record_values;
    message->sample = *sample;
    message->thread_id = tid;
    message->context = context;

    bool success = channel.sender.send(&channel.sender, message);
    if (!success) {
      // todo: is this too noisy even for debug?
      const char *str =
          "[Datadog Profiling] Failed to store sample for aggregation; queue is likely full or closed.\n";
      datadog_php_string_view msg = {strlen(str), str};
      prof_logger.log(DATADOG_PHP_LOG_DEBUG, msg);
      free(message);
    }
    return success;
  }
  return false;
}

typedef struct instant_s instant;
struct instant_s {
  // private:
  uint64_t started_at_nanos;
};

static uint64_t instant_elapsed(instant self) {
  return (uv_hrtime() - self.started_at_nanos);
}

static instant instant_now(void) {
  instant now = {.started_at_nanos = uv_hrtime()};
  return now;
}

static bool ddprof_ffi_export(datadog_php_static_logger *logger,
                              const struct ddprof_ffi_Profile *profile,
                              uint64_t timeout_ms) {
  ddprof_ffi_SerializeResult serialize_result =
      ddprof_ffi_Profile_serialize(profile);
  if (serialize_result.tag == DDPROF_FFI_SERIALIZE_RESULT_ERR) {
    logger->log_cstr(DATADOG_PHP_LOG_WARN,
                     "[Datadog Profiling] Failed to serialize profile.");
    ddprof_ffi_SerializeResult_drop(serialize_result);
    return false;
  }

  struct ddprof_ffi_EncodedProfile *encoded_profile = &serialize_result.ok;

  ddprof_ffi_Timespec start = encoded_profile->start;
  ddprof_ffi_Timespec end = encoded_profile->end;

  ddprof_ffi_File files_[] = {{
      .name = CHARSLICE_C("profile.pprof"),
      .file = ddprof_ffi_Vec_u8_as_slice(&encoded_profile->buffer),
  }};

  struct ddprof_ffi_Slice_file files = {
      .ptr = files_,
      .len = sizeof files_ / sizeof *files_,
  };
  // needs to outlive tags
  char runtime_val[37] = {0};

  ddprof_ffi_Vec_tag tags = ddprof_ffi_Vec_tag_new();
  {
    datadog_php_uuid runtime_id = datadog_profiling_runtime_id();
    if (!datadog_php_uuid_is_nil(runtime_id)) {
      datadog_php_uuid_encode36(runtime_id, runtime_val);

      ddprof_ffi_CharSlice val = {.ptr = runtime_val,
                                  .len = strlen(runtime_val)};
      struct ddprof_ffi_PushTagResult result =
          ddprof_ffi_Vec_tag_push(&tags, CHARSLICE_C("runtime-id"), val);

      if (result.tag == DDPROF_FFI_PUSH_TAG_RESULT_ERR) {
        datadog_php_string_view message = {
            .len = result.err.len,
            .ptr = (const char *)result.err.ptr,
        };
        logger->log(DATADOG_PHP_LOG_WARN, message);
      }

      ddprof_ffi_PushTagResult_drop(result);
    }
  }

  /* The SAPI tag can be helpful as a filter. It also would have been useful
   * in the past to diagnose why we had so many empty profiles. Were these
   * short-lived CLI jobs on cron? Over-provisioned idle workers? The SAPI
   * would have at least given us a hint.
   */
  if (sapi_module.name != NULL) {
    ddprof_ffi_CharSlice val = {.ptr = sapi_module.name,
                                .len = strlen(sapi_module.name)};
    struct ddprof_ffi_PushTagResult result =
        ddprof_ffi_Vec_tag_push(&tags, CHARSLICE_C("php.sapi"), val);

    if (result.tag == DDPROF_FFI_PUSH_TAG_RESULT_ERR) {
      datadog_php_string_view message = {
          .len = result.err.len,
          .ptr = (const char *)result.err.ptr,
      };
      logger->log(DATADOG_PHP_LOG_WARN, message);
    }
    ddprof_ffi_PushTagResult_drop(result);
  }

  ddprof_ffi_Request *request = ddprof_ffi_ProfileExporterV3_build(
      exporter, start, end, files, &tags, timeout_ms);
  ddprof_ffi_Vec_tag_drop(tags);

  bool succeeded = false;
  if (request) {
    struct ddprof_ffi_SendResult result =
        ddprof_ffi_ProfileExporterV3_send(exporter, request, NULL);

    if (result.tag == DDPROF_FFI_SEND_RESULT_FAILURE) {
      datadog_php_string_view messages[2] = {
          datadog_php_string_view_from_cstr(
              "[Datadog Profiling] Failed to upload profile: "),
          {result.failure.len, (const char *)result.failure.ptr},
      };
      logger->logv(DATADOG_PHP_LOG_WARN, 2, messages);
    } else if (result.tag == DDPROF_FFI_SEND_RESULT_HTTP_RESPONSE) {
      uint16_t code = result.http_response.code;
      if (200 <= code && code < 300) {
        logger->log_cstr(DATADOG_PHP_LOG_INFO,
                         "[Datadog Profiling] Successfully uploaded profile.");
        succeeded = true;
      } else {
        char code_string[8] = {'u', 'n', 'k', 'n', 'o', 'w', 'n', '\0'};
        (void)snprintf(code_string, sizeof code_string, "%" PRIu16, code);
        datadog_php_string_view messages[2] = {
            datadog_php_string_view_from_cstr(
                "[Datadog Profiling] Unexpected HTTP status code when sending profile: "),
            {strlen(code_string), code_string},
        };
        datadog_php_log_level log_level =
            code >= 400 ? DATADOG_PHP_LOG_ERROR : DATADOG_PHP_LOG_WARN;
        logger->logv(log_level, 2, messages);
      }
    }

    ddprof_ffi_SendResult_drop(result);
  } else {
    logger->log_cstr(DATADOG_PHP_LOG_WARN,
                     "[Datadog Profiling] Failed to create HTTP request.");
  }

  ddprof_ffi_SerializeResult_drop(serialize_result);
  return succeeded;
}

/**
 * A frame is empty if it has neither a file name nor a function name.
 */
static bool is_empty_frame(datadog_php_stack_sample_frame *frame) {
  return (frame->function.len | frame->file.len) == 0;
}

// convert the id to a string, not a num!
static struct ddprof_ffi_Slice_c_char label_u64(char buffer[static 20],
                                                uint64_t id) {
  char tmp[21] = {0};
  struct ddprof_ffi_Slice_c_char val = {.ptr = "", .len = 0};
  int size = snprintf(tmp, sizeof tmp, "%" PRIu64, id);
  if (size > 0 && size < sizeof tmp) {
    memcpy(buffer, tmp, size);
    val = (struct ddprof_ffi_Slice_c_char){buffer, size};
  }
  return val;
}

// convert the id to a string, not a num!
static struct ddprof_ffi_Slice_c_char label_i64(char buffer[static 21],
                                                int64_t id) {
  char tmp[22] = {0};
  struct ddprof_ffi_Slice_c_char val = {.ptr = "", .len = 0};
  int size = snprintf(tmp, sizeof tmp, "%" PRId64, id);
  if (size > 0 && size < sizeof tmp) {
    memcpy(buffer, tmp, size);
    val = (struct ddprof_ffi_Slice_c_char){buffer, size};
  }
  return val;
}

static void datadog_php_recorder_add(struct ddprof_ffi_Profile *profile,
                                     record_msg *message) {
  uint32_t locations_capacity = message->sample.depth;
  struct ddprof_ffi_Location *locations =
      calloc(locations_capacity, sizeof(struct ddprof_ffi_Location));
  if (!locations) {
    prof_logger.log_cstr(
        DATADOG_PHP_LOG_WARN,
        "[Datadog Profiling] Failed to allocate storage for sample locations.");
    return;
  }

  // There is one line per location, at least as long as PHP doesn't inline
  struct ddprof_ffi_Line *lines =
      calloc(locations_capacity, sizeof(struct ddprof_ffi_Line));
  if (!lines) {
    prof_logger.log_cstr(
        DATADOG_PHP_LOG_WARN,
        "[Datadog Profiling] Failed to allocate storage for sample lines.");
    goto free_locations;
  }

  uint16_t locations_size = 0;
  datadog_php_stack_sample_iterator iterator;
  for (iterator = datadog_php_stack_sample_iterator_ctor(&message->sample);
       datadog_php_stack_sample_iterator_valid(&iterator);
       datadog_php_stack_sample_iterator_next(&iterator)) {
    datadog_php_stack_sample_frame frame =
        datadog_php_stack_sample_iterator_frame(&iterator);

    if (is_empty_frame(&frame)) {
      continue;
    }

    struct ddprof_ffi_Line *line = lines + locations_size;
    struct ddprof_ffi_Function function = {
        .name = {.ptr = frame.function.ptr, .len = frame.function.len},
        .filename = {.ptr = frame.file.ptr, .len = frame.file.len},
    };
    line->function = function;
    line->line = frame.lineno;

    struct ddprof_ffi_Location location = {
        /* Yes, we use an empty mapping! We don't map to a .so or anything
         * remotely like it, so we do not pretend.
         */
        .mapping = {},
        .lines = {.ptr = line, .len = 1},
        .is_folded = false,
    };
    locations[locations_size++] = location;
  }
  datadog_php_stack_sample_iterator_dtor(&iterator);

  int64_t values_storage[3] = {
      (int64_t)message->record_values.count,
      message->record_values.wall_time,
      message->record_values.cpu_time,
  };
  size_t values_storage_len = datadog_php_profiling_cpu_time_enabled ? 3 : 2;
  struct ddprof_ffi_Slice_i64 values = {.ptr = values_storage,
                                        .len = values_storage_len};

  char thread_id_str[24] = "";
  struct ddprof_ffi_Slice_c_char thread_id_slice =
      label_i64(thread_id_str, message->thread_id);

  char local_root_span_id_str[24] = "";
  struct ddprof_ffi_Slice_c_char local_root_span_id =
      label_u64(local_root_span_id_str, message->context.local_root_span_id);

  char span_id_str[24] = "";
  struct ddprof_ffi_Slice_c_char span_id =
      label_u64(span_id_str, message->context.span_id);

  ddprof_ffi_Label labels[] = {
      {.key = {ZEND_STRL("thread id")}, .str = thread_id_slice},
      {.key = {ZEND_STRL("local root span id")}, .str = local_root_span_id},
      {.key = {ZEND_STRL("span id")}, .str = span_id},
  };

  size_t n_labels = sizeof labels / sizeof labels[0];
  // seems something failed
  if (span_id.len == 0 || local_root_span_id.len == 0) {
    n_labels -= 2;
  }

  struct ddprof_ffi_Sample sample = {
      .values = values,
      .locations = {.ptr = locations, .len = locations_size},
      .labels = {.ptr = labels, .len = n_labels},
  };

  ddprof_ffi_Profile_add(profile, sample);

  free(lines);
free_locations:
  free(locations);
}

static const struct ddprof_ffi_Period period = {
    .type_ =
        {
            .type_ = CHARSLICE_C("wall-time"),
            .unit = CHARSLICE_C("nanoseconds"),
        },

    /* An interval of 60 seconds often ends up with an HTTP 502 Bad Gateway
     * every 2nd request. I have not investigated this, but I suspect that it
     * has to do with some timeout set to 60 seconds and hasn't healed yet.
     *
     * This only occurs when going through the agent, not directly to intake.
     *
     * So, I did the sensible thing of picking a prime number close to 60
     * seconds. The choices 59 and 61 seems like they might be _too_ close, so
     * I went with 67 seconds.
     */
    .value = 67000000000,
};

static struct ddprof_ffi_Profile *profile_new(void) {

  /* Some tools assume the last value type is the "primary" one, so put
   * cpu-time last, as that's what the Datadog UI will default to (once it is
   * released).
   */
  static struct ddprof_ffi_ValueType value_types[3] = {
      {
          .type_ = CHARSLICE_C("sample"),
          .unit = CHARSLICE_C("count"),
      },
      {
          .type_ = CHARSLICE_C("wall-time"),
          .unit = CHARSLICE_C("nanoseconds"),
      },
      {
          .type_ = CHARSLICE_C("cpu-time"),
          .unit = CHARSLICE_C("nanoseconds"),
      },
  };

  /* Note that the maximum memory used by the profile can be estimated with
   * decent accuracy by using the period, sample frequency, maximum payload size
   * of each sample, and the channel's capacity.
   * HOWEVER, this may not hold true for future profiles, such as garbage
   * collection profiling, so be cautious about that when adding them.
   */
  struct ddprof_ffi_Slice_value_type sample_types = {
      .ptr = value_types,
      .len = datadog_php_profiling_cpu_time_enabled ? 3 : 2,
  };
  return ddprof_ffi_Profile_new(sample_types, &period);
}

void datadog_php_recorder_plugin_main(void) {
  if (period.value < 0) {
    // widest i64 is -9223372036854775808 (20 chars)
    char buffer[24] = {'(', 'u', 'n', 'k', 'n', 'o', 'w', 'n', ')', '\0'};

    (void)snprintf(buffer, sizeof buffer, "%" PRId64, period.value);

    datadog_php_string_view messages[] = {
        datadog_php_string_view_from_cstr(
            "[Datadog Profiling] Failed to start; invalid upload period of "),
        {strlen(buffer), buffer},
        datadog_php_string_view_from_cstr("."),
    };
    size_t n_messages = sizeof messages / sizeof *messages;
    prof_logger.logv(DATADOG_PHP_LOG_ERROR, n_messages, messages);
    return;
  }

  const uint64_t period_val = (uint64_t)period.value;
  datadog_php_receiver *receiver = &channel.receiver;
  struct ddprof_ffi_Profile *profile = profile_new();
  if (!profile) {
    const char *msg =
        "[Datadog Profiling] Failed to create profile. Samples will not be collected.";
    prof_logger.log_cstr(DATADOG_PHP_LOG_ERROR, msg);
    return;
  } else {
    const char *msg = "[Datadog Profiling] Recorder online.";
    prof_logger.log_cstr(DATADOG_PHP_LOG_DEBUG, msg);
  }

  while (datadog_php_profiling_recorder_enabled) {
    uint64_t sample_count = 0;
    uint64_t sleep_for_nanos = period_val;
    instant before = instant_now();
    do {
      record_msg *message;
      if (receiver->recv(receiver, (void **)&message, sleep_for_nanos)) {
        // an empty message can be sent, such as when we're shutting down
        if (message) {
          datadog_php_recorder_add(profile, message);
          free(message);
          ++sample_count;
        }
      }
      uint64_t duration = instant_elapsed(before);
      sleep_for_nanos = duration < period_val ? period_val - duration : 0;
      // protect against underflow
    } while (datadog_php_profiling_recorder_enabled && sleep_for_nanos);

    /* If no samples have been collected, then don't report the profile. Some
     * customers are having millions of profiles per hour, most of which are
     * empty. At the moment this is just a guess, but I suspect these are
     * short-lived CLI invocations that are running on a cron close to every
     * second. When multiplied by many hosts, it quickly escalates too high.
     *
     * Aside from the cost, the aggregation feature doesn't work well because
     * the profiles of interest aren't being chosen, so it essentially shows
     * no data, despite there being data.
     */
    if (sample_count) {
      ddprof_ffi_export(&prof_logger, profile, UPLOAD_TIMEOUT_MS);
    } else {
      const char *msg = "[Datadog Profiling] No profiles to upload.";
      prof_logger.log_cstr(DATADOG_PHP_LOG_INFO, msg);
    }
    (void)ddprof_ffi_Profile_reset(profile);
  }

  ddprof_ffi_Profile_free(profile);
  receiver->dtor(receiver);
}

void datadog_php_recorder_plugin_shutdown(zend_extension *extension) {
  (void)extension;

  if (!datadog_php_profiling_recorder_enabled)
    return;

  // Disable the plugin before sending as that flag's checked by the receiver.
  datadog_php_profiling_recorder_enabled = false;

  // Send an empty message to wake receiver up.
  channel.sender.send(&channel.sender, NULL);

  // Must clean up channel sender before thread join, or it will deadlock.
  channel.sender.dtor(&channel.sender);

  if (thread_id && uv_thread_join(thread_id)) {
    const char *str = "[Datadog Profiling] Recorder thread failed to join.";
    datadog_php_string_view message = {strlen(str), str};
    prof_logger.log(DATADOG_PHP_LOG_WARN, message);
  } else {
    const char *str = "[Datadog Profiling] Recorder offline.";
    datadog_php_string_view message = {strlen(str), str};
    prof_logger.log(DATADOG_PHP_LOG_INFO, message);
  }

  ddprof_ffi_ProfileExporterV3_delete(exporter);
}

#define SV(literal)                                                            \
  (datadog_php_string_view) { sizeof(literal) - 1, literal }

static bool recorder_first_activate_helper() {
  const datadog_php_profiling_config *config = global_config;
  bool success = datadog_php_channel_ctor(&channel, CHANNEL_CAPACITY);
  if (!success) {
    return false;
  }

  datadog_php_profiling_cpu_time_enabled =
      config->profiling_experimental_cpu_enabled;

  ddprof_ffi_CharSlice family = CHARSLICE_C("php");
  const ddprof_ffi_Vec_tag *tags = &config->tags.tags;
  struct ddprof_ffi_NewProfileExporterV3Result exporter_result =
      ddprof_ffi_ProfileExporterV3_new(family, tags, config->endpoint);

  if (exporter_result.tag == DDPROF_FFI_NEW_PROFILE_EXPORTER_V3_RESULT_ERR) {
    const char *str =
        "[Datadog Profiling] Failed to start; could not create HTTP uploader: ";
    datadog_php_string_view messages[] = {
        {strlen(str), str},
        {exporter_result.err.len, (const char *)exporter_result.err.ptr},
    };
    prof_logger.logv(DATADOG_PHP_LOG_ERROR, sizeof messages / sizeof *messages,
                     messages);
    ddprof_ffi_NewProfileExporterV3Result_drop(exporter_result);
    return false;
  }

  exporter = exporter_result.ok;

  thread_id = &thread_id_v;
  int result = uv_thread_create(
      thread_id, (uv_thread_cb)datadog_php_recorder_plugin_main, NULL);
  if (result != 0) {
    thread_id = NULL;
    ddprof_ffi_ProfileExporterV3_delete(exporter);
    channel.receiver.dtor(&channel.receiver);
    channel.sender.dtor(&channel.sender);

    const char *str =
        "[Datadog Profiling] Failed to start; could not create thread for aggregating profiles.";
    datadog_php_string_view msg = {strlen(str), str};
    prof_logger.log(DATADOG_PHP_LOG_ERROR, msg);
    return false;
  }
  return true;
}

void datadog_php_recorder_plugin_first_activate(
    const datadog_php_profiling_config *config) {
  global_config = config;
  datadog_php_profiling_recorder_enabled =
      config->profiling_enabled && recorder_first_activate_helper();
}

static int64_t
upload_logv(datadog_php_log_level level, size_t n_messages,
            datadog_php_string_view messages[static n_messages]) {

  const char *key;
  switch (level) {
  default:
  case DATADOG_PHP_LOG_UNKNOWN:
    key = "unknown"; // shouldn't happen at this location
    break;
  case DATADOG_PHP_LOG_OFF:
    key = "off"; // shouldn't happen at this location
    break;
  case DATADOG_PHP_LOG_ERROR:
    key = "error";
    break;
  case DATADOG_PHP_LOG_WARN:
    key = "warn";
    break;
  case DATADOG_PHP_LOG_INFO:
    key = "info";
    break;
  case DATADOG_PHP_LOG_DEBUG:
    key = "debug";
    break;
  }

  size_t bytes = 2; // trailing newline and null byte
  for (size_t i = 0; i != n_messages; ++i) {
    bytes += messages[i].len;
  }

  char *message = malloc(bytes);
  if (message) {
    size_t offset = 0;
    for (size_t i = 0; i != n_messages; ++i) {
      memcpy(message + offset, messages[i].ptr, messages[i].len);
      offset += messages[i].len;
    }
    message[offset] = '\n';
    message[offset + 1] = '\0';
  }

  datadog_profiling_info_diagnostics_row(
      key, message ? message : "(error formatting messages)");

  free(message);

  return 0; // not intended to be checked
}

static void upload_log(datadog_php_log_level level,
                       datadog_php_string_view message) {
  (void)upload_logv(level, 1, &message);
}

static void upload_log_cstr(datadog_php_log_level level, const char *cstr) {
  upload_log(level, datadog_php_string_view_from_cstr(cstr));
}

static void
datadog_php_recorder_collect(const datadog_php_profiling_config *config,
                             struct ddprof_ffi_Profile *profile) {
  record_msg message = {
      .record_values = {1, 0, 0},
      .context = datadog_profiling_get_profiling_context(),
      .thread_id = (int64_t)uv_thread_self(),
  };

  uint64_t wall_before = uv_hrtime();
  datadog_php_cpu_time_result cpu_before = datadog_php_cpu_time_now();
  datadog_php_stack_collect(EG(current_execute_data), &message.sample);
  datadog_php_cpu_time_result cpu_after = datadog_php_cpu_time_now();
  uint64_t wall_after = uv_hrtime();

  if (config->profiling_experimental_cpu_enabled &&
      cpu_before.tag == DATADOG_PHP_CPU_TIME_OK &&
      cpu_after.tag == DATADOG_PHP_CPU_TIME_OK) {
    struct timespec then = cpu_before.ok, now = cpu_after.ok;
    int64_t current = now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
    int64_t prev = then.tv_sec * INT64_C(1000000000) + then.tv_nsec;
    message.record_values.cpu_time = current - prev;
  }

  message.record_values.wall_time = (int64_t)(wall_after - wall_before);

  datadog_php_recorder_add(profile, &message);
}

void datadog_php_recorder_plugin_diagnose(
    const datadog_php_profiling_config *config) {
  const char *yes = "true", *no = "false";

  php_info_print_table_colspan_header(2, "Profiling Recorder Diagnostics");

  datadog_profiling_info_diagnostics_row(
      "Enabled", datadog_php_profiling_recorder_enabled ? yes : no);

  struct ddprof_ffi_Profile *profile = profile_new();
  datadog_profiling_info_diagnostics_row("Can create profiles",
                                         profile ? yes : no);
  if (datadog_php_profiling_recorder_enabled && profile) {
    datadog_php_static_logger logger = {
        .log = upload_log,
        .logv = upload_logv,
        .log_cstr = upload_log_cstr,
    };

    datadog_php_recorder_collect(config, profile);

    php_info_print_table_colspan_header(2, "Profiling Upload Diagnostics");
    bool uploaded = ddprof_ffi_export(&logger, profile, UPLOAD_TIMEOUT_MS);
    datadog_profiling_info_diagnostics_row("Can upload profiles",
                                           uploaded ? yes : no);
  }

  ddprof_ffi_Profile_free(profile);
}
