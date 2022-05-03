add_library(datadog-php-once once.c)
target_include_directories(
  datadog-php-once PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)

target_compile_features(datadog-php-once PUBLIC c_std_11)

# libuv's pc file doesn't put -pthread into its link line, but it depends on it
# for all our current platforms.
find_package(Threads REQUIRED)
target_link_libraries(datadog-php-once PRIVATE PkgConfig::UV Threads::Threads)

# TODO: add tests if(DATADOG_PHP_TESTING) add_subdirectory(tests) endif()
