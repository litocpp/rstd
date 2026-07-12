set(gtest_force_shared_crt ON)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0)
  FetchContent_MakeAvailable(googletest)
endif()

if(RSTD_USE_ASAN)
  add_compile_options(-g -fsanitize=address -fsanitize=undefined
                      -fsanitize-recover=address)

  add_link_options(-fsanitize=address -fsanitize=undefined
                   -fsanitize-recover=address)
endif()

set(
  RSTD_TEST_SOURCES
  trait.cpp
  deref.cpp
  result.cpp
  try_async.cpp
  compile.cpp
  option.cpp
  clone.cpp
  convert.cpp
  enum.cpp
  hint.cpp
  alloc/rc.cpp
  alloc/str.cpp
  alloc/boxed.cpp
  alloc/vec.cpp
  alloc/sync.cpp
  alloc/string.cpp
  collections/btree_map.cpp
  collections/hash_map.cpp
  json/number.cpp
  json/value.cpp
  json/parser.cpp
  json/serialize.cpp
  json/module.cpp
  iter/iterator.cpp
  sys/sync/mutex/futex.cpp
  thread/thread.cpp
  num/nonzero.cpp
  thread.cpp
  async.cpp
  async/concurrency.cpp
  async/facility.cpp
  async/frame.cpp
  async/lifecycle.cpp
  async/poll.cpp
  async/runtime.cpp
  bytes.cpp
  sync/condvar.cpp
  sync/mpsc.cpp
  time.cpp
  fmt.cpp
  io.cpp
  net.cpp
  env.cpp
  char.cpp
  floats.cpp
  ints.cpp
  str.cpp
  ffi/os_str.cpp
  path.cpp
  log.cpp)

if(UNIX)
  list(
    APPEND
    RSTD_TEST_SOURCES
    sys/sync/mutex/pthread.cpp
    process.cpp
    sys_fd.cpp
    fs.cpp)
endif()

add_executable(rstd_test ${RSTD_TEST_SOURCES})

target_sources(
  rstd_test
  PRIVATE FILE_SET modules TYPE CXX_MODULES FILES log_module_check.cppm)

target_link_libraries(rstd_test PRIVATE rstd::rstd GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(rstd_test)

add_executable(rstd_try_test try.cpp)
target_sources(
  rstd_try_test
  PRIVATE FILE_SET modules TYPE CXX_MODULES FILES try_module_check.cppm)
target_link_libraries(rstd_try_test PRIVATE rstd::core rstd.runtime GTest::gtest_main)
gtest_discover_tests(rstd_try_test)

add_executable(rstd_try_coroutine_test try_coroutine.cpp)
target_link_libraries(rstd_try_coroutine_test PRIVATE rstd::core rstd.runtime
                                                      GTest::gtest_main)
gtest_discover_tests(rstd_try_coroutine_test)

function(add_rstd_compile_fail target source name expected_regex)
  add_executable(${target} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${target} PRIVATE rstd::rstd)
  add_test(
    NAME ${name}
    COMMAND
      ${CMAKE_COMMAND}
      -DBUILD_DIR=${CMAKE_BINARY_DIR}
      -DTARGET_NAME=${target}
      -DCONFIG=$<CONFIG>
      "-DEXPECTED_REGEX=${expected_regex}"
      -P ${CMAKE_CURRENT_LIST_DIR}/compile_fail/run_compile_fail.cmake)
  set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE)
endfunction()

add_rstd_compile_fail(
  rstd_compile_fail_trait_impled_diagnostic
  compile_fail/trait_impled_diagnostic.cpp
  Trait.ImpledDiagnosticFailure
  "rstd::Impled failed: no external Impl or complete in-class trait API")
add_rstd_compile_fail(
  rstd_compile_fail_trait_in_class_api_mismatch
  compile_fail/trait_in_class_api_mismatch.cpp
  Trait.InClassApiMismatchFailure
  "rstd::Impled failed: in-class implementation does not satisfy trait API")
add_rstd_compile_fail(
  rstd_compile_fail_trait_external_api_mismatch
  compile_fail/trait_external_api_mismatch.cpp
  Trait.ExternalApiMismatchFailure
  "rstd::Impled failed: external Impl does not satisfy trait API")
add_rstd_compile_fail(
  rstd_compile_fail_trait_external_const_api_mismatch
  compile_fail/trait_external_const_api_mismatch.cpp
  Trait.ExternalConstApiMismatchFailure
  "rstd::Impled failed: external Impl does not satisfy trait API")
add_rstd_compile_fail(
  rstd_compile_fail_trait_direct_api_mismatch
  compile_fail/trait_direct_api_mismatch.cpp
  Trait.DirectApiMismatchFailure
  "rstd::Impled failed: direct trait implementation does not satisfy trait API")
add_rstd_compile_fail(
  rstd_compile_fail_std_does_not_export_json
  compile_fail/std_does_not_export_json.cpp
  JsonModule.StdDoesNotExportJson
  "no member named 'json' in namespace 'rstd'")
add_rstd_compile_fail(
  rstd_compile_fail_try_unsupported_source
  compile_fail/try_unsupported_source.cpp
  Try.UnsupportedSourceFailure
  "rstd_try requires rstd::Result or rstd::Option")
add_rstd_compile_fail(
  rstd_compile_fail_try_option_to_result
  compile_fail/try_option_to_result.cpp
  Try.OptionToResultFailure
  "no viable conversion|could not convert")
add_rstd_compile_fail(
  rstd_compile_fail_try_result_to_option
  compile_fail/try_result_to_option.cpp
  Try.ResultToOptionFailure
  "no viable conversion|could not convert")
add_rstd_compile_fail(
  rstd_compile_fail_try_incompatible_error
  compile_fail/try_incompatible_error.cpp
  Try.IncompatibleErrorFailure
  "no viable conversion|could not convert")
add_rstd_compile_fail(
  rstd_compile_fail_deref_mut_without_deref
  compile_fail/deref_mut_without_deref.cpp
  Deref.DerefMutWithoutDerefFailure
  "constraints not satisfied|Impled.*Deref")

if(OFF)
add_executable(nostd nostd/main.cpp)
target_link_libraries(nostd PRIVATE rstd::nostd)
target_link_options(nostd PRIVATE -nostdlib++)
endif()
