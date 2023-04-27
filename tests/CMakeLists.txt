find_program(BASH bash)
find_package(Python REQUIRED)

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

add_test(test_multipage_readers
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-multipage-readers.sh")

add_test(test_gradient
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-gradient.sh")

add_test(test_memin
         ${Python_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_memin.py")

add_test(test_fft
         ${BASH} "${CMAKE_CURRENT_SOURCE_DIR}/test-fft.sh")

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/make-input-multipage-readers
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/check-multipage-readers
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/check-gradient
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/check-fft
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)
