add_executable(resolver
  dump.c
  ${TEST_GEN_DIR}/resolve.gen.c
  main.c
  ${TEST_TOOLS}/fixture.c
)
spn_test_binary(resolver)
target_include_directories(resolver PRIVATE ${GEN_DIR} ${TEST_GEN_DIR})
target_link_libraries(resolver PRIVATE spn_core)
add_dependencies(resolver spn_codegen_gen spn_codegen_test_gen)
add_test(NAME resolver COMMAND "$<TARGET_FILE:resolver>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
