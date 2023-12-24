enable_testing()

add_executable(lean_server_launch_exit_test lean-server-launch-exit.cpp)
target_link_libraries(lean_server_launch_exit_test PRIVATE libedtr)
add_test(NAME lean_server_launch_exit COMMAND $<TARGET_FILE:lean_server_launch_exit_test>)

add_executable(lean_server_open_file_test lean-server-open-file.cpp)
target_link_libraries(lean_server_open_file_test PRIVATE libedtr)
add_test(NAME lean_server_open_file COMMAND $<TARGET_FILE:lean_server_open_file_test>)

add_executable(lean_server_open_non_lean_file_test lean-server-open-non-lean-file.cpp)
target_link_libraries(lean_server_open_non_lean_file_test PRIVATE libedtr)
add_test(NAME lean_server_open_non_lean_file COMMAND $<TARGET_FILE:lean_server_open_non_lean_file_test>)


add_executable(lake_open_file_test lake-open-file.cpp)
target_link_libraries(lake_open_file_test PRIVATE libedtr)
add_test(NAME lake_open_file COMMAND $<TARGET_FILE:lake_open_file_test>)

file(COPY lake-testdir  DESTINATION .) #lake-testdir)
