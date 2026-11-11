add_executable(render
  main.c
  cases.c
  ${TEST_TOOLS}/caps.c
  ${TEST_TOOLS}/lanes.c
  ${TEST_TOOLS}/env.c
  ${TEST_TOOLS}/fixture.c
)
spn_test_binary(render)
target_include_directories(render PRIVATE
  ${GEN_DIR}
  ${SRC}/external
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(render PRIVATE
  SPN_TEST_BIN=".build/store/${TRIPLE}${SPN_FLAVOR}/bin/spn${CMAKE_EXECUTABLE_SUFFIX}"
)
target_link_libraries(render PRIVATE spn_core)
add_dependencies(render spn spn_codegen_gen)
add_test(NAME render COMMAND "$<TARGET_FILE:render>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
