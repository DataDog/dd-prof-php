#include "profiling/php_datadog-profiling.h"

#include <php.h>

ZEND_COLD zend_result
datadog_profiling_hybrid_startup(zend_extension *extension);

/* This is used by the engine to ensure that the extension is built against
 * the same version as the engine. It must be named `extension_version_info`.
 */
ZEND_API zend_extension_version_info extension_version_info = {
    .zend_extension_api_no = ZEND_EXTENSION_API_NO,
    .build_id = ZEND_EXTENSION_BUILD_ID,
};

/* This must be named `zend_extension_entry` for the engine to know this is a
 * zend extension.
 */
ZEND_API zend_extension zend_extension_entry = {
    .name = "datadog-profiling",
    .version = PHP_DATADOG_PROFILING_VERSION,
    .author = "Datadog",
    .URL = "https://github.com/DataDog/dd-prof-php",
    .copyright = "Copyright Datadog",
    .startup = datadog_profiling_hybrid_startup,
    .activate = datadog_profiling_activate,
    .deactivate = datadog_profiling_deactivate,
    .shutdown = datadog_profiling_shutdown,
    .message_handler = datadog_profiling_message_handler,
    .resource_number = -1,
};

PHP_MINFO_FUNCTION(datadog_profiling) {
  (void)zend_module;

  datadog_profiling_diagnostics();
}

/* Make this a hybrid zendextension-module, which gives us access to the minfo
 * hook, so we can print diagnostics.
 */
static zend_module_entry datadog_profiling_module_entry = {
    STANDARD_MODULE_HEADER,
    "datadog-profiling",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    PHP_MINFO(datadog_profiling),
    PHP_DATADOG_PROFILING_VERSION,
    STANDARD_MODULE_PROPERTIES,
};

ZEND_COLD zend_result
datadog_profiling_hybrid_startup(zend_extension *extension) {
  if (datadog_profiling_startup(extension) != SUCCESS) {
    return FAILURE;
  }

  /* Since the module_entry and extension both come from the same DL_HANDLE,
   * we need to ensure that the handle lives as long as both. The extension
   * lives longer than the module_entry, so the extension owns the DL_HANDLE,
   * and the module_entry just has NULL.
   */
  ZEND_ASSERT(datadog_profiling_module_entry.handle == NULL);

  return zend_startup_module(&datadog_profiling_module_entry);
}
