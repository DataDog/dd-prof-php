file(GLOB_RECURSE RUN_TESTS_CANDIDATES ${PhpConfig_ROOT_DIR}/**/run-tests.php)
list(POP_FRONT RUN_TESTS_CANDIDATES RUN_TESTS)

#[[ The run-tests.php file may not be able to be run in-place because it does
    some file I/O relative to its location, and the user may not have
    permissions. So, copy it to the build dir.
 ]]
add_test(NAME datadog-php-copy-run-tests
         COMMAND ${CMAKE_COMMAND} -E copy ${RUN_TESTS}
                 ${CMAKE_CURRENT_BINARY_DIR}/run-tests.php)

add_test(
  NAME datadog-php-phpt-tests
  COMMAND
    ${PhpConfig_PHP_BINARY} ${CMAKE_CURRENT_BINARY_DIR}/run-tests.php -p
    ${PhpConfig_PHP_BINARY} -d zend_extension=$<TARGET_FILE:datadog-profiling> .
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(datadog-php-phpt-tests
                     PROPERTIES LABELS phpt DEPENDS datadog-php-copy-run-tests)
