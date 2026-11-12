add_executable(resolver
  dump.c
  ${TEST_GEN_DIR}/resolve.gen.c
  main.c
  ${TEST_TOOLS}/fixture.c
  ${TEST_GEN_DIR}/resolver.embed.o
)
set_source_files_properties(${TEST_GEN_DIR}/resolver.embed.o PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
spn_test_binary(resolver)
target_include_directories(resolver PRIVATE ${GEN_DIR} ${TEST_GEN_DIR})
target_compile_definitions(resolver PRIVATE TEST_EMBED="resolver.embed.h")
target_link_libraries(resolver PRIVATE spn_core)
add_dependencies(resolver spn_codegen_gen spn_codegen_test_gen resolver_embed)
add_test(NAME resolver COMMAND "$<TARGET_FILE:resolver>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
