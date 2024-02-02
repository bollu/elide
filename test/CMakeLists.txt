enable_testing()

# add_executable(lean_server_launch_exit lean-server-launch-exit.cpp)
# target_link_libraries(lean_server_launch_exit PRIVATE edtr)
# add_test(NAME lean_server_launch_exit COMMAND $<TARGET_FILE:lean_server_launch_exit>)
# 
# file(COPY test_file.lean DESTINATION .) # copy test file.
# 
# add_executable(lean_server_open_file lean-server-open-file.cpp)
# target_link_libraries(lean_server_open_file PRIVATE edtr)
# add_test(NAME lean_server_open_file COMMAND $<TARGET_FILE:lean_server_open_file>)
# 
# add_executable(lean_server_hover lean-server-hover.cpp)
# target_link_libraries(lean_server_hover PRIVATE edtr)
# add_test(NAME lean_server_hover COMMAND $<TARGET_FILE:lean_server_hover>)
# 
# add_executable(lean_server_open_non_lean_file lean-server-open-non-lean-file.cpp)
# target_link_libraries(lean_server_open_non_lean_file PRIVATE edtr)
# add_test(NAME lean_server_open_non_lean_file COMMAND $<TARGET_FILE:lean_server_open_non_lean_file>)
# file(COPY test_file.txt DESTINATION .) # copy test file.
# 
# 
# add_executable(utf8 utf8.cpp)
# target_link_libraries(utf8 PRIVATE edtr)
# add_test(NAME utf8 COMMAND $<TARGET_FILE:utf8>)
# 
# 
# file(COPY lake-testdir  DESTINATION .)
# 
# add_executable(lake_hover_cli lake-hover-cli.cpp)
# target_link_libraries(lake_hover_cli PRIVATE edtr)
# add_test(NAME lake_hover_cli COMMAND $<TARGET_FILE:lake_hover_cli>)
# 
# add_executable(lake_completion_cli lake-completion-cli.cpp)
# target_link_libraries(lake_completion_cli PRIVATE edtr)
# add_test(NAME lake_completion_cli COMMAND $<TARGET_FILE:lake_completion_cli>)
# 
# add_executable(lake_get_messages lake-get-messages.cpp)
# target_link_libraries(lake_get_messages PRIVATE edtr)
# add_test(NAME lake_get_messages COMMAND $<TARGET_FILE:lake_get_messages>)
# 
# add_executable(abbreviations abbreviations.cpp)
# target_link_libraries(abbreviations PRIVATE edtr)
# add_test(NAME abbreviations COMMAND $<TARGET_FILE:abbreviations>)
# file(COPY ../src/abbreviations.json DESTINATION .) # used to test abbreviations.
# 
# add_executable(edit edit.cpp)
# target_link_libraries(edit PRIVATE edtr)
# add_test(NAME edit COMMAND $<TARGET_FILE:edit>)
# 
# 
# add_executable(rg rg.cpp)
# target_link_libraries(rg PRIVATE edtr)
# add_test(NAME rg COMMAND $<TARGET_FILE:edit>)
# file(COPY rg.txt DESTINATION .) # used to test rg.
# file(COPY rg2.txt DESTINATION .) # used to test rg.

# add_executable(uv uv.cpp)
# target_link_libraries(uv PRIVATE edtr)
# add_test(NAME uv COMMAND $<TARGET_FILE:uv>)
