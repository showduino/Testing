#pragma once

#include <stddef.h>
#include <stdint.h>

namespace showduino {

// Minimal Base64 (RFC 4648) encode/decode for chunk transport.
// Returns output length (0 on failure).

size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max);
size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max);

}  // namespace showduino

