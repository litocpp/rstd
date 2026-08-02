cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

if(WIN32
   AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
   AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -rtlib=compiler-rt --print-libgcc-file-name
    RESULT_VARIABLE RSTD_COMPILER_RT_RESULT
    OUTPUT_VARIABLE RSTD_COMPILER_RT_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT RSTD_COMPILER_RT_RESULT EQUAL 0 OR NOT EXISTS "${RSTD_COMPILER_RT_PATH}")
    message(FATAL_ERROR "Unable to locate the Clang compiler runtime library")
  endif()
  get_filename_component(RSTD_COMPILER_RT_LIBRARY "${RSTD_COMPILER_RT_PATH}" NAME_WLE)
endif()

option(RSTD_BUILD_BENCHMARKS "Build rstd benchmark executables" OFF)
option(RSTD_USE_ASAN "asan" OFF)
option(RSTD_CHECK_INTEGER_OVERFLOW
       "Enable integer overflow checks in non-Debug builds" OFF)

set(ALL_OPTS
  -Wall
  -Wpedantic
  -fno-rtti
  -fno-exceptions
)
set(GNU_OPTS
  -Werror
  -Wno-attributes=clang::lifetimebound
)
set(CLANG_OPTS
  -Wno-gnu-statement-expression
  -Wno-deprecated-declarations
  -Wunknown-attributes
  -fsized-deallocation
)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,GNU>:${GNU_OPTS}>")
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang>:${CLANG_OPTS}>")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ALL_OPTS}>")
endif()

add_subdirectory(src)

if(RSTD_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()

# Add installation rules
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  set_target_properties(rstd.basic PROPERTIES EXPORT_NAME basic)
  set_target_properties(rstd.runtime PROPERTIES EXPORT_NAME runtime)
  set_target_properties(rstd.core PROPERTIES EXPORT_NAME core)
  set_target_properties(rstd.alloc PROPERTIES EXPORT_NAME alloc)
  set_target_properties(rstd.std PROPERTIES EXPORT_NAME rstd)
  if(TARGET rstd.dlopn)
    set_target_properties(rstd.dlopn PROPERTIES EXPORT_NAME dlopn)
  endif()
  set_target_properties(rstd.bench PROPERTIES EXPORT_NAME bench)

  install(
    TARGETS rstd.basic
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/basic)
  install(
    TARGETS rstd.runtime
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/runtime)
  install(
    TARGETS rstd.core
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/core)
  install(
    TARGETS rstd.alloc
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/alloc)
  install(
    TARGETS rstd.std
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/std)
  if(TARGET rstd.dlopn)
    install(
      TARGETS rstd.dlopn
      EXPORT rstdTargets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/dlopn)
  endif()
  install(
    TARGETS rstd.argparse
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/argparse)
  install(
    TARGETS rstd.bench
    EXPORT rstdTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/bench)

  configure_package_config_file(
    cmake/rstdConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rstdConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rstd)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rstdConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(
    EXPORT rstdTargets
    NAMESPACE rstd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rstd
    CXX_MODULES_DIRECTORY cxx-modules)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/rstdConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/rstdConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rstd)
endif()
