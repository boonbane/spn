#include "graph/identity.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "graph/build.h"
#include "paths/paths.h"
#include "session/types.h"
#include "hash/digest/digest.h"
#include "unit/package.h"
#include "unit/unit.h"

static void identity_hash_pin(spn_digest_ctx_t* ctx, const spn_build_source_pin_t* pin) {
  spn_dag_hash_u8(ctx, (u8)pin->kind);
  spn_dag_hash_str(ctx, pin->rev);
  spn_dag_hash_str(ctx, pin->dir);
  spn_dag_hash_u64(ctx, pin->patches);
}

spn_build_source_pin_t spn_build_source_pin(spn_pkg_unit_t* unit) {
  spn_build_source_pin_t pin = sp_zero;
  if (!unit->session) {
    return pin;
  }
  spn_resolved_pkg_t* resolved = sp_ht_getp(unit->session->resolve, unit->id.pkg);
  if (!resolved || resolved->origin.source.kind != SPN_PKG_ROOT_GIT) {
    return pin;
  }
  pin.kind = resolved->origin.source.kind;
  pin.rev = resolved->origin.source.git.rev;
  pin.dir = resolved->origin.source.git.dir;
  pin.patches = resolved->origin.source.git.patches.hash;
  return pin;
}

spn_dag_digest_t spn_build_tree_identity(spn_pkg_unit_t* unit, const spn_build_source_pin_t* pin) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.tree.v11"));
  spn_dag_hash_str(&ctx, unit->info->qualified);
  identity_hash_pin(&ctx, pin);

  spn_pkg_unit_header_maps_t published = spn_pkg_unit_header_maps(unit);
  sp_for(mt, published.count) {
    sp_om_for(published.maps[mt], it) {
      spn_dag_hash_paths(&ctx, sp_str_om_at(published.maps[mt], it)->headers);
    }
  }

  sp_da(spn_publish_copy_t) copies = unit->info->publish.copy;
  sp_da_for(copies, it) {
    spn_dag_hash_u8(&ctx, (u8)copies[it].tree);
    spn_dag_hash_str(&ctx, copies[it].pattern);
    spn_dag_hash_str(&ctx, copies[it].dest);
  }

  return spn_dag_hash_final(&ctx);
}

spn_dag_digest_t hash_user_node(spn_user_node_t* node, const spn_build_source_pin_t* pin) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.user.v8"));
  spn_dag_hash_str(&ctx, node->pkg->info->qualified);
  spn_dag_hash_u64(&ctx, node->pkg->fingerprint);
  identity_hash_pin(&ctx, pin);
  spn_dag_hash_str(&ctx, node->tag);
  spn_dag_hash_str(&ctx, node->fn);
  spn_dag_hash_paths(&ctx, node->inputs);
  spn_dag_hash_u64(&ctx, sp_da_size(node->outputs));
  sp_da_for(node->outputs, it) {
    spn_dag_hash_u64(&ctx, node->outputs[it].dir);
    spn_dag_hash_str(&ctx, node->outputs[it].sub);
    spn_dag_hash_u64(&ctx, node->outputs[it].kind);
  }

  return spn_dag_hash_final(&ctx);
}

static void hash_invocation(spn_digest_ctx_t* ctx, sp_hash_t toolchain, const spn_invocation_t* invocation) {
  spn_dag_hash_u64(ctx, toolchain);
  spn_dag_hash_arg(ctx, invocation->program);
  spn_dag_hash_path(ctx, invocation->cwd);
  spn_dag_hash_args(ctx, invocation->args);
  spn_dag_hash_u64(ctx, sp_da_size(invocation->env));
  sp_da_for(invocation->env, it) {
    spn_dag_hash_u64(ctx, invocation->env[it].key);
    spn_dag_hash_args(ctx, invocation->env[it].values);
  }
}

spn_dag_digest_t hash_compile_unit(const spn_compile_unit_t* unit) {
  sp_assert(!spn_arg_empty(unit->invocation.program));
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.compile.v6"));
  hash_invocation(&ctx, unit->target->pkg->build->toolchain->identity, &unit->invocation);
  spn_dag_hash_path(&ctx, unit->paths.file);
  return spn_dag_hash_final(&ctx);
}

spn_dag_digest_t hash_compile_commands(const spn_path_roots_t* roots, spn_pkg_unit_t* unit, sp_da(spn_compile_unit_t*) objects) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.compile_commands.v1"));
  spn_dag_hash_str(&ctx, unit->info->qualified);
  sp_for(it, SPN_PATH_ROOT_COUNT) {
    spn_dag_hash_str(&ctx, roots->dirs[it]);
  }
  spn_dag_hash_u64(&ctx, sp_da_size(objects));
  sp_da_for(objects, it) {
    spn_dag_hash_digest(&ctx, hash_compile_unit(objects[it]));
    spn_dag_hash_path(&ctx, objects[it]->paths.object);
  }
  return spn_dag_hash_final(&ctx);
}

spn_dag_digest_t hash_link(sp_hash_t toolchain, const spn_invocation_t* invocation) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.link.v6"));
  hash_invocation(&ctx, toolchain, invocation);
  return spn_dag_hash_final(&ctx);
}

spn_dag_digest_t hash_exports(sp_hash_t toolchain, spn_cc_exports_format_t format, sp_str_t name) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.exports.v5"));
  spn_dag_hash_u64(&ctx, toolchain);
  spn_dag_hash_u8(&ctx, (u8)format);
  spn_dag_hash_str(&ctx, name);
  return spn_dag_hash_final(&ctx);
}

spn_dag_digest_t hash_rsp(const spn_path_roots_t* roots, spn_rsp_style_t style, sp_da(spn_arg_t) args) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.rsp.v1"));
  spn_dag_hash_u8(&ctx, (u8)style);
  sp_for(it, SPN_PATH_ROOT_COUNT) {
    spn_dag_hash_str(&ctx, roots->dirs[it]);
  }
  spn_dag_hash_args(&ctx, args);
  return spn_dag_hash_final(&ctx);
}
