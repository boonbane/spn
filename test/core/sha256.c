#include "spn_test.h"

typedef struct {
  const c8* name;
  const c8* data;
  u64 fill;
  const c8* expect;
} test_t;

static const test_t tests [] = {
  { .name = "abc",                          .data = "abc",                                                      .expect = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
  { .name = "empty",                        .data = "",                                                         .expect = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
  { .name = "two_block_message",            .data = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", .expect = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
  { .name = "padding_fits_the_block",       .fill = 55,                                                         .expect = "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
  { .name = "padding_needs_a_second_block", .fill = 56,                                                         .expect = "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
  { .name = "exact_block",                  .fill = 64,                                                         .expect = "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
  { .name = "block_plus_one",               .fill = 65,                                                         .expect = "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
  { .name = "many_blocks",                  .fill = 1000000,                                                    .expect = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" },
};

sp_test_each(sha256, digest, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t data = sp_cstr_as_str(it->data);
  if (it->fill) {
    c8* bytes = (c8*)sp_alloc(mem, it->fill);
    sp_mem_fill_u8(bytes, it->fill, (u8)0x61);
    data = sp_str(bytes, (u32)it->fill);
  }
  sp_expect_str_eq_c(t, test_sha256_hex(mem, data), it->expect);

  return SP_OK;
}
