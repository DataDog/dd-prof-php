add_library(datadog-php-config OBJECT config.c config.h)

target_include_directories(datadog-php-config
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_compile_features(datadog-php-config
  INTERFACE c_std_99
  PRIVATE c_std_11
)

target_link_libraries(datadog-php-config
  PUBLIC
    datadog-php-arena
    datadog-php-log
    datadog_php_string_view
    DDProf::FFI
  PRIVATE PhpConfig::PhpConfig
)
