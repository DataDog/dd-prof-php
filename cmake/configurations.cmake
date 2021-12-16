# This pattern is in the book Professional CMake; I referenced 8th edition.

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3" CACHE STRING "C flags" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3" CACHE STRING "C++ flags" FORCE)

#[[ RelWithDebInfo by default will do -O2 -g -DNDEBUG, which is reasonable.
    Release by default will do -O3 -DNDEBUG, which is also reasonable.
    However, we can get a better debugging experience for RelWithDebInfo
    with -Og -g -DNDEBUG. We also want release builds to have symbols.
    So, we tinker with the flags.
]]
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Og -g3 -DNDEBUG" CACHE STRING "C flags" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Og -g3 -DNDEBUG" CACHE STRING "C++ flags" FORCE)

set(CMAKE_C_FLAGS_RELEASE "-O3 -g1 -DNDEBUG" CACHE STRING "C flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g1 -DNDEBUG" CACHE STRING "C++ flags" FORCE)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(isMultiConfig)
  if(NOT "RelWithAsan" IN_LIST CMAKE_CONFIGURATION_TYPES)
    list(APPEND CMAKE_CONFIGURATION_TYPES RelWithAsan)
  endif()
else()
  get_property(allowedBuildTypes CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS)
  if (NOT "Debug" IN_LIST allowedBuildTypes)
    list(APPEND allowedBuildTypes Debug)
  endif()
  if (NOT "RelWithDebInfo" IN_LIST allowedBuildTypes)
    list(APPEND allowedBuildTypes RelWithDebInfo)
  endif()
  if (NOT "RelWithAsan" IN_LIST allowedBuildTypes)
    list(APPEND allowedBuildTypes RelWithAsan)
  endif()
  if (NOT "Release" IN_LIST allowedBuildTypes)
    list(APPEND allowedBuildTypes Release)
  endif()
  if (NOT "MinSizeRel" IN_LIST allowedBuildTypes)
    list(APPEND allowedBuildTypes MinSizeRel)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${allowedBuildTypes}")
endif()

set(CMAKE_C_FLAGS_RELWITHASAN
  "-O1 -g -DNDEBUG -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
  "Flags used by the C compiler for theRelWithAsan build type." FORCE)

set(CMAKE_CXX_FLAGS_RELWITHASAN
  "-O1 -g -DNDEBUG -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
  "Flags used by the C++ compiler for the RelWithAsan build type." FORCE)

set(CMAKE_EXE_LINKER_FLAGS_RELWITHASAN
  "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} -fsanitize=address" CACHE STRING
  "Flags to be used to create executables for the RelWithAsan build type." FORCE)

set(CMAKE_SHARED_LINKER_FLAGS_RELWITHASAN
  "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} -fsanitize=address" CACHE STRING
  "Flags used to create shared libraries for the RelWithAsan build type." FORCE)

set(CMAKE_MODULE_LINKER_FLAGS_RELWITHASAN
  "${CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO} -fsanitize=address" CACHE STRING
  "Flags used to create modules for the RelWithAsan build type." FORCE)
