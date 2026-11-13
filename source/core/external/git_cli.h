#ifndef SPN_EXTERNAL_GIT_CLI_H
#define SPN_EXTERNAL_GIT_CLI_H

#include "sp.h"
#include "spn/core.h"
#include "external/git.h"

spn_err_t spn_git_cli_clone(sp_str_t url, sp_str_t path, spn_git_failure_t* failure);
spn_err_t spn_git_cli_fetch(sp_str_t repo, spn_git_fetch_t fetch, spn_git_failure_t* failure);
spn_err_t spn_git_cli_push(sp_mem_t mem, sp_str_t repo, sp_str_t url, sp_str_t refspec, sp_str_t* output);

#endif
