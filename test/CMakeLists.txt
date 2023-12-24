enable_testing()

add_executable(lean_server_launch_exit lean-server-launch-exit.cpp)
target_link_libraries(lean_server_launch_exit PRIVATE libedtr)
add_test(NAME lean_server_launch_exit COMMAND $<TARGET_FILE:lean_server_launch_exit>)

add_executable(lean_server_open_file lean-server-open-file.cpp)
target_link_libraries(lean_server_open_file PRIVATE libedtr)
add_test(NAME lean_server_open_file COMMAND $<TARGET_FILE:lean_server_open_file>)

add_executable(lean_server_open_non_lean_file lean-server-open-non-lean-file.cpp)
target_link_libraries(lean_server_open_non_lean_file PRIVATE libedtr)
add_test(NAME lean_server_open_non_lean_file COMMAND $<TARGET_FILE:lean_server_open_non_lean_file>)

add_executable(lake_open_file lake-open-file.cpp)
target_link_libraries(lake_open_file PRIVATE libedtr)
add_test(NAME lake_open_file COMMAND $<TARGET_FILE:lake_open_file>)
file(COPY lake-testdir  DESTINATION .)

add_executable(abbreviations abbreviations.cpp)
target_link_libraries(abbreviations PRIVATE libedtr)
add_test(NAME abbreviations COMMAND $<TARGET_FILE:abbreviations>)
file(COPY ../src/abbreviations.json DESTINATION .) # used to test abbreviations.

