#include "spn_test.h"

#include "toolchain/provision.h"

#define max_mirrors 4

typedef struct {
  const c8* name;
  const c8* url;
  const c8* listing;
  u64 seed;
  const c8* expect [max_mirrors + 1];
} test_t;

static const test_t tests [] = {
  {
    .name = "mirror_serves_the_artifact_file_name",
    .url = "https://O.example.com/x/T.tar.gz",
    .listing = "https://A.example.com/M\n",
    .expect = { "https://A.example.com/M/T.tar.gz?source=spn" },
  },
  {
    .name = "blank_lines_and_padding_are_skipped",
    .url = "https://O.example.com/x/T.tar.gz",
    .listing = "\n\n https://A.example.com/M/ \r\n\n",
    .expect = { "https://A.example.com/M/T.tar.gz?source=spn" },
  },
  {
    .name = "url_without_a_file_name_points_at_the_mirror_root",
    .url = "https://O.example.com/",
    .listing = "https://A.example.com/M\n",
    .expect = { "https://A.example.com/M/?source=spn" },
  },
  {
    .name = "empty_listing_has_no_mirrors",
    .url = "https://O.example.com/x/T.tar.gz",
    .listing = "",
  },
  {
    .name = "seed_orders_the_mirrors",
    .url = "https://O.example.com/x/T.tar.gz",
    .listing = "https://A.example.com\nhttps://B.example.com\nhttps://C.example.com\nhttps://D.example.com\n",
    .seed = 1,
    .expect = {
      "https://D.example.com/T.tar.gz?source=spn",
      "https://B.example.com/T.tar.gz?source=spn",
      "https://A.example.com/T.tar.gz?source=spn",
      "https://C.example.com/T.tar.gz?source=spn",
    },
  },
  {
    .name = "another_seed_reorders_the_mirrors",
    .url = "https://O.example.com/x/T.tar.gz",
    .listing = "https://A.example.com\nhttps://B.example.com\nhttps://C.example.com\nhttps://D.example.com\n",
    .seed = 2,
    .expect = {
      "https://D.example.com/T.tar.gz?source=spn",
      "https://A.example.com/T.tar.gz?source=spn",
      "https://B.example.com/T.tar.gz?source=spn",
      "https://C.example.com/T.tar.gz?source=spn",
    },
  },
};

sp_test_each(mirrors, urls, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_artifact_t artifact = { .url = sp_cstr_as_str(it->url) };

  sp_da(sp_str_t) urls = spn_artifact_mirror_urls(mem, artifact, sp_cstr_as_str(it->listing), it->seed);
  sp_must_strs_eq(t, urls, sp_da_size(urls), it->expect);

  return SP_OK;
}
