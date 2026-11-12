#include "spn.h"

#include <fcntl.h>
#include <unistd.h>

static spn_err_t copy(spn_root_t from, spn_root_t to, const c8* path) {
  c8 content = 0;
  s32 in = openat(from, "X", O_RDONLY);
  s32 out = openat(to, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (read(in, &content, 1) != 1 || write(out, &content, 1) != 1) {
    return SPN_ERROR;
  }
  close(in);
  close(out);
  return SPN_OK;
}

SPN_EXPORT
spn_err_t n(spn_t* spn) {
  spn_root_t manifest = 0;
  spn_root_t source = 0;
  spn_root_t work = 0;
  spn_try(spn_get_root_manifest(spn, &manifest));
  spn_try(spn_get_root_source(spn, &source));
  spn_try(spn_get_root_work(spn, &work));
  spn_try(copy(source, work, "S"));
  spn_try(copy(manifest, work, "M"));
  return SPN_OK;
}
