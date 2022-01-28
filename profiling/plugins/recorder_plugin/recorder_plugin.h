#ifndef DATADOG_PHP_RECORDER_PLUGIN_H
#define DATADOG_PHP_RECORDER_PLUGIN_H

#include <Zend/zend_extensions.h>
#include <config/config.h>
#include <stack-collector/stack-collector.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

extern atomic_bool datadog_php_profiling_recorder_enabled;
extern bool datadog_php_profiling_cpu_time_enabled;

/* The recorder has two high level responsibilities:
 *  1. Aggregate samples.
 *  2. Export profiles once the configured period has elapsed.
 */

typedef struct datadog_php_record_values {
  uint64_t count;    // usually 0 or 1
  int64_t wall_time; // wall time in ns since last sample, may be 0
  int64_t cpu_time;  // cpu time in ns since last sample, may be 0
} datadog_php_record_values;

__attribute__((nonnull)) bool
datadog_php_recorder_plugin_record(datadog_php_record_values record_values,
                                   int64_t tid,
                                   const datadog_php_stack_sample *sample);

void datadog_php_recorder_plugin_first_activate(
    const datadog_php_profiling_config *config);
void datadog_php_recorder_plugin_shutdown(zend_extension *extension);

void datadog_php_recorder_plugin_diagnose(
    const datadog_php_profiling_config *config);

#endif // DATADOG_PHP_RECORDER_PLUGIN_H
