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

set(
  RSTD_TEST_SOURCES
  trait.cpp
  any.cpp
  argparse/definition.cpp
  argparse/parsing.cpp
  argparse/relations.cpp
  argparse/subcommands.cpp
  argparse/values.cpp
  argparse/help.cpp
  argparse/lifecycle.cpp
  argparse/errors.cpp
  array.cpp
  slice.cpp
  atomic.cpp
  deref.cpp
  result.cpp
  try_async.cpp
  compile.cpp
  option.cpp
  clone.cpp
  clone_tuple.cpp
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
  collections/btree_set.cpp
  collections/hash_map.cpp
  collections/hash_set.cpp
  json/number.cpp
  json/value.cpp
  json/parser.cpp
  json/serialize.cpp
  json/module.cpp
  toml/parser.cpp
  toml/module.cpp
  iter/iterator.cpp
  sys/sync/mutex/futex.cpp
  thread/thread.cpp
  thread/blocking_task_group.cpp
  num/nonzero.cpp
  thread.cpp
  async.cpp
  async/abort_on_drop.cpp
  async/concurrency.cpp
  async/facility.cpp
  async/frame.cpp
  async/lifecycle.cpp
  async/poll.cpp
  async/runtime.cpp
  bytes.cpp
  sync/mutex.cpp
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
  str_literal_cross_tu.cpp
  str_literal_cross_tu_provider.cpp
  ffi/os_str.cpp
  ffi/c_str.cpp
  path.cpp
  prelude.cpp
  log.cpp)

if(UNIX)
  list(
    APPEND
    RSTD_TEST_SOURCES
    sys/sync/condvar/pthread.cpp
    sys/sync/mutex/pthread.cpp
    process.cpp
    os_fd.cpp
    fs.cpp)
endif()

add_executable(rstd_test ${RSTD_TEST_SOURCES})
target_compile_definitions(
  rstd_test
  PRIVATE
    "RSTD_TEST_CHECK_INTEGER_OVERFLOW=$<OR:$<CONFIG:Debug>,$<BOOL:${RSTD_CHECK_INTEGER_OVERFLOW}>>")

target_sources(
  rstd_test
  PRIVATE FILE_SET modules TYPE CXX_MODULES FILES log_module_check.cppm
          any_module_check.cppm)

target_link_libraries(rstd_test PRIVATE rstd::rstd rstd::json rstd::toml rstd::log rstd::argparse
                                        GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(rstd_test)

add_executable(rstd_toml_test_decoder EXCLUDE_FROM_ALL toml/conformance_decoder.cpp)
target_link_libraries(rstd_toml_test_decoder PRIVATE rstd::rstd rstd::json rstd::toml)

set(RSTD_TOML_TEST_EXECUTABLE "" CACHE FILEPATH "Path to toml-test v2.2.0")
add_custom_target(
  rstd_toml_conformance
  COMMAND
    ${CMAKE_COMMAND}
    "-DTOML_TEST_EXECUTABLE=${RSTD_TOML_TEST_EXECUTABLE}"
    "-DDECODER=$<TARGET_FILE:rstd_toml_test_decoder>"
    -P ${CMAKE_CURRENT_LIST_DIR}/toml/run_conformance.cmake
  DEPENDS rstd_toml_test_decoder
  USES_TERMINAL)

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

add_executable(rstd_bench_test bench.cpp)
target_link_libraries(rstd_bench_test PRIVATE rstd::bench GTest::gtest_main)
gtest_discover_tests(rstd_bench_test)

add_executable(rstd_lifetime_annotation_positive EXCLUDE_FROM_ALL lifetimebound_positive.cpp)
target_link_libraries(rstd_lifetime_annotation_positive PRIVATE rstd::rstd)
target_compile_options(
  rstd_lifetime_annotation_positive
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")

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
  rstd_compile_fail_choice_empty
  compile_fail/choice_empty.cpp
  Choice.Empty
  "rstd::Choice requires at least one choice_case")
add_rstd_compile_fail(
  rstd_compile_fail_choice_mixed_tags
  compile_fail/choice_mixed_tags.cpp
  Choice.MixedTags
  "all rstd::Choice tags must have the same type")
add_rstd_compile_fail(
  rstd_compile_fail_choice_duplicate_tags
  compile_fail/choice_duplicate_tags.cpp
  Choice.DuplicateTags
  "all rstd::Choice tags must be unique")
add_rstd_compile_fail(
  rstd_compile_fail_choice_invalid_tag
  compile_fail/choice_invalid_tag.cpp
  Choice.InvalidTag
  "the requested tag is not present in rstd::Choice")
add_rstd_compile_fail(
  rstd_compile_fail_choice_invalid_arguments
  compile_fail/choice_invalid_arguments.cpp
  Choice.InvalidArguments
  "no matching function|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_choice_reference_payload
  compile_fail/choice_reference_payload.cpp
  Choice.ReferencePayload
  "reference payloads are not supported by rstd::Choice")

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
add_rstd_compile_fail(
  rstd_compile_fail_numeric_mixed_native_arithmetic
  compile_fail/numeric_mixed_native_arithmetic.cpp
  Numeric.MixedNativeArithmetic
  "invalid operands to binary expression|no viable overloaded")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_implicit_narrowing
  compile_fail/numeric_implicit_narrowing.cpp
  Numeric.ImplicitNarrowing
  "no viable conversion|cannot initialize")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_implicit_primitive
  compile_fail/numeric_implicit_primitive.cpp
  Numeric.ImplicitPrimitive
  "no viable conversion|cannot initialize|explicit constructor")
add_rstd_compile_fail(
  rstd_compile_fail_invalid_utf8_literal
  compile_fail/invalid_utf8_literal.cpp
  Str.InvalidUtf8Literal
  "rstd string literal must be valid UTF-8")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_increment
  compile_fail/numeric_increment.cpp
  Numeric.SignedIncrement
  "cannot increment|invalid argument type|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_temporary_increment
  compile_fail/numeric_temporary_increment.cpp
  Numeric.TemporaryIncrement
  "cannot increment|invalid argument type|no viable overloaded|candidate function not viable|expects an lvalue")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_literal_out_of_range
  compile_fail/numeric_literal_out_of_range.cpp
  Numeric.LiteralOutOfRange
  "not a constant expression|consteval function")
add_rstd_compile_fail(
  rstd_compile_fail_numeric_constexpr_overflow
  compile_fail/numeric_constexpr_overflow.cpp
  Numeric.ConstexprOverflow
  "not a constant expression|constexpr variable")
add_rstd_compile_fail(
  rstd_compile_fail_hash_key_without_hash
  compile_fail/hash_key_without_hash.cpp
  Hash.KeyWithoutHash
  "HashableBy|HashableWith|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_hash_builder_missing
  compile_fail/hash_builder_missing.cpp
  Hash.BuilderMissing
  "HashBuilder|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_hash_builder_invalid_state
  compile_fail/hash_builder_invalid_state.cpp
  Hash.BuilderInvalidState
  "HashBuilder|Hasher|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_hash_callable_builder
  compile_fail/hash_callable_builder.cpp
  Hash.CallableBuilder
  "HashBuilder|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_hash_float_key
  compile_fail/hash_float_key.cpp
  Hash.FloatKey
  "HashableBy|HashableWith|constraints not satisfied")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_box
  compile_fail/lifetimebound_temporary_box.cpp
  LifetimeBound.TemporaryBox
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_box
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_vec
  compile_fail/lifetimebound_temporary_vec.cpp
  LifetimeBound.TemporaryVec
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_vec
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_string_view
  compile_fail/lifetimebound_temporary_string_view.cpp
  LifetimeBound.TemporaryStringView
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_string_view
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_array
  compile_fail/lifetimebound_temporary_array.cpp
  LifetimeBound.TemporaryArray
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_array
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_owned_fd
  compile_fail/lifetimebound_temporary_owned_fd.cpp
  LifetimeBound.TemporaryOwnedFd
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_owned_fd
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_temporary_file
  compile_fail/lifetimebound_temporary_file.cpp
  LifetimeBound.TemporaryFile
  "temporary whose address is used.*will be destroyed|will be destroyed at the end of the full-expression")
target_compile_options(
  rstd_compile_fail_lifetimebound_temporary_file
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>")
add_rstd_compile_fail(
  rstd_compile_fail_lifetimebound_trait_proxy
  compile_fail/lifetimebound_trait_proxy.cpp
  LifetimeBound.TraitProxy
  "address of stack memory associated with local variable.*returned|returning address of local temporary object")
target_compile_options(
  rstd_compile_fail_lifetimebound_trait_proxy
  PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=return-stack-address>")

add_executable(rstd_nostd_test nostd/main.cpp)
target_link_libraries(rstd_nostd_test PRIVATE rstd::nostd)
target_link_options(rstd_nostd_test PRIVATE -nostdlib++)
add_test(NAME Nostd.Runtime COMMAND rstd_nostd_test)
