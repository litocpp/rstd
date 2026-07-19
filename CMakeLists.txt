cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

option(RSTD_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(RSTD_BUILD_BENCHMARKS "Build rstd benchmark executables" OFF)
option(RSTD_BENCHMARKS_IN_CTEST "Register benchmark smoke tests in ctest" OFF)
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



if(RSTD_BUILD_TESTING OR RSTD_BENCHMARKS_IN_CTEST)
  include(CTest)
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

# Testing configuration
if(RSTD_BUILD_TESTING AND BUILD_TESTING)
  add_subdirectory(test)
endif()
