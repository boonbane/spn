#include "sp.h"
#include "macro/macro.h"
#include "http.h"
#include "version/version.h"

#include <curl/curl.h>

sp_static_assert(spn_http_message_max >= CURL_ERROR_SIZE, message_holds_a_curl_error);

static size_t write_body(char* data, size_t size, size_t count, void* user) {
  u64 written = 0;
  sp_io_write((sp_io_writer_t*)user, data, size * count, &written);
  return written;
}

static int abort_if_cancelled(void* user, curl_off_t download_total, curl_off_t download_now, curl_off_t upload_total, curl_off_t upload_now) {
  return sp_atomic_s32_load((sp_atomic_s32_t*)user, SP_ATOMIC_SEQ_CST);
}

spn_err_t spn_http_init() {
  return curl_global_init(CURL_GLOBAL_DEFAULT) ? SPN_ERROR : SPN_OK;
}

spn_err_t spn_http_download(const spn_http_config_t* config, sp_str_t url, sp_path_t dest, spn_http_failure_t* failure) {
  *failure = (spn_http_failure_t) { .kind = SPN_HTTP_FAILED };

  CURL* curl = curl_easy_init();
  if (!curl) {
    sp_cstr_copy_to(curl_easy_strerror(CURLE_FAILED_INIT), failure->message, spn_http_message_max);
    return SPN_ERROR;
  }

  sp_io_file_writer_t file = sp_zero;
  if (sp_io_file_writer_from_path_at(&file, dest)) {
    curl_easy_cleanup(curl);
    failure->kind = SPN_HTTP_UNWRITABLE;
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  curl_easy_setopt(curl, CURLOPT_URL, sp_str_to_cstr(scratch.mem, url));
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "spn/" SPN_VERSION);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_QUICK_EXIT, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)config->timeout_s);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)config->timeout_s);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, failure->message);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file.base);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abort_if_cancelled);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, config->cancelled);
  if (!sp_str_empty(config->ca_bundle)) {
    c8* bundle = sp_str_to_cstr(scratch.mem, config->ca_bundle);
    curl_easy_setopt(curl, CURLOPT_CAINFO, bundle);
    curl_easy_setopt(curl, CURLOPT_PROXY_CAINFO, bundle);
  }

  long status = 0;
  CURLcode code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (code && !failure->message[0]) {
    sp_cstr_copy_to(curl_easy_strerror(code), failure->message, spn_http_message_max);
  }
  curl_easy_cleanup(curl);
  sp_mem_end_scratch(scratch);

  if (sp_io_file_writer_close(&file) && !code) {
    code = CURLE_WRITE_ERROR;
  }

  switch (code) {
    case CURLE_OK: {
      return SPN_OK;
    }
    case CURLE_HTTP_RETURNED_ERROR: {
      if (status >= 500 || status == 429) {
        failure->kind = SPN_HTTP_DROPPED;
      }
      break;
    }
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE: {
      failure->kind = SPN_HTTP_DROPPED;
      break;
    }
    case CURLE_ABORTED_BY_CALLBACK: {
      failure->kind = SPN_HTTP_CANCELLED;
      break;
    }
    case CURLE_WRITE_ERROR: {
      failure->kind = SPN_HTTP_UNWRITABLE;
      break;
    }
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE: {
      failure->kind = SPN_HTTP_UNTRUSTED;
      break;
    }
    default: {
      break;
    }
  }
  return SPN_ERROR;
}
