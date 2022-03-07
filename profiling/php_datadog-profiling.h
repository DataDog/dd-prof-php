#ifndef PHP_DATADOG_PROFILING_H
#define PHP_DATADOG_PROFILING_H

#include <php_config.h>

#include <Zend/zend_extensions.h>
#include <Zend/zend_portability.h>
#include <components/uuid/uuid.h>

#define PHP_DATADOG_PROFILING_VERSION "0.5.0"

int datadog_profiling_startup(zend_extension *);
void datadog_profiling_activate(void);
void datadog_profiling_deactivate(void);
void datadog_profiling_shutdown(zend_extension *);
void datadog_profiling_diagnostics(void);
void datadog_profiling_message_handler(int message, void *arg);

BEGIN_EXTERN_C()
ZEND_API void datadog_profiling_interrupt_function(struct _zend_execute_data *);
ZEND_API datadog_php_uuid datadog_profiling_runtime_id(void);
END_EXTERN_C()

#if defined(ZTS)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif // PHP_DATADOG_PROFILING_H
