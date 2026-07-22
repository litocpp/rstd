if(NOT EXISTS "${TOML_TEST_EXECUTABLE}")
  message(FATAL_ERROR "RSTD_TOML_TEST_EXECUTABLE must point to toml-test v2.2.0")
endif()

execute_process(
  COMMAND "${TOML_TEST_EXECUTABLE}" version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error)
if(NOT version_result EQUAL 0)
  message(FATAL_ERROR "toml-test version failed: ${version_error}")
endif()
if(NOT version_output MATCHES "^toml-test v2\\.2\\.0;")
  string(STRIP "${version_output}" version_output)
  message(FATAL_ERROR "expected toml-test v2.2.0, got: ${version_output}")
endif()

execute_process(
  COMMAND
    "${TOML_TEST_EXECUTABLE}" test -toml 1.1 -color never
    -decoder "${DECODER}"
  RESULT_VARIABLE conformance_result)
if(NOT conformance_result EQUAL 0)
  message(FATAL_ERROR "toml-test conformance failed with exit ${conformance_result}")
endif()
