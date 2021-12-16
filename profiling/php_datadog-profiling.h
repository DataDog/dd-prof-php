#ifndef PHP_DATADOG_PROFILING_H
#define PHP_DATADOG_PROFILING_H

#include <php_config.h>

#include <Zend/zend_extensions.h>

#define PHP_DATADOG_PROFILING_VERSION "0.3.0"

int datadog_profiling_startup(zend_extension *);
void datadog_profiling_activate(void);
void datadog_profiling_deactivate(void);
void datadog_profiling_shutdown(zend_extension *);
void datadog_profiling_diagnostics(void);

#if defined(ZTS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif // PHP_DATADOG_PROFILING_H
