#include "unit.h"
#include "fixture.h"

#include "toolchain/unpack.h"

#define max_entries 2

typedef struct {
  const c8* path;
  const c8* content;
  const c8* link;
  bool dir;
  bool executable;
} entry_t;

typedef struct {
  spn_err_t err;
  spn_unpack_reason_t reason;
  const c8* entry;
  entry_t entries [max_entries];
  const c8* absent;
} expect_t;

typedef struct {
  const c8* name;
  const c8* archive;
  bool symlinks;
  bool modes;
  bool drives;
  bool nul;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "contents_of_the_top_directory_land_in_root",
    .archive = "file.tar.xz",
    .expect = { .entries = { { .path = "B", .content = "B" } } },
  },
  {
    .name = "directory_entry_becomes_a_directory",
    .archive = "dir.tar.xz",
    .expect = { .entries = { { .path = "C", .dir = true } } },
  },
  {
    .name = "missing_parents_are_created",
    .archive = "orphan.tar.xz",
    .expect = { .entries = { { .path = "C/B", .content = "B" } } },
  },
  {
    .name = "executable_bit_is_kept",
    .archive = "executable.tar.xz",
    .modes = true,
    .expect = { .entries = { { .path = "B", .content = "B", .executable = true } } },
  },
  {
    .name = "symlink_keeps_its_target",
    .archive = "symlink.tar.xz",
    .symlinks = true,
    .expect = { .entries = { { .path = "D", .content = "B", .link = "B" } } },
  },
  {
    .name = "symlink_to_a_directory_resolves",
    .archive = "dirlink.tar.xz",
    .symlinks = true,
    .expect = { .entries = { { .path = "L/B", .content = "B" } } },
  },
  {
    .name = "hard_link_shares_content",
    .archive = "hardlink.tar.xz",
    .expect = { .entries = { { .path = "E", .content = "B" } } },
  },
  {
    .name = "dot_components_are_ignored",
    .archive = "dot.tar.xz",
    .expect = { .entries = { { .path = "B", .content = "B" } } },
  },
  {
    .name = "rooted_path_stays_under_root",
    .archive = "absolute.tar.xz",
    .expect = { .entries = { { .path = "B", .content = "B" } } },
  },
  {
    .name = "drive_letter_path_is_unsafe",
    .archive = "drive.tar.xz",
    .drives = true,
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/C:/B" },
  },
  {
    .name = "parent_component_is_unsafe",
    .archive = "dotdot.tar.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/../B", .absent = "B" },
  },
  {
    .name = "entry_under_a_symlink_is_unsafe",
    .archive = "through.tar.xz",
    .symlinks = true,
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/U", .absent = "B" },
  },
  {
    .name = "symlink_under_a_symlink_is_unsafe",
    .archive = "nested.tar.xz",
    .symlinks = true,
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/U", .absent = "U" },
  },
  {
    .name = "entry_replacing_a_symlink_is_unsafe",
    .archive = "onto.tar.xz",
    .symlinks = true,
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/O", .absent = "B" },
  },
  {
    .name = "entry_matching_a_symlink_up_to_a_nul_is_unsafe",
    .archive = "nul.tar.xz",
    .symlinks = true,
    .nul = true,
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/O", .absent = "B" },
  },
  {
    .name = "hard_link_target_obeys_the_same_rules",
    .archive = "outlink.tar.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSAFE, .entry = "A/E" },
  },
  {
    .name = "file_under_a_file_is_unwritable",
    .archive = "under.tar.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNWRITABLE, .entry = "A/B/C" },
  },
  {
    .name = "entries_beside_the_top_directory_are_skipped",
    .archive = "loose.tar.xz",
    .expect = { .absent = "R/B" },
  },
  {
    .name = "special_file_is_unsupported",
    .archive = "fifo.tar.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSUPPORTED },
  },
  {
    .name = "cut_off_content_is_corrupt",
    .archive = "cut.tar.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT, .entry = "A/B" },
  },
  {
    .name = "bytes_that_are_not_a_tar_are_corrupt",
    .archive = "bytes.xz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT },
  },
  {
    .name = "gzip_archive_unpacks",
    .archive = "file.tar.gz",
    .expect = { .entries = { { .path = "B", .content = "B" } } },
  },
  {
    .name = "gzip_without_its_trailer_is_corrupt",
    .archive = "trailer.tar.gz",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT },
  },
  {
    .name = "zip_archive_unpacks",
    .archive = "file.zip",
    .expect = { .entries = { { .path = "Z", .content = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB" } } },
  },
  {
    .name = "zip_directory_entry_becomes_a_directory",
    .archive = "dir.zip",
    .expect = { .entries = { { .path = "C", .dir = true } } },
  },
  {
    .name = "zip_executable_bit_is_kept",
    .archive = "executable.zip",
    .modes = true,
    .expect = { .entries = { { .path = "Y", .content = "B", .executable = true } } },
  },
  {
    .name = "zip_symlink_keeps_its_target",
    .archive = "symlink.zip",
    .symlinks = true,
    .expect = { .entries = { { .path = "D", .content = "B", .link = "B" } } },
  },
  {
    .name = "zip_content_failing_its_checksum_is_corrupt",
    .archive = "sum.zip",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT, .entry = "A/B" },
  },
  {
    .name = "zip_entry_contradicting_the_directory_is_corrupt",
    .archive = "header.zip",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT },
  },
  {
    .name = "zip_without_a_directory_is_corrupt",
    .archive = "cut.zip",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_CORRUPT },
  },
  {
    .name = "zip_compression_other_than_deflate_is_unsupported",
    .archive = "bzip2.zip",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNSUPPORTED },
  },
  {
    .name = "unknown_bytes_are_unrecognized",
    .archive = "bytes",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNRECOGNIZED },
  },
  {
    .name = "missing_archive_is_unreadable",
    .archive = "Z",
    .expect = { .err = SPN_ERROR, .reason = SPN_UNPACK_UNREADABLE },
  },
};

static bool executable(sp_path_t path) {
#if defined(SP_WIN32)
  return false;
#else
  sp_sys_file_meta_t meta = sp_zero;
  sp_sys_get_path_metadata_s(path.dir, path.sub, &meta);
  return (meta.perms.value & 0100) != 0;
#endif
}

sp_test_each(unpack, archives, test_t, tests) {
  if (it->symlinks) {
    sp_test_skip_without_symlinks();
  }
  if (it->modes) {
    sp_test_skip_on_win32();
  }
  if (it->drives) {
    sp_test_skip_on_linux();
    sp_test_skip_on_macos();
  }
  if (it->nul) {
    sp_test_skip_on_win32();
  }

  sp_mem_t mem = sp_test_arena(t);
  sp_path_t root = sp_path_join(mem, sp_test_dir(t), sp_str_lit("R"));
  sp_must_ok(t, sp_fs_create_dir_at(root));
  sp_path_t archive = sp_path_from_str(test_repo_path(mem, sp_test_format(t, "test/core/toolchain/archives/{}", sp_fmt_cstr(it->archive))));

  spn_unpack_failure_t failure = sp_zero;
  sp_expect_eq(t, (u32)it->expect.err, (u32)spn_artifact_unpack(mem, archive, root, &failure));
  sp_expect_eq(t, (u32)it->expect.reason, (u32)failure.reason);
  sp_expect_str_eq_c(t, failure.entry, it->expect.entry);

  u32 count = 0;
  sp_carr_detect_len(it->expect.entries, count, it->expect.entries[count].path);
  sp_for(at, count) {
    entry_t entry = it->expect.entries[at];
    sp_path_t path = sp_path_join(mem, root, sp_cstr_as_str(entry.path));
    sp_sys_file_meta_t meta = sp_zero;
    sp_expect_eq(t, entry.dir, !sp_fs_get_link_metadata_at(path, &meta) && meta.kind == SP_FS_KIND_DIR);
    if (entry.dir) {
      continue;
    }

    sp_str_t content = sp_zero;
    sp_expect_ok(t, sp_fs_read_at(mem, path, &content));
    sp_expect_str_eq_c(t, content, entry.content);
    sp_expect_eq(t, entry.executable, executable(path));

    c8 target [SP_PATH_MAX] = sp_zero;
    sp_str_t link = sp_zero;
    sp_sys_readlink_s(path.dir, path.sub, target, sizeof(target), &link);
    sp_expect_str_eq_c(t, link, entry.link);
  }

  if (it->expect.absent) {
    sp_sys_file_meta_t meta = sp_zero;
    sp_expect_err_eq(t, sp_fs_get_path_metadata_at(sp_path_join(mem, sp_test_dir(t), sp_cstr_as_str(it->expect.absent)), &meta), SP_ERR_SYS_NOT_FOUND);
  }
  return SP_OK;
}
