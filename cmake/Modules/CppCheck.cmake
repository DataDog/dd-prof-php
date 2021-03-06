# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022-Present Datadog, Inc.
option(DATADOG_PHP_STATIC_ANALYSIS "Enable static analysis tooling" OFF)
find_program(CPP_CHECK_COMMAND NAMES cppcheck)

if (DATADOG_PHP_STATIC_ANALYSIS AND CPP_CHECK_COMMAND)

  #The manual : http://cppcheck.sourceforge.net/manual.pdf
  message("-- CppCheck found : ${CPP_CHECK_COMMAND}")

  set(CPPCHECK_TEMPLATE "cppcheck:{id}:{file}:{line}:{severity}:{message}")

  list(APPEND CPP_CHECK_COMMAND 
      "--enable=warning,performance,portability,information,style"
      "--template=${CPPCHECK_TEMPLATE}"
      "--quiet" 
      "--suppress=missingInclude"
      "--suppress=missingIncludeSystem"
      "--suppress=unmatchedSuppression"
      "--force"
      "--error-exitcode=1"
      )

  # ignore all warnings coming from zend files
  foreach(PhpConfig_Single_INCLUDE_PATH ${PhpConfig_INCLUDE_DIRS})
    list(APPEND CPP_CHECK_COMMAND 
        "--suppress=*:${PhpConfig_Single_INCLUDE_PATH}*")
  endforeach()

  # Let user define their own cpp check commands if needed.
  if(NOT DEFINED CACHE{CMAKE_C_CPPCHECK})
    set(CMAKE_C_CPPCHECK "${CPP_CHECK_COMMAND};--std=c11")
  endif()
  if(NOT DEFINED CACHE{CMAKE_CXX_CPPCHECK})
    set(CMAKE_CXX_CPPCHECK "${CPP_CHECK_COMMAND};--std=c++11")
  endif()
endif()
