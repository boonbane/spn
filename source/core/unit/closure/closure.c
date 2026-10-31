#include "unit/closure/closure.h"

#include "unit/types.h"

si_closure_it_t si_closure_it(sp_mem_t mem, si_closure_shared_t shared) {
  return (si_closure_it_t) {
    .mem = mem,
    .shared = shared,
  };
}

void si_closure_it_push(si_closure_it_t* it, spn_target_unit_t* root) {
  si_ht_insert(it->mem, it->libs, root, (u8)true);
  si_ht_insert(it->mem, it->pkgs, root->pkg, (u8)true);
  si_closure_frame_t frame = { .lib = root, .root = true };
  si_da_push(it->mem, it->stack, frame);
}

bool si_closure_it_next(si_closure_it_t* it) {
  while (!si_da_empty(it->stack)) {
    u64 depth = si_da_size(it->stack);
    si_closure_frame_t* frame = si_da_back(it->stack);

    si_da(spn_pkg_edge_t) edges = sp_zero;
    si_da(spn_target_unit_t*) children = sp_zero;
    if (frame->lib) {
      bool walk = frame->root || it->shared == SI_CLOSURE_CHASE_SHARED;
      switch (frame->lib->lib_kind) {
        case SPN_LIB_KIND_STATIC:
        case SPN_LIB_KIND_SOURCE:
        case SPN_LIB_KIND_OBJECT: {
          walk = walk || !frame->lib->target->info->no_link;
          break;
        }
        case SPN_LIB_KIND_SHARED:
        case SPN_LIB_KIND_NONE: {
          break;
        }
      }
      if (frame->root) {
        edges = frame->lib->edges;
      }
      if (walk) {
        children = frame->lib->deps;
      }
    }
    else {
      bool walk = true;
      if (it->shared == SI_CLOSURE_STOP_AT_SHARED) {
        si_da_for_value(frame->pkg->libs, lib) {
          walk = walk && lib->lib_kind != SPN_LIB_KIND_SHARED;
        }
      }
      if (walk) {
        edges = frame->pkg->deps;
      }
      children = frame->pkg->libs;
    }

    while (frame->edge < si_da_size(edges)) {
      spn_pkg_edge_t* edge = &edges[frame->edge++];
      if (si_ht_getp(it->pkgs, edge->pkg)) {
        continue;
      }
      si_ht_insert(it->mem, it->pkgs, edge->pkg, (u8)true);
      si_closure_frame_t child = { .pkg = edge->pkg, .private = frame->private || edge->private };
      si_da_push(it->mem, it->stack, child);
      break;
    }
    if (si_da_size(it->stack) != depth) {
      continue;
    }

    while (frame->child < si_da_size(children)) {
      spn_target_unit_t* lib = children[frame->child++];
      if (si_ht_getp(it->libs, lib)) {
        continue;
      }
      si_ht_insert(it->mem, it->libs, lib, (u8)true);
      si_closure_frame_t child = { .lib = lib, .private = frame->private };
      si_da_push(it->mem, it->stack, child);
      break;
    }
    if (si_da_size(it->stack) != depth) {
      continue;
    }

    si_closure_frame_t done = *frame;
    si_da_pop(it->stack);
    if (done.root) {
      continue;
    }
    it->entry = (si_closure_entry_t) {
      .yield = done.lib ? SI_CLOSURE_YIELD_LIB : SI_CLOSURE_YIELD_PKG,
      .pkg = done.lib ? done.lib->pkg : done.pkg,
      .lib = done.lib,
      .private = done.private,
    };
    return true;
  }
  return false;
}
