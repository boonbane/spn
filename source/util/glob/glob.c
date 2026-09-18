#include "glob/glob.h"

sp_glob_meta_t sp_glob_parse_meta(sp_str_t pattern) {
  sp_glob_meta_t shape = { .literal = true };
  u32 dir = 0;
  u32 rest = 0;
  u32 name = 0;
  u32 depth = 0;
  bool spans = false;
  sp_for(it, pattern.len) {
    switch (pattern.data[it]) {
      case '/': {
        if (shape.literal) {
          dir = it;
          rest = it + 1;
        } else {
          shape.deep = true;
        }
        if (depth) {
          spans = true;
        } else {
          name = it + 1;
          spans = false;
        }
        break;
      }
      case '*': {
        shape.deep = shape.deep || (it && pattern.data[it - 1] == '*');
        shape.literal = false;
        break;
      }
      case '{': {
        depth++;
        shape.literal = false;
        break;
      }
      case '}': {
        if (depth) {
          depth--;
        }
        break;
      }
      case '?':
      case '[': {
        shape.literal = false;
        break;
      }
      default: {
        break;
      }
    }
  }
  shape.dir = sp_str_prefix(pattern, (s32)dir);
  shape.rest = sp_str_suffix(pattern, (s32)(pattern.len - rest));
  shape.name = spans ? sp_str_lit("") : sp_str_suffix(pattern, (s32)(pattern.len - name));
  return shape;
}
