#include "unit.h"
#include "hash_table/hash_table.h"

typedef struct {
  f32 x;
  f32 y;
  f32 z;
} vec3_t;

SPN_PACK_PUSH
typedef struct {
  u32 id;
  u8 kind;
} packed_key_t;
SPN_PACK_POP

sp_test(si_ht, zero_is_empty) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(s32, s32) ht = SP_NULLPTR;

  sp_must_eq(t, si_ht_size(ht), 0);
  sp_must(t, si_ht_empty(ht));
  sp_must(t, si_ht_getp(ht, 1) == SP_NULLPTR);

  u32 visited = 0;
  si_ht_for(ht, it) {
    visited++;
  }
  sp_must_eq(t, visited, 0);

  si_ht_erase(ht, 1);
  si_ht_clear(ht);
  si_ht_free(mem, ht);
  sp_must(t, ht == SP_NULLPTR);
  return SP_OK;
}

sp_test(si_ht, scalar_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(s32, f32) ht = SP_NULLPTR;

  si_ht_insert(mem, ht, 42, 3.14f);
  sp_must_eq(t, si_ht_size(ht), 1);
  sp_must(t, !si_ht_empty(ht));
  sp_must(t, si_ht_getp(ht, 42) != SP_NULLPTR);
  sp_must_eq(t, *si_ht_getp(ht, 42), 3.14f);

  si_ht_insert(mem, ht, 10, 1.5f);
  si_ht_insert(mem, ht, 20, 2.5f);
  si_ht_insert(mem, ht, 30, 3.5f);
  sp_must_eq(t, si_ht_size(ht), 4);
  sp_must_eq(t, *si_ht_getp(ht, 10), 1.5f);
  sp_must_eq(t, *si_ht_getp(ht, 20), 2.5f);
  sp_must_eq(t, *si_ht_getp(ht, 30), 3.5f);

  si_ht_insert(mem, ht, 42, 6.28f);
  sp_must_eq(t, *si_ht_getp(ht, 42), 6.28f);
  sp_must_eq(t, si_ht_size(ht), 4);

  si_ht_erase(ht, 20);
  sp_must(t, si_ht_getp(ht, 20) == SP_NULLPTR);
  sp_must_eq(t, si_ht_size(ht), 3);

  si_ht_clear(ht);
  sp_must_eq(t, si_ht_size(ht), 0);
  sp_must(t, si_ht_empty(ht));

  si_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, struct_value) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(s32, vec3_t) ht = SP_NULLPTR;

  si_ht_insert(mem, ht, 1, ((vec3_t) { 1.0f, 2.0f, 3.0f }));
  si_ht_insert(mem, ht, 2, ((vec3_t) { 4.0f, 5.0f, 6.0f }));
  si_ht_insert(mem, ht, 3, ((vec3_t) { 7.0f, 8.0f, 9.0f }));

  vec3_t* v = si_ht_getp(ht, 2);
  sp_must(t, v != SP_NULLPTR);
  sp_must_eq(t, v->x, 4.0f);
  sp_must_eq(t, v->y, 5.0f);
  sp_must_eq(t, v->z, 6.0f);

  v->x = 40.0f;
  sp_must_eq(t, si_ht_getp(ht, 2)->x, 40.0f);

  si_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, packed_struct_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(packed_key_t, const c8*) ht = SP_NULLPTR;

  si_ht_insert(mem, ht, ((packed_key_t) { 100, 1 }), "A");
  si_ht_insert(mem, ht, ((packed_key_t) { 200, 2 }), "B");
  si_ht_insert(mem, ht, ((packed_key_t) { 100, 2 }), "C");
  sp_must_eq(t, si_ht_size(ht), 3);

  const c8** found = si_ht_getp(ht, ((packed_key_t) { 200, 2 }));
  sp_must(t, found != SP_NULLPTR);
  sp_must(t, sp_cstr_equal(*found, "B"));

  found = si_ht_getp(ht, ((packed_key_t) { 100, 2 }));
  sp_must(t, found != SP_NULLPTR);
  sp_must(t, sp_cstr_equal(*found, "C"));

  sp_must(t, si_ht_getp(ht, ((packed_key_t) { 200, 1 })) == SP_NULLPTR);

  si_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, str_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_str_ht(s32) ht = SP_NULLPTR;

  si_str_ht_insert(mem, ht, sp_str_lit("A"), 1);
  si_str_ht_insert(mem, ht, sp_str_lit("B"), 2);
  si_str_ht_insert(mem, ht, sp_str_lit("C"), 3);
  sp_must_eq(t, si_str_ht_size(ht), 3);

  sp_str_t copy = sp_str_copy(mem, sp_str_lit("B"));
  s32* found = si_str_ht_get(ht, copy);
  sp_must(t, found != SP_NULLPTR);
  sp_must_eq(t, *found, 2);

  sp_must(t, si_str_ht_get(ht, sp_str_lit("D")) == SP_NULLPTR);

  si_str_ht_erase(ht, sp_str_lit("A"));
  sp_must(t, si_str_ht_get(ht, sp_str_lit("A")) == SP_NULLPTR);
  sp_must_eq(t, si_str_ht_size(ht), 2);

  si_str_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, cstr_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_cstr_ht(s32) ht = SP_NULLPTR;

  si_cstr_ht_insert(mem, ht, "A", 1);
  si_cstr_ht_insert(mem, ht, "B", 2);
  si_cstr_ht_insert(mem, ht, "C", 3);
  sp_must_eq(t, si_cstr_ht_size(ht), 3);

  c8 copy [] = "B";
  s32* found = si_cstr_ht_get(ht, (const c8*)copy);
  sp_must(t, found != SP_NULLPTR);
  sp_must_eq(t, *found, 2);

  sp_must(t, si_cstr_ht_get(ht, "D") == SP_NULLPTR);

  si_cstr_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, iteration) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(s32, s32) ht = SP_NULLPTR;

  sp_for(i, 50) {
    si_ht_insert(mem, ht, (s32)i, (s32)i * 10);
  }
  sp_must_eq(t, si_ht_size(ht), 50);

  s32 count = 0;
  s32 sum = 0;
  si_ht_for_kv(ht, it) {
    sp_must_eq(t, *it.val, *it.key * 10);
    count++;
    sum += *it.val;
  }
  sp_must_eq(t, count, 50);
  sp_must_eq(t, sum, 12250);

  count = 0;
  si_ht_for(ht, it) {
    sp_must_eq(t, *si_ht_it_getp(ht, it), *si_ht_it_getkp(ht, it) * 10);
    count++;
  }
  sp_must_eq(t, count, 50);

  si_ht_free(mem, ht);
  return SP_OK;
}

sp_test(si_ht, growth) {
  sp_mem_t mem = sp_test_arena(t);
  si_ht(u64, u64) ht = SP_NULLPTR;

  sp_for(i, 1000) {
    si_ht_insert(mem, ht, (u64)i, (u64)i * i);
  }
  sp_must_eq(t, si_ht_size(ht), 1000);

  sp_for(i, 1000) {
    u64* v = si_ht_getp(ht, (u64)i);
    sp_must(t, v != SP_NULLPTR);
    sp_must_eq(t, *v, (u64)i * i);
  }
  sp_must(t, si_ht_getp(ht, (u64)1000) == SP_NULLPTR);

  si_ht_free(mem, ht);
  return SP_OK;
}
