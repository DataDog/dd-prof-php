file(GLOB_RECURSE RUN_TESTS_CANDIDATES ${PhpConfig_ROOT_DIR}/**/run-tests.php)
list(POP_FRONT RUN_TESTS_CANDIDATES RUN_TESTS)

add_test(
  NAME datadog-php-phpt-tests
  COMMAND ${PhpConfig_PHP_BINARY} ${RUN_TESTS} -p ${PhpConfig_PHP_BINARY} -d
          zend_extension=$<TARGET_FILE:datadog-profiling> .
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(datadog-php-phpt-tests PROPERTIES LABELS phpt)
