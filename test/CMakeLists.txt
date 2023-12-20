enable_testing()

add_executable(lean_server_launch_exit_test lean-server-launch-exit.cpp)
target_link_libraries(lean_server_launch_exit_test PRIVATE libedtr)

add_test(NAME lean_server_launch_exit COMMAND $<TARGET_FILE:lean_server_launch_exit_test>)



