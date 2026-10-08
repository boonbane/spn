#include "unit.h"

#include "ordered_map.h"

SPN_PACK_PUSH
typedef struct {
  u32 name;
  u8 kind;
} packed_key_t;
SPN_PACK_POP

typedef struct {
  s32 x;
  s32 y;
} point_t;

sp_test(si_om, zero_is_empty) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  sp_must_eq(t, si_om_size(map), 0);
  sp_must(t, si_om_empty(map));
  sp_must(t, si_om_getp(map, 1) == SP_NULLPTR);
  sp_must(t, si_om_get(map, 1) == SP_NULLPTR);
  sp_must(t, !si_om_has(map, 1));

  u32 visited = 0;
  si_om_for(map, it) {
    visited++;
  }
  sp_must_eq(t, visited, 0);

  si_om_free(mem, map);
  sp_must(t, map.order == SP_NULLPTR);
  sp_must(t, map.index == SP_NULLPTR);
  return SP_OK;
}

sp_test(si_om, insert_and_get) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  si_om_insert(mem, map, 1, 10);
  si_om_insert(mem, map, 2, 20);
  si_om_insert(mem, map, 3, 30);

  sp_must_eq(t, si_om_size(map), 3);
  sp_must_eq(t, *si_om_get(map, 1), 10);
  sp_must_eq(t, *si_om_get(map, 2), 20);
  sp_must_eq(t, *si_om_get(map, 3), 30);
  sp_must(t, si_om_get(map, 4) == SP_NULLPTR);

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, insertion_order) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  si_om_insert(mem, map, 300, 30);
  si_om_insert(mem, map, 100, 10);
  si_om_insert(mem, map, 200, 20);

  sp_must_eq(t, *si_om_at(map, 0), 30);
  sp_must_eq(t, *si_om_at(map, 1), 10);
  sp_must_eq(t, *si_om_at(map, 2), 20);
  sp_must_eq(t, *si_om_back(map), 20);

  s32 expected [] = { 30, 10, 20 };
  si_om_for(map, it) {
    sp_must_eq(t, *si_om_at(map, it), expected[it]);
  }

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, has_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  sp_must(t, !si_om_has(map, 42));
  si_om_insert(mem, map, 42, 100);
  sp_must(t, si_om_has(map, 42));
  sp_must(t, !si_om_has(map, 99));

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, duplicate_insert_keeps_first) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  si_om_insert(mem, map, 7, 100);
  si_om_insert(mem, map, 7, 200);

  sp_must_eq(t, si_om_size(map), 1);
  sp_must_eq(t, *si_om_get(map, 7), 100);

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, stable_pointers) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, s32) map = sp_zero;

  si_om_insert(mem, map, 0, 100);
  s32* first = si_om_at(map, 0);
  s32* via_get = si_om_get(map, 0);
  sp_must(t, first == via_get);

  sp_for(i, 200) {
    si_om_insert(mem, map, (s32)i + 1, (s32)i + 1);
  }

  sp_must_eq(t, si_om_size(map), 201);
  sp_must_eq(t, *first, 100);
  sp_must(t, first == si_om_at(map, 0));
  sp_must(t, first == si_om_get(map, 0));

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, emplace) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(s32, point_t) map = sp_zero;

  point_t* p = SP_NULLPTR;
  si_om_emplace(mem, map, 5, p);
  sp_must(t, p != SP_NULLPTR);
  sp_must_eq(t, p->x, 0);
  sp_must_eq(t, p->y, 0);

  p->x = 3;
  p->y = 4;
  sp_must_eq(t, si_om_size(map), 1);
  sp_must_eq(t, si_om_get(map, 5)->x, 3);
  sp_must_eq(t, si_om_get(map, 5)->y, 4);
  sp_must(t, si_om_get(map, 5) == p);

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, packed_struct_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_om(packed_key_t, s32) map = sp_zero;

  si_om_insert(mem, map, ((packed_key_t) { 1, 0 }), 10);
  si_om_insert(mem, map, ((packed_key_t) { 1, 1 }), 11);
  si_om_insert(mem, map, ((packed_key_t) { 2, 0 }), 20);

  sp_must_eq(t, si_om_size(map), 3);
  sp_must_eq(t, *si_om_get(map, ((packed_key_t) { 1, 0 })), 10);
  sp_must_eq(t, *si_om_get(map, ((packed_key_t) { 1, 1 })), 11);
  sp_must_eq(t, *si_om_get(map, ((packed_key_t) { 2, 0 })), 20);
  sp_must(t, !si_om_has(map, ((packed_key_t) { 2, 1 })));

  si_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, str_key) {
  sp_mem_t mem = sp_test_arena(t);
  si_str_om(point_t) map = sp_zero;

  si_str_om_insert(mem, map, sp_str_lit("A"), ((point_t) { 0, 0 }));
  si_str_om_insert(mem, map, sp_str_lit("B"), ((point_t) { 1, 1 }));

  sp_must_eq(t, si_str_om_size(map), 2);

  sp_str_t copy = sp_str_copy(mem, sp_str_lit("B"));
  point_t* b = si_str_om_get(map, copy);
  sp_must(t, b != SP_NULLPTR);
  sp_must_eq(t, b->x, 1);
  sp_must_eq(t, b->y, 1);
  sp_must(t, b == si_str_om_at(map, 1));
  sp_must(t, si_str_om_get(map, sp_str_lit("D")) == SP_NULLPTR);

  point_t* c = SP_NULLPTR;
  si_str_om_emplace(mem, map, sp_str_lit("C"), c);
  c->x = 100;
  sp_must_eq(t, si_str_om_size(map), 3);
  sp_must_eq(t, si_str_om_get(map, sp_str_lit("C"))->x, 100);

  si_str_om_free(mem, map);
  return SP_OK;
}

sp_test(si_om, zeroed_struct_member) {
  sp_mem_t mem = sp_test_arena(t);
  struct {
    si_om(s32, s32) by_id;
    si_str_om(s32) by_name;
  } record = sp_zero;

  sp_must(t, si_om_empty(record.by_id));
  sp_must(t, si_str_om_empty(record.by_name));

  si_om_insert(mem, record.by_id, 1, 10);
  si_str_om_insert(mem, record.by_name, sp_str_lit("A"), 10);

  sp_must_eq(t, *si_om_get(record.by_id, 1), 10);
  sp_must_eq(t, *si_str_om_get(record.by_name, sp_str_lit("A")), 10);

  si_om_free(mem, record.by_id);
  si_str_om_free(mem, record.by_name);
  return SP_OK;
}
