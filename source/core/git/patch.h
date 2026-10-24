#ifndef SPN_GIT_PATCH_H
#define SPN_GIT_PATCH_H

#include "sp.h"

#include "git/types.h"

sp_hash_t spn_git_patch_hash(si_da(sp_str_t) files, u32* missing);

#endif
