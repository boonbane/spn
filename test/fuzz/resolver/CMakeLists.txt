add_executable(fuzz_resolver
  main.c
  generator.c
  oracles/exhaustive.c
  checkers/solve.c
  checkers/units.c
  dump.c
  ${TEST_TOOLS}/fuzz/fuzz.c
)
spn_test_binary(fuzz_resolver)
target_include_directories(fuzz_resolver PRIVATE ${GEN_DIR} ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/test/core/tools ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fuzz_resolver PRIVATE spn_core)
add_dependencies(fuzz_resolver spn_codegen_gen)
add_test(NAME fuzz_resolver COMMAND "$<TARGET_FILE:fuzz_resolver>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
