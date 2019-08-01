find_program(BASH bash)

add_test(test_file_write_regression
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-file-write-regression.sh")

add_test(test_142
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-142.sh")

add_test(test_149
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-149.sh")

add_test(test_153
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-153.sh")

add_test(test_161
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-161.sh")

add_test(test_177
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-177.sh")

add_test(test_core_149
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-core-149.sh")

add_test(test_nlm
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-nlm.sh")
