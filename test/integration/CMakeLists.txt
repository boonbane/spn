add_executable(integration
  ${TEST_GEN_DIR}/probe.gen.c
  main.c
  cli.c
  compile_commands.c
  config.c
  consume.c
  corrupt.c
  cxx.c
  defines.c
  deps.c
  example.c
  exports.c
  freshness.c
  glob.c
  layout.c
  linker.c
  log.c
  offline.c
  options.c
  patch.c
  platform.c
  profile.c
  reexport.c
  script.c
  stage.c
  target.c
  units.c
  upstream.c
  warm.c
  ${TEST_TOOLS}/caps.c
  ${TEST_TOOLS}/lanes.c
  ${TEST_TOOLS}/env.c
  tools/harness.c
  ${TEST_TOOLS}/fixture.c
  ${TEST_GEN_DIR}/integration.embed.o
)
set_source_files_properties(${TEST_GEN_DIR}/integration.embed.o PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
spn_test_binary(integration)
target_include_directories(integration PRIVATE
  ${GEN_DIR}
  ${TEST_GEN_DIR}
  ${SRC}/external
  ${CMAKE_CURRENT_SOURCE_DIR}/tools
)
target_compile_definitions(integration PRIVATE
  SPN_TEST_BIN=".build/store/${TRIPLE}${SPN_FLAVOR}/bin/spn${CMAKE_EXECUTABLE_SUFFIX}"
  TEST_EMBED="integration.embed.h"
)
target_link_libraries(integration PRIVATE spn_core)
add_dependencies(integration spn spn_codegen_gen spn_codegen_test_gen integration_embed)
add_test(NAME integration COMMAND "$<TARGET_FILE:integration>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
