#include "compiler/stdinc.h"

spn_err_t si_cc_parse_search_list(sp_mem_t mem, sp_str_t content, sp_str_t builtin, si_da(sp_str_t)* args) {
  si_da_push(mem, *args, sp_str_lit("-nostdinc"));
  sp_str_t framework = sp_str_lit(" (framework directory)");
  bool listing = false;
  bool found = false;
  sp_str_for_split(content, '\n', line) {
    sp_str_t entry = sp_str_trim(line.value);
    if (!listing) {
      listing = sp_str_equal_cstr(entry, "#include <...> search starts here:");
      continue;
    }
    if (sp_str_equal_cstr(entry, "End of search list.")) {
      return SPN_OK;
    }
    if (sp_str_ends_with(entry, framework)) {
      si_da_push(mem, *args, sp_fmt(mem, "-iframework{}", sp_fmt_str(sp_str_strip_right(entry, framework))).value);
    }
    else if (!found && sp_str_equal(entry, builtin)) {
      found = true;
      (*args)[0] = sp_str_lit("-nostdlibinc");
    }
    else {
      si_da_push(mem, *args, sp_fmt(mem, "{}{}", sp_fmt_cstr(found ? "-idirafter" : "-isystem"), sp_fmt_str(entry)).value);
    }
  }
  return SPN_ERROR;
}
