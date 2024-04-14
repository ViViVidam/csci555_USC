cmake_minimum_required(VERSION 3.13)
project(thanos)

set(CMAKE_CXX_STANDARD 20)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPOsupported OUTPUT error)

# Add our module directory to the include path.
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

set(THANOS_EXTERNAL_LIBS "")

include_directories(src/)
include_directories(src/third_party_libs)
include_directories(src/migration)
include_directories(src/samples)
include_directories(src/system_info)
include_directories(src/utils)

option(JUST_INS "Measure just INST_RETIRED hardware counter. Disables measurements of vector operations." OFF)
option(USE_512 "Enables measurements of 512b vector operations." OFF)
option(PROF "Enables options for profiling of the tool." OFF)
option(LIBPFM_INSTALL "Specify location of libpfm library" "")

if (JUST_INS)
    MESSAGE(STATUS "Enabled measurement of only INST_RETIRED hardware counter")
    add_compile_definitions("JUST_INS")
endif ()

if (USE_512)
    MESSAGE(STATUS "Enabled measurement of 512 bit vector instructions")
    add_compile_definitions("USE_512B_INS")
endif ()

# Debug and profile options
if (PROF)
    MESSAGE(STATUS "Using compiler debug and profile flags: -g -pg -fno-omit-frame-pointer")
    add_compile_options("-g")
    add_compile_options("-pg")
    add_link_options("-pg")
    add_compile_options("-fno-omit-frame-pointer")
endif ()

MESSAGE(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

MESSAGE(STATUS "Compiler family: ${CMAKE_CXX_COMPILER_ID}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # using Clang
    add_compile_options("-fconcepts")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # using GCC
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    # using Intel C++
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # using Visual Studio C++
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    MESSAGE(STATUS "Compilation flags: ${CMAKE_CXX_FLAGS_RELEASE}")
    MESSAGE(STATUS "Using further compiler optimisation flags: -march=native -ffast-math")
    add_compile_options("-march=native")
    add_compile_options("-ffast-math")
else ()
    MESSAGE(STATUS "Compilation flags: ${CMAKE_CXX_FLAGS_DEBUG}")
    MESSAGE(STATUS "Using compiler pedantic flags: -Wall -Wpedantic -Wextra -fdiagnostics-show-option -Wno-unused-function")
    add_compile_options("-Wall")
    add_compile_options("-Wpedantic")
    add_compile_options("-Wextra")
    # add_compile_options("-Weffc++") # Some third-party libs trigger a lot of warnings :(
    add_compile_options("-fdiagnostics-show-option")
    add_compile_options("-Wno-unused-function")
endif ()

include(FindLibpfm)
if (LIBPFM_FOUND)
    MESSAGE(STATUS "Found perfmon/pfmlib_perf_event.h in " ${LIBPFM_INCLUDE_DIR})
    MESSAGE(STATUS "Found libpfm.so in " ${LIBPFM_LIBRARY})
    list(APPEND THANOS_EXTERNAL_LIBS ${LIBPFM_LIBRARY})
else ()
    MESSAGE(FATAL_ERROR "Libpfm support was requested but libpfm.so was not found!\n"
            "Set -DLIBPFM_INSTALL=<path to libpfm src directory (e.g. -DLIBPFM_INSTALL=~/perfmon2/4.11.0)"
            " and re-run cmake")
endif ()

add_executable(thanos
        src/main.cpp src/utils/verbose.cpp src/system_info/system_info.cpp src/system_info/memory_info.cpp
        src/samples/samples.cpp src/samples/perf_event/perf_event.cpp src/migration/tickets.cpp
        src/migration/utils/times.cpp src/migration/migration_var.cpp)

target_link_libraries(thanos numa)
target_link_libraries(thanos ${THANOS_EXTERNAL_LIBS})

if (IPOsupported)
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET thanos PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else ()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif ()