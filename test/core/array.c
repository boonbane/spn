#include "unit.h"

#include "array/array.h"

sp_test(si_da, zero_is_empty) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(s32) arr = SP_NULLPTR;

  sp_must_eq(t, si_da_size(arr), 0);
  sp_must_eq(t, si_da_capacity(arr), 0);
  sp_must(t, si_da_empty(arr));

  u32 visited = 0;
  si_da_for(arr, it) {
    visited++;
  }
  sp_must_eq(t, visited, 0);

  si_da_pop(arr);
  si_da_clear(arr);
  si_da_free(mem, arr);
  sp_must(t, arr == SP_NULLPTR);
  return SP_OK;
}

sp_test(si_da, push_and_index) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(s32) arr = SP_NULLPTR;

  si_da_push(mem, arr, 42);
  sp_must_eq(t, si_da_size(arr), 1);
  sp_must(t, si_da_capacity(arr) >= 1);
  sp_must(t, !si_da_empty(arr));
  sp_must_eq(t, arr[0], 42);

  sp_for(i, 9) {
    si_da_push(mem, arr, (s32)(i + 1) * 10);
  }
  sp_must_eq(t, si_da_size(arr), 10);
  sp_must_eq(t, arr[0], 42);
  sp_for(i, 9) {
    sp_must_eq(t, arr[i + 1], (s32)(i + 1) * 10);
  }

  si_da_pop(arr);
  sp_must_eq(t, si_da_size(arr), 9);
  sp_must_eq(t, *si_da_back(arr), 80);

  si_da_clear(arr);
  sp_must_eq(t, si_da_size(arr), 0);
  sp_must(t, si_da_empty(arr));

  si_da_free(mem, arr);
  return SP_OK;
}

sp_test(si_da, growth) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(u32) arr = SP_NULLPTR;

  u64 prev = 0;
  sp_for(i, 100) {
    si_da_push(mem, arr, i);
    u64 cap = si_da_capacity(arr);
    if (cap != prev) {
      if (prev) {
        sp_must_eq(t, cap, prev * 2);
      }
      prev = cap;
    }
  }

  sp_must_eq(t, si_da_size(arr), 100);
  si_da_for(arr, it) {
    sp_must_eq(t, arr[it], (u32)it);
  }

  si_da_free(mem, arr);
  return SP_OK;
}

sp_test(si_da, reserve) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(f32) arr = SP_NULLPTR;

  si_da_reserve(mem, arr, 100);
  sp_must(t, si_da_capacity(arr) >= 100);
  sp_must_eq(t, si_da_size(arr), 0);

  sp_for(i, 50) {
    si_da_push(mem, arr, (f32)i * 0.5f);
  }
  sp_must(t, si_da_capacity(arr) >= 100);
  sp_must_eq(t, si_da_size(arr), 50);
  sp_must_eq(t, arr[49], 24.5f);

  si_da_free(mem, arr);
  return SP_OK;
}

typedef struct {
  s32 id;
  f32 value;
  c8 name [32];
} element_t;

sp_test(si_da, struct_elements) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(element_t) arr = SP_NULLPTR;

  sp_for(i, 10) {
    element_t e = sp_zero;
    e.id = (s32)i;
    e.value = (f32)i * 1.5f;
    e.name[0] = (c8)('A' + i);
    si_da_push(mem, arr, e);
  }

  sp_must_eq(t, si_da_size(arr), 10);
  si_da_for(arr, it) {
    sp_must_eq(t, arr[it].id, (s32)it);
    sp_must_eq(t, arr[it].value, (f32)it * 1.5f);
    sp_must_eq(t, arr[it].name[0], (c8)('A' + it));
  }

  si_da_free(mem, arr);
  return SP_OK;
}

sp_test(si_da, sp_da_reads) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(s32) arr = SP_NULLPTR;

  sp_for(i, 20) {
    si_da_push(mem, arr, (s32)i);
  }

  sp_must_eq(t, sp_da_size(arr), 20);
  sp_must(t, !sp_da_empty(arr));
  sp_must_eq(t, *sp_da_back(arr), 19);
  s32 sum = 0;
  sp_da_for(arr, it) {
    sum += arr[it];
  }
  sp_must_eq(t, sum, 190);

  si_da_free(mem, arr);
  return SP_OK;
}

sp_test(si_da, zeroed_struct_member) {
  sp_mem_t mem = sp_test_arena(t);
  struct {
    si_da(s32) ids;
    si_da(sp_str_t) names;
  } record = sp_zero;

  sp_must(t, si_da_empty(record.ids));
  sp_must(t, si_da_empty(record.names));

  si_da_push(mem, record.ids, 1);
  si_da_push(mem, record.names, sp_str_lit("A"));

  sp_must_eq(t, si_da_size(record.ids), 1);
  sp_must_eq(t, si_da_size(record.names), 1);
  sp_must(t, sp_str_equal(record.names[0], sp_str_lit("A")));

  si_da_free(mem, record.ids);
  si_da_free(mem, record.names);
  return SP_OK;
}
