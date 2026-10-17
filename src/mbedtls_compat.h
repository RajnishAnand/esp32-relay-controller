#pragma once

#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <stddef.h>
#include <stdint.h>

namespace mbedcompat {

#if MBEDTLS_VERSION_NUMBER >= 0x03000000

inline int sha256(const uint8_t* in, size_t len, uint8_t out[32]) { return mbedtls_sha256(in, len, out, 0); }
inline int sha256Starts(mbedtls_sha256_context* c) { return mbedtls_sha256_starts(c, 0); }
inline int sha256Update(mbedtls_sha256_context* c, const uint8_t* in, size_t len) {
  return mbedtls_sha256_update(c, in, len);
}
inline int sha256Finish(mbedtls_sha256_context* c, uint8_t out[32]) { return mbedtls_sha256_finish(c, out); }

#else

inline int sha256(const uint8_t* in, size_t len, uint8_t out[32]) { return mbedtls_sha256_ret(in, len, out, 0); }
inline int sha256Starts(mbedtls_sha256_context* c) { return mbedtls_sha256_starts_ret(c, 0); }
inline int sha256Update(mbedtls_sha256_context* c, const uint8_t* in, size_t len) {
  return mbedtls_sha256_update_ret(c, in, len);
}
inline int sha256Finish(mbedtls_sha256_context* c, uint8_t out[32]) { return mbedtls_sha256_finish_ret(c, out); }

#endif

}  // namespace mbedcompat
