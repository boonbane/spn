add_executable(fuzz_dag
  ${TEST_GEN_DIR}/fuzz.gen.c
  main.c
  cli.c
  generator.c
  journal.c
  lower.c
  model.c
  render.c
  trace.c
  ${TEST_TOOLS}/sim/sim.c
  ${TEST_TOOLS}/fuzz/fuzz.c
  ${TEST_TOOLS}/dag/track.c
)
spn_test_binary(fuzz_dag)
target_include_directories(fuzz_dag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GEN_DIR} ${TEST_GEN_DIR})
target_link_libraries(fuzz_dag PRIVATE spn_core)
add_dependencies(fuzz_dag spn_codegen_gen spn_codegen_test_gen)
add_test(NAME fuzz_dag COMMAND "$<TARGET_FILE:fuzz_dag>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
