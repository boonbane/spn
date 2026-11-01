#include "hash/digest/digest.h"

void spn_digest_init(spn_digest_ctx_t* ctx) {
  blake3_hasher_init(ctx);
}

void spn_digest_update(spn_digest_ctx_t* ctx, const void* data, u64 len) {
  blake3_hasher_update(ctx, data, len);
}

void spn_digest_final(spn_digest_ctx_t* ctx, u8 digest [32]) {
  blake3_hasher_finalize(ctx, digest, 32);
}

void spn_digest(const void* data, u64 len, u8 digest [32]) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init(&ctx);
  spn_digest_update(&ctx, data, len);
  spn_digest_final(&ctx, digest);
}

sp_str_t spn_digest_hex(sp_mem_t mem, const u8 digest [32]) {
  static const c8 hex [] = "0123456789abcdef";
  c8* buffer = (c8*)sp_alloc(mem, 64);
  sp_for(it, 32) {
    buffer[it * 2] = hex[digest[it] >> 4];
    buffer[it * 2 + 1] = hex[digest[it] & 0xf];
  }
  return sp_str(buffer, 64);
}

spn_err_t spn_digest_file(sp_path_t path, u8 digest [32], u64* size) {
  sp_io_file_reader_t reader = sp_zero;
  if (sp_io_file_reader_from_path_at(&reader, path)) {
    return SPN_ERROR;
  }

  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init(&ctx);

  *size = 0;
  u8 chunk [65536] = sp_zero;
  while (true) {
    u64 bytes_read = 0;
    sp_err_t err = sp_io_read(&reader.base, chunk, sizeof(chunk), &bytes_read);
    if (bytes_read) {
      spn_digest_update(&ctx, chunk, bytes_read);
      *size += bytes_read;
    }
    if (err == SP_ERR_IO_EOF) {
      break;
    }
    if (err) {
      sp_io_file_reader_close(&reader);
      return SPN_ERROR;
    }
    if (!bytes_read) {
      break;
    }
  }

  sp_io_file_reader_close(&reader);
  spn_digest_final(&ctx, digest);
  return SPN_OK;
}

sp_hash_t spn_digest_hash(const void* data, u64 len) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init(&ctx);
  spn_digest_update(&ctx, data, len);
  u8 bytes [sizeof(sp_hash_t)] = sp_zero;
  blake3_hasher_finalize(&ctx, bytes, sizeof(bytes));
  sp_hash_t hash = sp_zero;
  sp_mem_copy(&hash, bytes, sizeof(bytes));
  return hash;
}

sp_hash_t spn_digest_hash_str(sp_str_t str) {
  return spn_digest_hash(str.data, str.len);
}

sp_hash_t spn_digest_hash_combine(const sp_hash_t* parts, u64 count) {
  return spn_digest_hash(parts, count * sizeof(sp_hash_t));
}
