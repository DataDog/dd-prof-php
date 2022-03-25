#ifndef DATADOG_PHP_PROFILING_H
#define DATADOG_PHP_PROFILING_H

#include <components/string_view/string_view.h>
#include <stdbool.h>

void datadog_profiling_info_diagnostics_row(const char *col_a,
                                            const char *col_b);

#endif // DATADOG_PHP_PROFILING_H
