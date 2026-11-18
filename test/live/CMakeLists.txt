add_executable(live
  git.c
  http.c
  wait.c
  ${CMAKE_SOURCE_DIR}/test/core/tools/ctx.c
  ${CMAKE_SOURCE_DIR}/test/core/tools/main/live.c
  ${TEST_TOOLS}/fixture.c
  ${EMBED_OBJ}
  ${CORE_EMBED_OBJ}
)
set_source_files_properties(${EMBED_OBJ} ${CORE_EMBED_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
spn_test_binary(live)
target_include_directories(live PRIVATE
  ${CMAKE_SOURCE_DIR}/test/core
  ${CMAKE_SOURCE_DIR}/test/core/tools
  ${GEN_DIR}
  ${TEST_GEN_DIR}
  ${SRC}/external
  ${STORE}/include/sp
)
target_compile_definitions(live PRIVATE TEST_EMBED="core.embed.h")
target_link_libraries(live PRIVATE spn_core)
add_dependencies(live spn_embed core_embed spn_codegen_gen)
add_test(NAME live COMMAND "$<TARGET_FILE:live>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
