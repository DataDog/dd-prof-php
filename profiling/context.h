#ifndef DATADOG_PROFILING_CONTEXT_H
#define DATADOG_PROFILING_CONTEXT_H

#include <stdint.h>

// Keep in sync with tracer's version (but they are different).

struct ddtrace_profiling_context {
  uint64_t local_root_span_id, span_id;
};
typedef struct ddtrace_profiling_context ddtrace_profiling_context;

/**
 * Provide the active trace information for the profiler.
 * If there isn’t an active context, return 0 for both values.
 * This needs to be safe to call even if tracing is disabled, but only needs
 * to support being called from a PHP thread.
 */
extern ddtrace_profiling_context (*datadog_profiling_get_profiling_context)(
    void);

#endif // DATADOG_PROFILING_CONTEXT_H
