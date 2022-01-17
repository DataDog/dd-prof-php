# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022-Present Datadog, Inc.

find_program(CPP_CHECK_COMMAND NAMES cppcheck)
if (CPP_CHECK_COMMAND)
   #The manual : http://cppcheck.sourceforge.net/manual.pdf
   message("-- CppCheck found : ${CPP_CHECK_COMMAND}")

   # Listing all files to check manually (we could also use existing variables)
   file(GLOB_RECURSE CPPCHECK_ALL_SOURCE_FILES profiling/*.c profiling/*.h components/*.c components/*.h)
   set(CPPCHECK_TEMPLATE "cppcheck:{id}:{file}:{line}:{severity}:{message}")

   list(APPEND CPPCHECK_INCLUDE_CMD "-I${CMAKE_SOURCE_DIR}/profiling/")
   list(APPEND CPPCHECK_INCLUDE_CMD "-I${CMAKE_SOURCE_DIR}")
   # Include directories can help with the analysis when using PHP libraries (though they make things slow !)
    #foreach(IncludeDir ${PhpConfig_INCLUDE_DIRS})
    #     list(APPEND CPPCHECK_INCLUDE_CMD "-I${IncludeDir}")
    #  endforeach()

   list(APPEND CPP_CHECK_COMMAND 
         "--enable=warning,performance,portability,information,style"
         "--template=${CPPCHECK_TEMPLATE}"
         "--quiet" 
         "--suppressions-list=${CMAKE_SOURCE_DIR}/CppCheckSuppressions.txt"
         ${CPPCHECK_INCLUDE_CMD}
         )

   add_custom_target(
      cppcheck
      COMMAND ${CPP_CHECK_COMMAND}
      --error-exitcode=1 # make sure CI pipeline fails
    #   --check-config #check what header files are missing
      ${CPPCHECK_ALL_SOURCE_FILES}
      )
endif()
