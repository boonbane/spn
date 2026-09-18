#include "unit.h"

#include "glob/glob.h"

typedef struct {
  const c8* dir;
  const c8* rest;
  const c8* name;
  bool literal;
  bool deep;
} expect_t;

typedef struct {
  const c8* name;
  const c8* pattern;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "literal",              .pattern = "A.c",         .expect = { .rest = "A.c",                    .name = "A.c",     .literal = true } },
  { .name = "literal_in_dir",       .pattern = "A/B.c",       .expect = { .dir = "A",  .rest = "B.c",       .name = "B.c",     .literal = true } },
  { .name = "wild",                 .pattern = "*.c",         .expect = { .rest = "*.c",                    .name = "*.c" } },
  { .name = "wild_in_dir",          .pattern = "A/*.c",       .expect = { .dir = "A",  .rest = "*.c",       .name = "*.c" } },
  { .name = "dir_ends_before_meta", .pattern = "A/B?/C/*.c",  .expect = { .dir = "A",  .rest = "B?/C/*.c",  .name = "*.c",     .deep = true } },
  { .name = "wild_dir",             .pattern = "A/*/*.c",     .expect = { .dir = "A",  .rest = "*/*.c",     .name = "*.c",     .deep = true } },
  { .name = "recursive",            .pattern = "A/**/*.c",    .expect = { .dir = "A",  .rest = "**/*.c",    .name = "*.c",     .deep = true } },
  { .name = "recursive_tail",       .pattern = "A/**",        .expect = { .dir = "A",  .rest = "**",        .name = "**",      .deep = true } },
  { .name = "recursive_root",       .pattern = "**/*.c",      .expect = { .rest = "**/*.c",                 .name = "*.c",     .deep = true } },
  { .name = "class",                .pattern = "A/[a-z].c",   .expect = { .dir = "A",  .rest = "[a-z].c",   .name = "[a-z].c" } },
  { .name = "alternates",           .pattern = "A/{B,C}.c",   .expect = { .dir = "A",  .rest = "{B,C}.c",   .name = "{B,C}.c" } },
  { .name = "alternates_span_dirs", .pattern = "A/{B/C,D}.c", .expect = { .dir = "A",  .rest = "{B/C,D}.c",                    .deep = true } },
  { .name = "alternates_dir",       .pattern = "{A,B}/*.c",   .expect = { .rest = "{A,B}/*.c",              .name = "*.c",     .deep = true } },
  { .name = "absolute",             .pattern = "/A/*.c",      .expect = { .dir = "/A", .rest = "*.c",       .name = "*.c" } },
};

sp_test_each(glob, shape, test_t, tests) {
  sp_glob_meta_t shape = sp_glob_parse_meta(sp_cstr_as_str(it->pattern));
  sp_expect_str_eq_c(t, shape.dir, it->expect.dir);
  sp_expect_str_eq_c(t, shape.rest, it->expect.rest);
  sp_expect_str_eq_c(t, shape.name, it->expect.name);
  sp_expect_eq(t, it->expect.literal, shape.literal);
  sp_expect_eq(t, it->expect.deep, shape.deep);
  return SP_OK;
}
