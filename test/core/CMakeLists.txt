add_executable(core
  tools/ctx.c
  tools/elf_emit.c
  tools/wasm_emit.c
  tools/main/unit.c
  drain.c
  elf.c
  fs.c
  copy.c
  wake.c
  sim.c
  intern.c
  lazy.c
  stable_map.c
  array.c
  hash_table.c
  ordered_map.c
  str.c
  triple.c
  ctx.c
  version.c
  when.c
  closure.c
  compiler/harness.c
  compiler/invocation.c
  compiler/compile.c
  compiler/depfile.c
  compiler/env.c
  compiler/exports/render.c
  compiler/exports/wasm.c
  compiler/flags.c
  compiler/link.c
  compiler/rsp/gnu.c
  compiler/rsp/style.c
  compiler/rsp/windows.c
  compiler/run.c
  compiler/toc.c
  config/config.c
  external/obj.c
  external/wasm.c
  gen/lower.c
  gen/reader.c
  gen/union.c
  git/fixture.c
  git/cache.c
  git/key.c
  git/patch.c
  git/url.c
  index/cache.c
  index/dir.c
  index/location.c
  index/package.c
  index/query.c
  index/publish.c
  index/release.c
  index/sync.c
  index/transaction.c
  install/harness.c
  install/plan.c
  install/render.c
  install/report.c
  install/resolve.c
  install/shim.c
  jtd/harness.c
  jtd/conformance.c
  jtd/discriminator.c
  jtd/document.c
  jtd/elements.c
  jtd/enums.c
  jtd/metadata.c
  jtd/names.c
  jtd/properties.c
  jtd/ref.c
  jtd/type.c
  jtd/values.c
  jtd/walk.c
  lock/lock.c
  manifest/gen/gen.c
  manifest/lower/lower.c
  options/merge.c
  pkg/eval.c
  pkg/load.c
  options/session.c
  paths/anchor.c
  paths/canonicalize.c
  paths/cwd.c
  paths/head.c
  paths/label.c
  paths/normal.c
  paths/path.c
  paths/resolve.c
  paths/roots.c
  paths/set.c
  paths/stage.c
  paths/tree_path.c
  paths/tree_rel.c
  profile/profile.c
  publish/publish.c
  queue/ops.c
  queue/smoke.c
  reflect/read.c
  reflect/walk.c
  schema/schema.c
  thread_pool/ops.c
  thread_pool/smoke.c
  toml/harness.c
  toml/roundtrip.c
  toml/parse.c
  toml/find.c
  toml/set.c
  toolchain/builtins.c
  toolchain/catalog.c
  toolchain/driver.c
  toolchain/linker.c
  toolchain/parse.c
  toolchain/probe.c
  toolchain/program.c
  toolchain/provision.c
  toolchain/sdk.c
  toolchain/sdk_detect.c
  toolchain/sdk_hash.c
  toolchain/sdk_libc.c
  toolchain/sdk_msvc.c
  toolchain/sdk_path.c
  toolchain/sdk_root.c
  toolchain/sdk_serve.c
  toolchain/sdk_wasi.c
  toolchain/search.c
  toolchain/select.c
  toolchain/sha256.c
  toolchain/toolchain.c
  toolchain/zig.c
  unit/harness.c
  unit/compile.c
  unit/edges.c
  unit/emit.c
  unit/errors.c
  unit/fingerprint.c
  unit/includes.c
  unit/link.c
  unit/lower.c
  unit/objects.c
  unit/paths.c
  unit/staged.c
  unit/walk.c
  zig/decode.c
  zig/stubs.c
  ${CMAKE_SOURCE_DIR}/tools/jtd.c
  ${CMAKE_SOURCE_DIR}/tools/gen/lower.c
  ${CMAKE_SOURCE_DIR}/tools/install/render.c
  ${CLI}/install/plan.c
  ${TEST_TOOLS}/fixture.c
  ${TEST_TOOLS}/path_roots.c
  ${TEST_TOOLS}/sim/sim.c
  ${EMBED_OBJ}
  ${CORE_EMBED_OBJ}
)
set_source_files_properties(${EMBED_OBJ} ${CORE_EMBED_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
spn_test_binary(core)
target_include_directories(core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CLI}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools
  ${GEN_DIR}
  ${TEST_GEN_DIR}
  ${SRC}/external
  ${SOURCE_ROOT}/wamr/core/iwasm/include
  ${CMAKE_SOURCE_DIR}/tools
  ${CMAKE_SOURCE_DIR}/tools/gen
  ${CMAKE_SOURCE_DIR}/tools/install
  ${STORE}/include/sp
)
set_source_files_properties(schema/schema.c PROPERTIES COMPILE_DEFINITIONS
  "SCHEMA_DIR=\"source/core/codegen/schema\"")
set_source_files_properties(jtd/harness.c jtd/conformance.c PROPERTIES COMPILE_DEFINITIONS
  "JTD_TEST_JSON_DIR=\"test/core/jtd/json\"")
set_source_files_properties(manifest/lower/lower.c PROPERTIES COMPILE_DEFINITIONS
  "MANIFEST_DIR=\"test/core/manifest/lower/manifests\"")
set_source_files_properties(pkg/load.c PROPERTIES COMPILE_DEFINITIONS
  "PKG_MANIFEST_DIR=\"test/core/pkg/manifests\"")
set_source_files_properties(config/config.c PROPERTIES COMPILE_DEFINITIONS
  "CONFIG_DIR=\"test/core/config/configs\"")
set_source_files_properties(
  toolchain/builtins.c
  toolchain/catalog.c
  toolchain/driver.c
  toolchain/linker.c
  toolchain/parse.c
  toolchain/probe.c
  toolchain/program.c
  toolchain/sdk.c
  toolchain/sdk_detect.c
  toolchain/sdk_hash.c
  toolchain/sdk_libc.c
  toolchain/sdk_msvc.c
  toolchain/sdk_path.c
  toolchain/sdk_root.c
  toolchain/sdk_serve.c
  toolchain/sdk_wasi.c
  toolchain/search.c
  toolchain/provision.c
  toolchain/select.c
  toolchain/sha256.c
  toolchain/toolchain.c
  toolchain/zig.c
  PROPERTIES COMPILE_DEFINITIONS "TOOLCHAINS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/toolchain/toolchains\"")
target_link_libraries(core PRIVATE spn_core)
add_dependencies(core spn_embed core_embed spn_codegen_gen)
add_test(NAME core COMMAND "$<TARGET_FILE:core>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# DAG tests: dag/*.c run in both binaries; dag/sim needs the sim's deterministic mtimes, dag/os needs a real filesystem.
set(DAG_TEST_SOURCES
  ${TEST_TOOLS}/dag/track.c
  ${TEST_TOOLS}/sim/sim.c
  ${TEST_TOOLS}/path_roots.c
)
file(GLOB DAG_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dag/*.c)
file(GLOB DAG_SIM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dag/sim/*.c)
file(GLOB DAG_OS_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dag/os/*.c)
add_executable(dag ${DAG_TESTS} ${DAG_SIM_TESTS} ${DAG_TEST_SOURCES} tools/main/sim.c)
add_executable(dag_os ${DAG_TESTS} ${DAG_OS_TESTS} ${DAG_TEST_SOURCES} tools/wasm_emit.c tools/main/unit.c)
foreach(target dag dag_os)
  spn_test_binary(${target})
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${GEN_DIR}
    ${SRC}/external
    ${SOURCE_ROOT}/wamr/core/iwasm/include
    ${STORE}/include/sp
  )
  target_link_libraries(${target} PRIVATE spn_core)
  add_dependencies(${target} spn_codegen_gen)
  add_test(NAME ${target} COMMAND "$<TARGET_FILE:${target}>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()

add_executable(glob glob/shape.c tools/main/unit.c)
spn_test_binary(glob)
target_link_libraries(glob PRIVATE spn_core)
target_include_directories(glob PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools ${STORE}/include/sp)
add_test(NAME glob COMMAND "$<TARGET_FILE:glob>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_subdirectory(resolver)
