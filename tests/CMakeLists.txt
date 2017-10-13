find_program(BASH bash)

add_test(test_file_write_regression
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-file-write-regression.sh")
