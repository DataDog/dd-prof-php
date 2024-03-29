#[[ Need 3.19+ for HANDLE_VERSION_RANGE support in the FindPhpConfig module.
    This should be checked in the FindPhpConfig.cmake file somehow, but I do not
    know to do this yet, so we do it here instead.
 ]]
cmake_minimum_required(VERSION 3.19)

project(datadog-php-profiling LANGUAGES C CXX)

# Add cmake/Modules to CMAKE_MODULE_PATH so find_package(PhpConfig) will work
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

if(UNIX) # This is probably a bit too broad
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/configurations.cmake)

  #[[ For some reason, I often write "RelWithDebugInfo" instead of the correct
      "RelWithDebInfo". This code will error on an unknown build type.
      These checks depend on cmake/configurations.cmake, so that's why it's done
      only in this branch.
  ]]
  if(CMAKE_BUILD_TYPE)
    get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    get_property(
      allowedBuildTypes
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS)
    if(NOT isMultiConfig AND NOT CMAKE_BUILD_TYPE IN_LIST allowedBuildTypes)
      message(
        FATAL_ERROR
          "Invalid build type: ${CMAKE_BUILD_TYPE}; allowed build types: ${allowedBuildTypes}"
      )
    endif()
  endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE on)

option(DATADOG_PHP_TESTING OFF)
if(DATADOG_PHP_TESTING)
  enable_testing()

  find_package(Catch2 REQUIRED)
  include(Catch)

  if(NOT TARGET Catch2::Catch2WithMain AND TARGET Catch2::Catch2)
    #[[ The build of catch2 we are using wasn't configured with
        `CATCH_BUILD_STATIC_LIBRARY`; let's polyfill it.
    ]]
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/catch2withmain.cc
         "#define CATCH_CONFIG_MAIN\n" "#include <catch2/catch.hpp>\n")

    add_library(Catch2WithMain ${CMAKE_CURRENT_BINARY_DIR}/catch2withmain.cc)
    target_compile_features(Catch2WithMain INTERFACE cxx_std_11)
    target_link_libraries(Catch2WithMain PUBLIC Catch2::Catch2)
    add_library(Catch2::Catch2WithMain ALIAS Catch2WithMain)
  endif()

  if(NOT TARGET Catch2::Catch2WithMain)
    message(FATAL_ERROR "Catch2WithMain not found and polyfill failed.")
  endif()
endif()

find_package(Threads REQUIRED)
find_package(PhpConfig 7.1...<8.2 REQUIRED)
find_package(PkgConfig REQUIRED)

# Cpp check static analysis
include(CppCheck)

#[[ Prefer libuv-static, but note that it needs to be built with Position
    Independent Code (PIC).
 ]]
pkg_check_modules(UV IMPORTED_TARGET libuv-static)
if(NOT UV_FOUND)
  pkg_check_modules(UV REQUIRED IMPORTED_TARGET libuv)
endif()

add_subdirectory(components)

# Prefer static libddprof to shared
set(_DATADOG_PHP_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES
    ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a .so)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a .dylib .so .tbd)
endif()

find_package(DDProf REQUIRED)

# Restore
set(CMAKE_FIND_LIBRARY_SUFFIXES
    ${_DATADOG_PHP_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

add_subdirectory(profiling)

add_library(
  datadog-profiling MODULE
  datadog-profiling.c
  profiling/datadog-profiling.c
  profiling/datadog-profiling.h
  profiling/plugins/log_plugin/log_plugin.c
  profiling/plugins/recorder_plugin/recorder_plugin.c
  profiling/plugins/stack_collector_plugin/stack_collector_plugin.c)

target_compile_features(datadog-profiling PUBLIC c_std_11)

#[[ Note that linking directly to libc dynamically is actually important!
    Without it, on Alpine it will sometimes link libc in statically, and then
    we hide the symbols with a linker script, and then __vdsosym will fail to
    find symbols in libc like clock_gettime!
 ]]
target_link_libraries(
  datadog-profiling
  PRIVATE datadog-php-arena
          datadog-php-channel
          datadog-php-config
          datadog-php-env
          datadog-php-log
          datadog-php-once
          datadog_php_sapi
          datadog-php-stack-collector
          datadog-php-stack-sample
          datadog_php_string_view
          datadog-php-time
          datadog-php-uuid
          DDProf::FFI
          PkgConfig::UV
          PhpConfig::PhpConfig
          Threads::Threads
  PUBLIC -lc)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set_target_properties(
    datadog-profiling
    PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/datadog-profiling.sym)

  include(CheckCCompilerFlag)
  check_c_compiler_flag(-static-libgcc HAS_STATIC_LIBGCC)

  if(HAS_STATIC_LIBGCC)
    target_link_options(datadog-profiling PRIVATE -static-libgcc)
  endif()

  target_link_options(
    datadog-profiling PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/datadog-profiling.sym)
endif()

set_target_properties(
  datadog-profiling
  PROPERTIES PREFIX "" # PHP modules are not prefixed with lib*
             C_VISIBILITY_PRESET hidden)

target_include_directories(
  datadog-profiling
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/components>)

if(DATADOG_PHP_TESTING)
  add_subdirectory(tests)
endif()
