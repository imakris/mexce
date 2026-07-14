if(NOT DEFINED MEXCE_PROTECT_EXECUTABLE OR
    NOT DEFINED MEXCE_PROTECT_TEST_DIR)
    message(FATAL_ERROR "The mexce_protect CLI test is missing its configuration")
endif()

file(REMOVE_RECURSE "${MEXCE_PROTECT_TEST_DIR}")
file(MAKE_DIRECTORY "${MEXCE_PROTECT_TEST_DIR}")

execute_process(
    COMMAND "${MEXCE_PROTECT_EXECUTABLE}"
    RESULT_VARIABLE usage_result
    ERROR_VARIABLE usage_error)
if(NOT usage_result EQUAL 2 OR
    NOT usage_error MATCHES "Usage: mexce_protect")
    message(FATAL_ERROR "mexce_protect did not report its command-line contract")
endif()

set(missing_expression "${MEXCE_PROTECT_TEST_DIR}/missing-expression.txt")
set(missing_schema "${MEXCE_PROTECT_TEST_DIR}/missing-schema.txt")
set(program_output "${MEXCE_PROTECT_TEST_DIR}/expression.mxp")
set(key_output "${MEXCE_PROTECT_TEST_DIR}/expression.key")
execute_process(
    COMMAND
        "${MEXCE_PROTECT_EXECUTABLE}"
        "${missing_expression}"
        "${missing_schema}"
        "${program_output}"
        "${key_output}"
    RESULT_VARIABLE failure_result
    ERROR_VARIABLE failure_error)
if(NOT failure_result EQUAL 1 OR
    NOT failure_error MATCHES "mexce_protect:")
    message(FATAL_ERROR "mexce_protect did not report an issuer failure")
endif()

set(expression_input "${MEXCE_PROTECT_TEST_DIR}/expression.txt")
set(schema_input "${MEXCE_PROTECT_TEST_DIR}/bindings.txt")
file(WRITE "${expression_input}" "value+1\n")
file(WRITE "${schema_input}" "value=0\n")
execute_process(
    COMMAND
        "${MEXCE_PROTECT_EXECUTABLE}"
        "${expression_input}"
        "${schema_input}"
        "${program_output}"
        "${key_output}"
    RESULT_VARIABLE success_result
    ERROR_VARIABLE success_error)
if(NOT success_result EQUAL 0)
    message(FATAL_ERROR "mexce_protect failed: ${success_error}")
endif()

if(NOT EXISTS "${program_output}" OR NOT EXISTS "${key_output}")
    message(FATAL_ERROR "mexce_protect did not publish both outputs")
endif()
file(SIZE "${program_output}" program_size)
file(SIZE "${key_output}" key_size)
if(program_size LESS_EQUAL 64 OR NOT key_size EQUAL 32)
    message(FATAL_ERROR "mexce_protect produced invalid output sizes")
endif()

file(REMOVE_RECURSE "${MEXCE_PROTECT_TEST_DIR}")
