#include "harness.h"
#include "elf/elf.h"
#include "probe.gen.h"
#include "error/error.h"
#include "lock/lock.h"
#include "semver/convert.h"
#include "triple/triple.h"
#include "yyjson.h"

#define expect_path(t, fixture, path) expect_exists(t, fixture, path, true, __FILE__, __LINE__)
#define expect_no_path(t, fixture, path) expect_exists(t, fixture, path, false, __FILE__, __LINE__)

static spn_triple_t path_target(const c8* triple) {
  spn_triple_t target = test_host();
  if (triple) {
    spn_triple_t partial = sp_zero;
    sp_assert(spn_triple_parse(sp_cstr_as_str(triple), &partial) == SPN_OK);
    target = spn_triple_merge(target, partial);
  }
  return target;
}

static sp_str_t layout_path(sp_mem_t mem, const c8* triple, const c8* profile, sp_str_t rest) {
  sp_str_t path = sp_str_lit("build");
  if (triple) {
    path = sp_fs_join_path(mem, path, sp_cstr_as_str(triple));
  }
  path = sp_fs_join_path(mem, path, sp_cstr_as_str(profile));
  return sp_fs_join_path(mem, path, rest);
}

static sp_str_t store_path(sp_mem_t mem, path_t path, const c8* profile, sp_str_t rest) {
  return layout_path(mem, path.triple, profile, sp_fs_join_path(mem, sp_str_lit("store"), sp_fs_join_path(mem, sp_cstr_as_str(path.pkg), rest)));
}

sp_str_t path_render(fixture_t* fixture, path_t path) {
  sp_mem_t mem = fixture->mem;
  const c8* profile = path.profile ? path.profile : "debug";
  spn_triple_t target = path_target(path.triple);
  sp_str_t name = sp_cstr_as_str(path.name);
  sp_str_t rendered = sp_zero;
  switch (path.kind) {
    case PATH_NONE: {
      break;
    }
    case PATH_LITERAL: {
      rendered = name;
      break;
    }
    case PATH_EXE: {
      rendered = layout_path(mem, path.triple, profile, test_exe_name(mem, target, name));
      break;
    }
    case PATH_TEST_EXE: {
      rendered = layout_path(mem, path.triple, profile, sp_fs_join_path(mem, sp_str_lit("test"), test_exe_name(mem, target, name)));
      break;
    }
    case PATH_EXAMPLE_EXE: {
      rendered = layout_path(mem, path.triple, profile, sp_fs_join_path(mem, sp_str_lit("example"), test_exe_name(mem, target, name)));
      break;
    }
    case PATH_STAGED_LIB: {
      rendered = layout_path(mem, path.triple, profile, test_shared_lib_name(mem, target, name));
      break;
    }
    case PATH_TEST_LIB: {
      rendered = layout_path(mem, path.triple, profile, sp_fs_join_path(mem, sp_str_lit("test"), test_shared_lib_name(mem, target, name)));
      break;
    }
    case PATH_STORE: {
      sp_str_t rest = path.exe ? sp_fs_join_path(mem, sp_str_lit("bin"), test_exe_name(mem, target, name)) : name;
      rendered = store_path(mem, path, profile, rest);
      break;
    }
    case PATH_STATIC_LIB: {
      rendered = store_path(mem, path, profile, sp_fs_join_path(mem, sp_str_lit("lib"), test_static_lib_name(mem, target, name)));
      break;
    }
    case PATH_SHARED_LIB: {
      rendered = store_path(mem, path, profile, sp_fs_join_path(mem, sp_str_lit("lib"), test_shared_lib_name(mem, target, name)));
      break;
    }
    case PATH_WORK: {
      rendered = layout_path(mem, path.triple, profile, sp_fs_join_path(mem, sp_str_lit(".spn"), name));
      break;
    }
  }
  if (path.dir) {
    rendered = sp_fs_join_path(mem, sp_cstr_as_str(path.dir), rendered);
  }
  return rendered;
}

static sp_str_t at(fixture_t* fixture, path_t path) {
  return fixture_path(fixture, path_render(fixture, path));
}

static sp_str_t display_path(fixture_t* fixture, sp_str_t path) {
  sp_str_t relative = sp_str_strip_left(path, fixture->root);
  if (relative.len == path.len) {
    return path;
  }
  return sp_str_concat(fixture->mem, sp_str_lit("$test"), relative);
}

sp_err_t expect_exists(sp_test_t* t, fixture_t* fixture, sp_str_t path, bool expected, const c8* file, u32 line) {
  bool exists = sp_fs_exists(path);
  if (exists == expected) {
    return SP_OK;
  }

  sp_test_kv(t, "root", fixture->root);
  sp_test_kv(t, "path", display_path(fixture, path));
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_cstr_as_str(expected ? "path exists" : "path does not exist"),
    .actual = sp_cstr_as_str(exists ? "it exists" : "it does not"),
  });
  return SP_ERR;
}

static sp_err_t expect_no_interp(sp_test_t* t, fixture_t* fixture, sp_str_t path, const c8* file, u32 line) {
  sp_io_file_reader_t reader = sp_zero;
  sp_must_ok(t, sp_io_file_reader_from_path(&reader, path));
  sp_io_seeking_reader_t elf = sp_zero;
  sp_io_seeking_reader_from_file_reader(&elf, &reader);
  sp_str_t interp = sp_zero;
  spn_err_t err = spn_elf_interp(fixture->mem, &elf, &interp);
  sp_io_file_reader_close(&reader);
  if (!err && sp_str_empty(interp)) {
    return SP_OK;
  }

  sp_test_kv(t, "root", fixture->root);
  sp_test_kv(t, "path", display_path(fixture, path));
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_cstr_as_str("a static elf64 with no program interpreter"),
    .actual = err ? sp_cstr_as_str("not an elf64 image") : interp,
  });
  return SP_ERR;
}

static sp_err_t expect_elf_entry(sp_test_t* t, fixture_t* fixture, sp_str_t path, u64 expected, const c8* file, u32 line) {
  sp_io_file_reader_t reader = sp_zero;
  sp_must_ok(t, sp_io_file_reader_from_path(&reader, path));
  sp_io_seeking_reader_t elf = sp_zero;
  sp_io_seeking_reader_from_file_reader(&elf, &reader);
  u64 entry = 0;
  spn_err_t err = spn_elf_entry(&elf, &entry);
  sp_io_file_reader_close(&reader);
  if (!err && entry == expected) {
    return SP_OK;
  }

  sp_mem_t mem = fixture->mem;
  sp_test_kv(t, "root", fixture->root);
  sp_test_kv(t, "path", display_path(fixture, path));
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_fmt(mem, "an elf64 image with entry {:x}", sp_fmt_uint(expected)).value,
    .actual = err ? sp_cstr_as_str("not an elf64 image") : sp_fmt(mem, "entry {:x}", sp_fmt_uint(entry)).value,
  });
  return SP_ERR;
}

static sp_err_t expect_elf_no_symbol(sp_test_t* t, fixture_t* fixture, sp_str_t path, const c8* prefix, const c8* file, u32 line) {
  sp_io_file_reader_t reader = sp_zero;
  sp_must_ok(t, sp_io_file_reader_from_path(&reader, path));
  sp_io_seeking_reader_t elf = sp_zero;
  sp_io_seeking_reader_from_file_reader(&elf, &reader);
  bool defined = false;
  spn_err_t err = spn_elf_defines_prefix(&elf, sp_cstr_as_str(prefix), &defined);
  sp_io_file_reader_close(&reader);
  if (!err && !defined) {
    return SP_OK;
  }

  sp_mem_t mem = fixture->mem;
  sp_test_kv(t, "root", fixture->root);
  sp_test_kv(t, "path", display_path(fixture, path));
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_fmt(mem, "an elf64 image defining no symbol starting with {}", sp_fmt_cstr(prefix)).value,
    .actual = err ? sp_cstr_as_str("not an elf64 image with a symbol table") : sp_fmt(mem, "a symbol starting with {}", sp_fmt_cstr(prefix)).value,
  });
  return SP_ERR;
}

static yyjson_val* event_field(sp_mem_t mem, yyjson_val* obj, const c8* key) {
  sp_da(sp_str_t) parts = sp_str_split_c8(mem, sp_cstr_as_str(key), '.');
  sp_da_for(parts, it) {
    if (!obj) {
      return SP_NULLPTR;
    }
    obj = yyjson_obj_getn(obj, parts[it].data, parts[it].len);
  }
  return obj;
}

static bool event_matches(yyjson_val* line, const c8* event, const c8* key, const c8* value) {
  const c8* name = yyjson_get_str(yyjson_obj_get(line, "event"));
  if (!name || !sp_cstr_equal(name, event)) {
    return false;
  }
  if (!key) {
    return true;
  }

  sp_mem_t mem = sp_mem_get_scratch();
  yyjson_val* field = event_field(mem, line, key);
  if (!field) {
    field = event_field(mem, yyjson_obj_get(line, "data"), key);
  }

  if (yyjson_is_int(field)) {
    sp_str_t num = sp_fmt(sp_mem_get_scratch(), "{}", sp_fmt_int(yyjson_get_sint(field))).value;
    return sp_str_equal_cstr(num, value);
  }

  const c8* str = yyjson_get_str(field);
  return str && sp_cstr_equal(str, value);
}

static u32 count_events(fixture_t* fixture, spn_event_kind_t kind, const c8* key, const c8* value) {
  const c8* event = spn_event_name(kind);
  sp_mem_t mem = fixture->mem;

  u32 count = 0;
  sp_da(sp_str_t) lines = sp_str_split_c8(mem, fixture->events, '\n');
  sp_da_for(lines, it) {
    if (sp_str_empty(lines[it])) {
      continue;
    }

    yyjson_doc* doc = yyjson_read(lines[it].data, lines[it].len, 0);
    if (!doc) {
      continue;
    }

    if (event_matches(yyjson_doc_get_root(doc), event, key, value)) {
      count++;
    }
    yyjson_doc_free(doc);
  }
  return count;
}

static sp_err_t expect_event(sp_test_t* t, fixture_t* fixture, spn_event_kind_t kind, const c8* key, const c8* value, bool lane, bool expected, const c8* file, u32 line) {
  if (lane) {
    value = test_toolchain()->name;
  }
  bool found = count_events(fixture, kind, key, value) > 0;
  if (found == expected) {
    return SP_OK;
  }

  sp_mem_t mem = fixture->mem;
  sp_test_kv(t, "event", sp_cstr_as_str(spn_event_name(kind)));
  if (key) {
    sp_test_kv(t, "field", sp_fmt(mem, "{} = {}", sp_fmt_cstr(key), sp_fmt_cstr(value)).value);
  }
  sp_test_kv(t, "events", fixture->events);
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_cstr_as_str(expected ? "event logged" : "event not logged"),
    .actual = sp_cstr_as_str(found ? "it was logged" : "it was not"),
  });
  return SP_ERR;
}

static sp_err_t expect_result(sp_test_t* t, fixture_t* fixture, spn_err_t err, const c8* file, u32 line) {
  sp_mem_t mem = fixture->mem;

  sp_str_t actual = sp_zero;
  sp_da(sp_str_t) lines = sp_str_split_c8(mem, fixture->events, '\n');
  sp_da_for(lines, it) {
    if (sp_str_empty(lines[it])) {
      continue;
    }

    yyjson_doc* doc = yyjson_read(lines[it].data, lines[it].len, 0);
    if (!doc) {
      continue;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    const c8* name = yyjson_get_str(yyjson_obj_get(root, "event"));
    if (name && sp_cstr_equal(name, "result")) {
      const c8* code = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "data"), "err"));
      actual = code ? sp_test_format(t, "{}", sp_fmt_cstr(code)) : (sp_str_t) sp_zero;
    }
    yyjson_doc_free(doc);
  }

  if (!sp_str_empty(actual) && sp_str_equal(spn_err_to_str(err), actual)) {
    return SP_OK;
  }

  sp_test_kv(t, "events", fixture->events);
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = spn_err_to_str(err),
    .actual = sp_str_empty(actual) ? sp_str_lit("no result event") : actual,
  });
  return SP_ERR;
}

static sp_str_t join_args(sp_mem_t mem, const c8* const* args, u32 max) {
  sp_str_t list = sp_str_lit("");
  sp_for(it, max) {
    if (!args[it]) {
      break;
    }
    list = sp_fmt(mem, "{} {.quote}", sp_fmt_str(list), sp_fmt_str(sp_cstr_as_str(args[it]))).value;
  }
  return list;
}

typedef struct {
  yyjson_doc* doc;
  yyjson_val* entries;
  u32 count;
} compile_commands_t;

static compile_commands_t read_compile_commands(fixture_t* fixture) {
  sp_str_t content = test_read_file(fixture->mem, fixture_path(fixture, sp_str_lit("compile_commands.json")));
  compile_commands_t cc = { .doc = yyjson_read(content.data, content.len, 0) };
  cc.entries = cc.doc ? yyjson_doc_get_root(cc.doc) : SP_NULLPTR;
  cc.count = (u32)yyjson_arr_size(cc.entries);
  return cc;
}

static bool entry_has_arg(yyjson_val* entry, const c8* const* alternatives, u32 max) {
  yyjson_val* arguments = yyjson_obj_get(entry, "arguments");
  u32 num_args = (u32)yyjson_arr_size(arguments);
  sp_for(ai, num_args) {
    const c8* arg = yyjson_get_str(yyjson_arr_get(arguments, ai));
    if (!arg) {
      continue;
    }
    sp_for(alt, max) {
      if (!alternatives[alt]) {
        break;
      }
      if (sp_cstr_equal(arg, alternatives[alt])) {
        return true;
      }
    }
  }
  return false;
}

static bool entry_includes(yyjson_val* entry, sp_str_t dir) {
  yyjson_val* arguments = yyjson_obj_get(entry, "arguments");
  u32 num_args = (u32)yyjson_arr_size(arguments);
  sp_for(ai, num_args) {
    const c8* arg = yyjson_get_str(yyjson_arr_get(arguments, ai));
    if (!arg) {
      continue;
    }
    sp_str_t str = sp_cstr_as_str(arg);
    if (sp_str_starts_with(str, sp_str_lit("-I")) || sp_str_starts_with(str, sp_str_lit("/I"))) {
      if (sp_str_starts_with(sp_str_suffix(str, str.len - 2), dir)) {
        return true;
      }
    }
  }
  return false;
}

static sp_err_t expect_cc_arg(sp_test_t* t, fixture_t* fixture, const action_t* action, const c8* file, u32 line) {
  bool expected = action->kind == ACTION_VERIFY_CC_ARG;
  compile_commands_t cc = read_compile_commands(fixture);

  sp_str_t offender = sp_zero;
  sp_for(it, cc.count) {
    yyjson_val* entry = yyjson_arr_get(cc.entries, it);
    if (entry_has_arg(entry, action->verify_cc_arg, sp_carr_len(action->verify_cc_arg)) != expected) {
      const c8* source = yyjson_get_str(yyjson_obj_get(entry, "file"));
      offender = source ? sp_cstr_as_str(source) : sp_str_lit("(unknown)");
    }
  }
  bool ok = cc.count && sp_str_empty(offender);
  if (cc.doc) {
    yyjson_doc_free(cc.doc);
  }
  if (ok) {
    return SP_OK;
  }

  sp_test_kv(t, "args", join_args(fixture->mem, action->verify_cc_arg, sp_carr_len(action->verify_cc_arg)));
  if (offender.len) {
    sp_test_kv(t, "entry", offender);
  }
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(file),
    .line = line,
    .expected = sp_cstr_as_str(expected ? "compile argument present in every entry" : "compile argument absent from every entry"),
    .actual = sp_cstr_as_str(cc.count ? (expected ? "an entry is missing it" : "an entry has it") : "no compile entries"),
  });
  return SP_ERR;
}

static sp_err_t expect_command_cc(sp_test_t* t, fixture_t* fixture, command_cc_t expected) {
  sp_mem_t mem = fixture->mem;
  compile_commands_t cc = read_compile_commands(fixture);
  sp_str_t file = expected.file.kind == PATH_NONE ? sp_str_lit("") : at(fixture, expected.file);
  sp_str_t include = expected.include ? fixture_path(fixture, sp_cstr_as_str(expected.include)) : sp_str_lit("");

  u32 selected = 0;
  u32 matches = 0;
  sp_for(it, cc.count) {
    yyjson_val* entry = yyjson_arr_get(cc.entries, it);
    const c8* source = yyjson_get_str(yyjson_obj_get(entry, "file"));
    if (!sp_str_empty(file) && !(source && sp_str_equal_cstr(file, source))) {
      continue;
    }
    selected++;
    if (expected.args[0]) {
      matches += entry_has_arg(entry, expected.args, sp_carr_len(expected.args));
    }
    if (expected.include) {
      matches += entry_includes(entry, include);
    }
  }
  if (cc.doc) {
    yyjson_doc_free(cc.doc);
  }

  bool ok = selected && (expected.absent ? matches == 0 : matches > 0);
  if (ok) {
    return SP_OK;
  }

  sp_test_kv(t, "file", file);
  sp_test_kv(t, "args", join_args(mem, expected.args, sp_carr_len(expected.args)));
  sp_test_kv(t, "include", include);
  sp_test_record(t, (sp_test_failure_t) {
    .file = sp_cstr_as_str(__FILE__),
    .line = (u32)__LINE__,
    .expected = sp_cstr_as_str(expected.absent ? "no selected compile entry has the argument" : "a selected compile entry has the argument"),
    .actual = sp_cstr_as_str(!selected ? "no compile entries selected" : (expected.absent ? "an entry has it" : "no entry has it")),
  });
  return SP_ERR;
}

static sp_ps_output_t run_fixture_bin(fixture_t* fixture, sp_str_t path) {
  return sp_ps_run(fixture->mem, (sp_ps_config_t) {
    .command = path,
    .cwd = fixture->root,
    .env = {
      .extra = {
        { sp_str_lit("ASAN_OPTIONS"), sp_str_lit("abort_on_error=0:exitcode=1") },
        { sp_str_lit("UBSAN_OPTIONS"), sp_str_lit("halt_on_error=1:abort_on_error=0:exitcode=1") },
      },
    },
    .io = {
      .in.mode = SP_PS_IO_MODE_NULL,
      .err.mode = SP_PS_IO_MODE_REDIRECT,
    },
  });
}

static void expect_command_file(sp_test_t* t, fixture_t* fixture, command_file_t expected) {
  sp_str_t path = at(fixture, expected.file);
  sp_str_t content = test_read_file(fixture->mem, path);
  sp_test_kv(t, "path", path);
  sp_test_kv(t, "content", content);
  if (expected.content) {
    sp_test_kv(t, "expected", sp_cstr_as_str(expected.content));
    sp_expect(t, sp_str_equal(content, sp_cstr_as_str(expected.content)));
  }
  if (expected.json) {
    yyjson_doc* doc = yyjson_read(content.data, content.len, 0);
    sp_expect(t, doc != SP_NULLPTR);
    yyjson_doc_free(doc);
  }
}

static sp_err_t expect_locked(sp_test_t* t, fixture_t* fixture, spn_lock_file_t* lock, const c8* name, const c8* version, const c8* file, u32 line) {
  sp_str_t key = sp_cstr_as_str(name);
  spn_lock_entry_t* entry = sp_ht_getp(lock->entries, key);
  sp_test_kv(t, "package", key);
  if (!entry) {
    sp_test_record(t, (sp_test_failure_t) {
      .file = sp_cstr_as_str(file),
      .line = line,
      .expected = sp_cstr_as_str("package locked"),
      .actual = sp_cstr_as_str("it is not"),
    });
    return SP_ERR;
  }
  if (version) {
    sp_str_t actual = spn_semver_to_str(fixture->mem, entry->version);
    sp_test_kv(t, "version", actual);
    sp_expect(t, sp_str_equal_cstr(actual, version));
  }
  return SP_OK;
}

static spn_lock_file_t load_lock(sp_test_t* t, fixture_t* fixture) {
  sp_str_t path = fixture_path(fixture, sp_str_lit("spn.lock"));
  expect_path(t, fixture, path);
  spn_lock_file_t lock = sp_zero;
  spn_lock_file_init(fixture->mem, &lock);
  if (sp_fs_exists(path)) {
    lock = spn_lock_file_load(fixture->mem, sp_path_cwd(path), SP_NULLPTR);
  }
  return lock;
}

static void expect_lock(sp_test_t* t, fixture_t* fixture, const command_lock_t* locked) {
  spn_lock_file_t lock = load_lock(t, fixture);
  u32 num = 0;
  sp_for(it, SPN_TEST_MAX_PACKAGES) {
    if (!locked[it].name) {
      break;
    }
    num++;
    expect_locked(t, fixture, &lock, locked[it].name, locked[it].version, __FILE__, __LINE__);
  }
  sp_expect_eq(t, num, (u32)sp_ht_size(lock.entries));
}

static void expect_store(sp_test_t* t, fixture_t* fixture, const c8* pkg, u32 count) {
  sp_mem_t mem = fixture->mem;
  sp_str_t path = sp_fs_join_path(mem, fixture->paths.storage, sp_fmt(mem, "cache/store/{}", sp_fmt_cstr(pkg)).value);
  sp_da(sp_fs_entry_t) entries = sp_zero;
  sp_fs_collect(mem, path, &entries);
  u32 dirs = 0;
  sp_da_for(entries, et) {
    if (entries[et].kind == SP_FS_KIND_DIR) {
      dirs++;
    }
  }
  sp_test_kv(t, "path", path);
  sp_expect_eq(t, count, dirs);
}

sp_err_t test_when(sp_test_t* t, test_when_t when) {
  sp_str_t blocked = test_when_blocked(when);
  if (blocked.len) {
    return sp_test_skip(t, "{}", sp_fmt_str(blocked));
  }
  return SP_OK;
}

static sp_err_t begin_test(sp_test_t* t, fixture_t* fixture, test_when_t when) {
  sp_try(fixture_init(t, fixture));
  return test_when(t, when);
}

sp_err_t run_command(sp_test_t* t, fixture_t* fixture, command_test_t test) {
  if (test.project) {
    fixture->project = test.project;
    sp_try(prepare_test(t, fixture, test.project, test.copy));
  }
  sp_ps_output_t output = (test.human ? run_spn : run_spn_json)(t, fixture, test.args, test.env);
  sp_expect_eq(t, test.expect.rc, output.status.exit_code);

  sp_str_t streams = sp_str_concat(fixture->mem, output.out, output.err);
  sp_carr_for(test.expect.contains, it) {
    if (!test.expect.contains[it]) {
      break;
    }
    sp_test_kv(t, "needle", sp_cstr_as_str(test.expect.contains[it]));
    sp_test_kv(t, "output", streams);
    sp_expect(t, sp_str_contains(streams, sp_cstr_as_str(test.expect.contains[it])));
  }

  sp_carr_for(test.expect.excludes, it) {
    if (!test.expect.excludes[it]) {
      break;
    }
    sp_test_kv(t, "needle", sp_cstr_as_str(test.expect.excludes[it]));
    sp_test_kv(t, "output", streams);
    sp_expect(t, !sp_str_contains(streams, sp_cstr_as_str(test.expect.excludes[it])));
  }

  sp_carr_for(test.expect.files, it) {
    if (test.expect.files[it].file.kind == PATH_NONE) {
      break;
    }
    expect_command_file(t, fixture, test.expect.files[it]);
  }

  sp_carr_for(test.expect.events, it) {
    command_event_t expected = test.expect.events[it];
    if (!expected.event) {
      break;
    }
    expect_event(t, fixture, expected.event, expected.key, expected.value, expected.lane, !expected.absent, __FILE__, __LINE__);
  }

  if (test.expect.err) {
    expect_event(t, fixture, SPN_EVENT_ERR, "kind", sp_str_to_cstr(fixture->mem, spn_err_to_str(test.expect.err)), false, true, __FILE__, __LINE__);
  }

  sp_carr_for(test.expect.cc, it) {
    if (!test.expect.cc[it].args[0] && !test.expect.cc[it].include && test.expect.cc[it].file.kind == PATH_NONE) {
      break;
    }
    expect_command_cc(t, fixture, test.expect.cc[it]);
  }

  sp_carr_for(test.expect.exists, it) {
    if (test.expect.exists[it].kind == PATH_NONE) {
      break;
    }
    expect_path(t, fixture, at(fixture, test.expect.exists[it]));
  }

  sp_carr_for(test.expect.missing, it) {
    if (test.expect.missing[it].kind == PATH_NONE) {
      break;
    }
    expect_no_path(t, fixture, at(fixture, test.expect.missing[it]));
  }

  if (test.expect.locked[0].name) {
    expect_lock(t, fixture, test.expect.locked);
  }
  return SP_OK;
}

sp_err_t run_command_test(sp_test_t* t, command_test_t test) {
  fixture_t fixture = sp_zero;
  sp_try(begin_test(t, &fixture, test.when));
  if (!test.project) {
    sp_try(prepare_test(t, &fixture, SP_NULLPTR, SP_NULLPTR));
  }
  return run_command(t, &fixture, test);
}

static sp_err_t apply_rebuild_change(sp_test_t* t, fixture_t* fixture, rebuild_change_t change) {
  sp_carr_for(change.remove_files, it) {
    if (change.remove_files[it].kind == PATH_NONE) {
      break;
    }
    sp_str_t path = at(fixture, change.remove_files[it]);
    sp_fs_remove_file(path);
    expect_no_path(t, fixture, path);
  }

  sp_carr_for(change.moves, it) {
    if (!change.moves[it].from) {
      break;
    }
    sp_str_t from = fixture_path(fixture, sp_cstr_as_str(change.moves[it].from));
    sp_str_t to = fixture_path(fixture, sp_cstr_as_str(change.moves[it].to));
    sp_str_t content = test_read_file(fixture->mem, from);
    fixture_create(fixture, sp_cstr_as_str(change.moves[it].to), content);
    sp_fs_remove_file(from);
    expect_no_path(t, fixture, from);
    expect_path(t, fixture, to);
  }

  sp_carr_for(change.writes, it) {
    if (!change.writes[it].file) {
      break;
    }
    fixture_create(fixture, sp_cstr_as_str(change.writes[it].file), sp_cstr_as_str(change.writes[it].content));
  }

  sp_carr_for(change.remove_dirs, it) {
    if (!change.remove_dirs[it]) {
      break;
    }
    sp_str_t path = fixture_path(fixture, sp_cstr_as_str(change.remove_dirs[it]));
    sp_fs_remove_dir(path);
    expect_no_path(t, fixture, path);
  }
  return SP_OK;
}

sp_err_t run_rebuild_test(sp_test_t* t, rebuild_test_t test) {
  fixture_t fixture = sp_zero;
  sp_try(begin_test(t, &fixture, test.when));
  fixture.toolchain = test.toolchain;
  fixture.project = test.project;

  sp_try(prepare_test(t, &fixture, test.project, test.copy));
  sp_try(run_command(t, &fixture, test.first));

  sp_tm_epoch_t mtimes [SPN_TEST_MAX_WATCHES] = sp_zero;
  sp_carr_for(test.watches, it) {
    if (test.watches[it].file.kind == PATH_NONE) {
      break;
    }
    sp_str_t path = at(&fixture, test.watches[it].file);
    expect_path(t, &fixture, path);
    mtimes[it] = sp_fs_get_mod_time(path);
  }

  sp_carr_for(test.rebuilds, it) {
    if (!test.rebuilds[it].command.args[0]) {
      break;
    }
    sp_try(apply_rebuild_change(t, &fixture, test.rebuilds[it].change));
    sp_try(run_command(t, &fixture, test.rebuilds[it].command));
  }

  sp_carr_for(test.watches, it) {
    rebuild_watch_t watch = test.watches[it];
    if (watch.file.kind == PATH_NONE) {
      break;
    }
    sp_str_t path = at(&fixture, watch.file);
    sp_tm_epoch_t now = sp_fs_get_mod_time(path);
    bool unchanged = mtimes[it].s == now.s && mtimes[it].ns == now.ns;
    if (watch.mtime == REBUILD_MTIME_CHANGED) {
      sp_expect(t, !unchanged);
    }
  }
  return SP_OK;
}

static sp_str_t bare_expect_token(bare_expect_t expect) {
  switch (expect) {
    case BARE_EXPECT_RUNS: return sp_str_lit("runs");
    case BARE_EXPECT_NOT_LOADABLE: return sp_str_lit("not_loadable");
  }
  sp_unreachable_return(sp_str_lit("runs"));
}

static sp_err_t stage_bare_run(sp_test_t* t, fixture_t* fixture, action_t action) {
  sp_mem_t mem = fixture->mem;
  sp_str_t lane = sp_fs_join_path(mem, test_probes(), sp_cstr_as_str(test_toolchain()->name));
  sp_str_t dir = sp_fs_join_path(mem, lane, sp_test_get_name(t));
  sp_try(sp_fs_create_dir(dir));

  sp_str_t exe_name = sp_fs_get_name(path_render(fixture, action.bare.file));
  sp_try(sp_fs_copy_file_at(sp_path_cwd(at(fixture, action.bare.file)), sp_path_cwd(sp_fs_join_path(mem, dir, exe_name)), SP_FS_ATOMIC_REPLACE));
  sp_carr_for(action.bare.with, it) {
    if (action.bare.with[it].kind == PATH_NONE) {
      break;
    }
    sp_str_t name = sp_fs_get_name(path_render(fixture, action.bare.with[it]));
    sp_try(sp_fs_copy_file_at(sp_path_cwd(at(fixture, action.bare.with[it])), sp_path_cwd(sp_fs_join_path(mem, dir, name)), SP_FS_ATOMIC_REPLACE));
  }

  spn_cg_probe_t probe = { .exe = exe_name, .expect = bare_expect_token(action.bare.expect) };
  return sp_fs_create_file_str(sp_fs_join_path(mem, dir, sp_str_lit("probe.json")), spn_probe_write(mem, &probe));
}

static sp_str_t create_content(fixture_t* fixture, action_t action) {
  if (action.create.from) {
    sp_mem_t mem = fixture->mem;
    sp_str_t project = sp_fs_join_path(mem, fixture->paths.root, sp_cstr_as_str(fixture->project));
    return test_read_file(mem, sp_fs_join_path(mem, project, sp_cstr_as_str(action.create.from)));
  }
  return sp_cstr_as_str(action.create.content);
}

sp_err_t run_actions(sp_test_t* t, fixture_t* fixture, const action_t* actions) {
  sp_mem_t mem = fixture->mem;

  sp_for(it, SPN_TEST_MAX_ACTIONS) {
    action_t action = actions[it];
    if (action.kind == ACTION_NONE) {
      break;
    }

    sp_test_kv_clear(t, SP_NULLPTR);

    switch (action.kind) {
      case ACTION_NONE: {
        break;
      }
      case ACTION_CREATE_FILE: {
        fixture_create(fixture, sp_cstr_as_str(action.create.file), create_content(fixture, action));
        break;
      }
      case ACTION_RUN_BIN: {
        sp_str_t bin = at(fixture, action.bin.file);
        expect_path(t, fixture, bin);

        sp_ps_output_t output = run_fixture_bin(fixture, bin);

        sp_test_kv(t, "command", bin);
        sp_test_kv(t, "output", output.out);
        sp_expect_eq(t, action.bin.rc, output.status.exit_code);
        break;
      }
      case ACTION_STAGE_BARE_RUN: {
        sp_try(stage_bare_run(t, fixture, action));
        break;
      }
      case ACTION_VERIFY_EXISTS: {
        expect_path(t, fixture, at(fixture, action.exists));
        break;
      }
      case ACTION_VERIFY_NOT_EXISTS: {
        expect_no_path(t, fixture, at(fixture, action.exists));
        break;
      }
      case ACTION_VERIFY_NO_INTERP: {
        expect_no_interp(t, fixture, at(fixture, action.verify_no_interp), __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_ELF_ENTRY: {
        expect_elf_entry(t, fixture, at(fixture, action.verify_elf_entry.file), action.verify_elf_entry.entry, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_ELF_NO_SYMBOL: {
        expect_elf_no_symbol(t, fixture, at(fixture, action.verify_elf_no_symbol.file), action.verify_elf_no_symbol.prefix, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_FILE_CONTAINS:
      case ACTION_VERIFY_FILE_NOT_CONTAINS: {
        sp_str_t path = fixture_path(fixture, sp_cstr_as_str(action.verify_file_contains.file));
        sp_str_t content = test_read_file(mem, path);
        sp_str_t needle = sp_cstr_as_str(action.verify_file_contains.needle);
        sp_test_kv(t, "path", path);
        sp_test_kv(t, "needle", needle);
        sp_test_kv(t, "content", content);
        sp_expect_eq(t, action.kind == ACTION_VERIFY_FILE_CONTAINS, sp_str_contains(content, needle));
        break;
      }
      case ACTION_REMOVE_DIR: {
        sp_str_t path = fixture_path(fixture, sp_cstr_as_str(action.rm.dir));
        sp_fs_remove_dir(path);
        expect_no_path(t, fixture, path);
        break;
      }
      case ACTION_CLEAR_WARM: {
        sp_str_t caches = sp_fs_join_path(mem, fixture->paths.toolchain, sp_str_lit("external/zig/cache"));
        sp_fs_it_t walk = sp_fs_it_new(mem, caches);
        while (sp_fs_it_next(&walk)) {
          sp_fs_remove_file(sp_fs_join_path(mem, sp_fs_join_path(mem, caches, walk.entry.name), sp_str_lit("spn/generation")));
        }
        sp_fs_it_deinit(&walk);
        break;
      }
      case ACTION_RUN_CLI: {
        const c8* args [SPN_TEST_COMMAND_MAX_ARGS] = { action.cli.cmd };
        sp_carr_for(action.cli.args, at) {
          if (!action.cli.args[at]) {
            break;
          }
          args[at + 1] = action.cli.args[at];
        }
        fixture->path = action.cli.path;
        fixture->cwd = action.cli.cwd;
        sp_ps_output_t output = run_spn_json(t, fixture, args, action.cli.env);
        fixture->path = SP_NULLPTR;
        fixture->cwd = SP_NULLPTR;
        sp_expect_eq(t, action.cli.rc, output.status.exit_code);
        break;
      }
      case ACTION_VERIFY_CC_ARG:
      case ACTION_VERIFY_NO_CC_ARG: {
        expect_cc_arg(t, fixture, &action, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_EVENT:
      case ACTION_VERIFY_NO_EVENT: {
        expect_event(t, fixture, action.verify_event.event, action.verify_event.key, action.verify_event.value, action.verify_event.lane, action.kind == ACTION_VERIFY_EVENT, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_RESULT: {
        expect_result(t, fixture, action.verify_result.err, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_PKG_LOCKED: {
        spn_lock_file_t lock = load_lock(t, fixture);
        expect_locked(t, fixture, &lock, action.verify_locked.name, action.verify_locked.version, __FILE__, __LINE__);
        break;
      }
      case ACTION_VERIFY_STORE: {
        expect_store(t, fixture, action.verify_store.pkg, action.verify_store.count);
        break;
      }
    }
  }
  return SP_OK;
}

sp_err_t run_test(sp_test_t* t, test_t test) {
  fixture_t fixture = sp_zero;
  sp_try(begin_test(t, &fixture, test.when));
  fixture.toolchain = test.toolchain;
  fixture.project = test.project;

  bool runs = test_when_runs(&test.when);
  bool stages = !sp_str_empty(test_probes());
  u32 kept = 0;
  sp_carr_for(test.actions, it) {
    action_t action = test.actions[it];
    bool keep = true;
    switch (action.kind) {
      case ACTION_RUN_BIN: {
        keep = runs;
        break;
      }
      case ACTION_STAGE_BARE_RUN: {
        keep = stages;
        break;
      }
      default: {
        break;
      }
    }
    if (keep) {
      test.actions[kept++] = action;
    }
  }
  while (kept < SPN_TEST_MAX_ACTIONS) {
    test.actions[kept++] = sp_zero_s(action_t);
  }

  sp_try(prepare_test(t, &fixture, test.project, test.copy));
  if (test.config) {
    sp_try(fixture_config_append(t, &fixture, test.project, test.config));
  }
  return run_actions(t, &fixture, test.actions);
}

static bool opt_build_present(const opt_build_t* build) {
  return build->present || build->profile || build->manifest || build->target || build->alternate || build->expect.rc || build->expect.exists[0].kind || build->expect.cc[0].args[0];
}

static sp_err_t opt_set_manifest(sp_test_t* t, fixture_t* fixture, const c8* manifest) {
  sp_str_t from = fixture_path(fixture, sp_cstr_as_str(manifest));
  sp_str_t to = fixture_path(fixture, sp_str_lit("spn.toml"));
  sp_str_t content = test_read_file(fixture->mem, from);
  fixture_create(fixture, sp_str_lit("spn.toml"), content);
  sp_fs_remove_file(from);
  sp_must(t, !sp_fs_exists(from));
  sp_must(t, sp_fs_exists(to));
  return SP_OK;
}

sp_err_t run_opt_test(sp_test_t* t, opt_test_t test) {
  fixture_t fixture = sp_zero;
  sp_try(begin_test(t, &fixture, test.when));
  fixture.project = test.project;

  sp_try(prepare_test(t, &fixture, test.project, test.copy));

  const test_toolchain_t* toolchain = test_toolchain();
  u32 ran = 0;
  sp_str_t blocked = sp_zero;
  sp_carr_for(test.builds, it) {
    const opt_build_t* build = &test.builds[it];
    if (!opt_build_present(build)) {
      break;
    }

    const c8* target = build->target;
    if (build->alternate) {
      target = test_target_alternate();
      if (!target) {
        blocked = sp_fmt(fixture.mem, "{} has no cross target", sp_fmt_cstr(toolchain->name)).value;
        continue;
      }
    }

    test_when_t when = build->when;
    if (!when.target) {
      when.target = target;
    }
    blocked = test_when_blocked(when);
    if (blocked.len) {
      continue;
    }

    if (build->manifest) {
      sp_try(opt_set_manifest(t, &fixture, build->manifest));
    }

    command_test_t command = {
      .args = { "build" },
      .expect = build->expect,
    };
    u32 arg = 1;
    if (build->profile) {
      command.args[arg++] = "-p";
      command.args[arg++] = build->profile;
    }
    if (target) {
      command.args[arg++] = "--target";
      command.args[arg++] = target;
    }
    sp_try(run_command(t, &fixture, command));
    ran++;
  }

  if (!ran) {
    return sp_test_skip(t, "{}", sp_fmt_str(blocked));
  }
  return SP_OK;
}

sp_err_t run_identity_test(sp_test_t* t, identity_test_t test) {
  fixture_t fixture = sp_zero;
  sp_try(begin_test(t, &fixture, test.when));
  fixture.project = test.project;
  sp_try(prepare_test(t, &fixture, test.project, test.copy));

  sp_str_t events = sp_str_lit("");
  sp_carr_for(test.builds, bt) {
    if (!test.builds[bt][0]) {
      break;
    }
    sp_ps_output_t output = run_spn_json(t, &fixture, test.builds[bt], SP_NULLPTR);
    sp_expect_eq(t, 0, output.status.exit_code);
    events = sp_str_concat(fixture.mem, events, fixture.events);
  }
  fixture.events = events;

  if (test.expect.locked[0].name) {
    expect_lock(t, &fixture, test.expect.locked);
  }
  sp_carr_for(test.expect.store, it) {
    if (!test.expect.store[it].pkg) {
      break;
    }
    expect_store(t, &fixture, test.expect.store[it].pkg, test.expect.store[it].count);
  }
  sp_carr_for(test.expect.configured, it) {
    const identity_count_t* expected = &test.expect.configured[it];
    if (!expected->pkg) {
      break;
    }
    sp_str_t message = sp_fmt(fixture.mem, "{} configure", sp_fmt_cstr(expected->pkg)).value;
    sp_test_kv(t, "package", sp_cstr_as_str(expected->pkg));
    sp_expect_eq(t, expected->count, count_events(&fixture, SPN_EVENT_USER_LOG, "message", sp_str_to_cstr(fixture.mem, message)));
  }
  return SP_OK;
}
