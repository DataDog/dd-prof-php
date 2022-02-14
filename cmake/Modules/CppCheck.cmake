# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022-Present Datadog, Inc.
option(DD_STATIC_ANALYSIS "Enable static analysis tooling" OFF)
find_program(CPP_CHECK_COMMAND NAMES cppcheck)

if (DD_STATIC_ANALYSIS AND CPP_CHECK_COMMAND)

  #The manual : http://cppcheck.sourceforge.net/manual.pdf
  message("-- CppCheck found : ${CPP_CHECK_COMMAND}")

  # Listing all files to check manually (we could also use existing variables)
  set(CPPCHECK_TEMPLATE "cppcheck:{id}:{file}:{line}:{severity}:{message}")

  list(APPEND CPP_CHECK_COMMAND 
      "--enable=warning,performance,portability,information,style"
      "--template=${CPPCHECK_TEMPLATE}"
      "--quiet" 
      "--suppressions-list=${CMAKE_SOURCE_DIR}/CppCheckSuppressions.txt"
      "--force"
      "--error-exitcode=1"
      )
    # Let user define his own cpp check commands if needed
    if(NOT DEFINED CACHE{CMAKE_C_CPPCHECK})
        set(CMAKE_CXX_CPPCHECK "${CPP_CHECK_COMMAND};--std=c++11")
        set(CMAKE_C_CPPCHECK "${CPP_CHECK_COMMAND};--std=c11")
    endif()
endif()
