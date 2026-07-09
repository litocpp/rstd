if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR is required")
endif()
if(NOT DEFINED TARGET_NAME)
  message(FATAL_ERROR "TARGET_NAME is required")
endif()
if(NOT DEFINED EXPECTED_REGEX)
  message(FATAL_ERROR "EXPECTED_REGEX is required")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target
                  "${TARGET_NAME}")
if(DEFINED CONFIG AND NOT "${CONFIG}" STREQUAL "")
  list(APPEND build_command --config "${CONFIG}")
endif()

execute_process(
  COMMAND ${build_command}
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr)

set(build_output "${build_stdout}${build_stderr}")
if(build_result EQUAL 0)
  message("${build_output}")
  message(FATAL_ERROR "Expected ${TARGET_NAME} to fail")
endif()

if(NOT build_output MATCHES "${EXPECTED_REGEX}")
  message("${build_output}")
  message(FATAL_ERROR "Expected diagnostic was not found")
endif()

message(STATUS "${TARGET_NAME} failed with expected diagnostic")
