#include "pkg/pkg.h"

#include "intern/intern.h"
#include "paths/paths.h"
#include "target/mutate.h"
#include "when/when.h"

typedef struct {
  sp_mem_t mem;
  const spn_path_roots_t* roots;
  spn_tree_roots_t trees;
  spn_when_env_t* env;
} eval_ctx_t;

static si_da(sp_str_t) eval_list(eval_ctx_t* ctx, spn_gated_list_t gated) {
  si_da(sp_str_t) plain = SP_NULLPTR;
  si_da_for_ptr(gated, entry) {
    if (!spn_when_eval(&entry->when, ctx->env)) {
      continue;
    }
    si_da_push(ctx->mem, plain, entry->value);
  }
  return plain;
}

static si_da(spn_path_t) eval_paths(eval_ctx_t* ctx, spn_gated_path_list_t gated) {
  si_da(spn_path_t) plain = SP_NULLPTR;
  si_da_for_ptr(gated, entry) {
    if (!spn_when_eval(&entry->when, ctx->env)) {
      continue;
    }
    si_da_push(ctx->mem, plain, spn_tree_path(ctx->mem, ctx->roots, ctx->trees, entry->tree, entry->path));
  }
  return plain;
}

static si_da(spn_source_t) eval_sources(eval_ctx_t* ctx, si_da(spn_gated_source_t) gated) {
  si_da(spn_source_t) plain = SP_NULLPTR;
  si_da_for_ptr(gated, entry) {
    if (!spn_when_eval(&entry->when, ctx->env)) {
      continue;
    }
    si_da_push(ctx->mem, plain, ((spn_source_t) {
      .kind = entry->kind,
      .path = spn_tree_path(ctx->mem, ctx->roots, ctx->trees, entry->tree, entry->path),
    }));
  }
  return plain;
}

static si_da(spn_publish_t) eval_publish(eval_ctx_t* ctx, si_da(spn_gated_publish_t) gated) {
  si_da(spn_publish_t) plain = SP_NULLPTR;
  si_da_for_ptr(gated, entry) {
    if (!spn_when_eval(&entry->source.when, ctx->env)) {
      continue;
    }
    si_da_push(ctx->mem, plain, ((spn_publish_t) {
      .root = entry->root,
      .source = {
        .kind = entry->source.kind,
        .path = spn_tree_path(ctx->mem, ctx->roots, ctx->trees, entry->source.tree, entry->source.path),
      },
      .dest = entry->dest,
    }));
  }
  return plain;
}

static si_target_t eval_target(eval_ctx_t* ctx, const si_target_info_t* decl) {
  si_target_t target = {
    .info = decl,
    .source = eval_sources(ctx, decl->source),
    .publish = eval_publish(ctx, decl->publish),
    .include = eval_paths(ctx, decl->include),
    .define = eval_list(ctx, decl->define),
    .flags = eval_list(ctx, decl->flags),
    .link_flags = eval_list(ctx, decl->link_flags),
    .linker_script = eval_paths(ctx, decl->linker_script),
    .system_deps = eval_list(ctx, decl->system_deps),
    .deps = eval_list(ctx, decl->deps),
    .macos = { .frameworks = eval_list(ctx, decl->macos.frameworks) },
  };
  si_da_for_value(decl->embed, embed) {
    if (!spn_when_eval(&embed.when, ctx->env)) {
      continue;
    }
    spn_target_add_embed(ctx->mem, &target, (spn_embed_t) {
      .kind = embed.kind,
      .path = spn_tree_path(ctx->mem, ctx->roots, ctx->trees, embed.tree, embed.path),
      .dest = embed.dest,
      .types = embed.types,
    });
  }
  return target;
}

si_pkg_t spn_pkg_eval(sp_mem_t mem, const si_pkg_info_t* manifest, const spn_path_roots_t* roots, spn_tree_roots_t trees, spn_when_env_t* env) {
  eval_ctx_t ctx = { .mem = mem, .roots = roots, .trees = trees, .env = env };
  si_pkg_t pkg = {
    .manifest = manifest,
    .include = eval_paths(&ctx, manifest->include),
    .define = eval_list(&ctx, manifest->define),
    .system_deps = eval_list(&ctx, manifest->system_deps),
    .publish = eval_publish(&ctx, manifest->publish),
    .macos = { .frameworks = eval_list(&ctx, manifest->macos.frameworks) },
  };
  si_om_for(manifest->targets, it) {
    const si_target_info_t* decl = si_om_at(manifest->targets, it);
    si_om_insert(mem, pkg.targets, decl->id, eval_target(&ctx, decl));
  }
  si_om_for(manifest->options, it) {
    spn_option_info_t* option = si_om_at(manifest->options, it);
    if (sp_str_empty(option->define)) {
      continue;
    }
    spn_option_value_t* value = sp_str_ht_get(*env, option->name.str);
    if (!value || value->kind != SPN_OPTION_VALUE_BOOL || !value->b) {
      continue;
    }
    si_da_push(mem, pkg.define, option->define);
    if (option->public) {
      si_da_push(mem, pkg.public_define, option->define);
    }
  }
  return pkg;
}

si_target_t* spn_pkg_get_target(si_pkg_t* pkg, sp_str_t name, spn_target_kind_t kind) {
  si_target_id_t id = { .name = spn_intern(name).id, .kind = kind };
  return si_om_get(pkg->targets, id);
}

si_target_t* spn_pkg_get_script(si_pkg_t* pkg, spn_target_kind_t kind) {
  si_om_for_each(pkg->targets, target) {
    if (target->info->kind == kind && !si_da_empty(target->source)) {
      return target;
    }
  }
  return SP_NULLPTR;
}

bool spn_pkg_has_scripts(si_pkg_t* pkg) {
  return spn_pkg_get_script(pkg, SPN_TARGET_KIND_CONFIGURE_METAPROGRAM) || spn_pkg_get_script(pkg, SPN_TARGET_KIND_BUILD_METAPROGRAM);
}
