#include <showduino/base64.h>

namespace showduino {

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) {
  if (!out) return 0;
  size_t out_len = 0;
  for (size_t i = 0; i < in_len; i += 3) {
    uint32_t v = 0;
    size_t rem = in_len - i;
    v |= static_cast<uint32_t>(in[i]) << 16;
    if (rem > 1) v |= static_cast<uint32_t>(in[i + 1]) << 8;
    if (rem > 2) v |= static_cast<uint32_t>(in[i + 2]);

    char c0 = kB64[(v >> 18) & 0x3F];
    char c1 = kB64[(v >> 12) & 0x3F];
    char c2 = (rem > 1) ? kB64[(v >> 6) & 0x3F] : '=';
    char c3 = (rem > 2) ? kB64[v & 0x3F] : '=';

    if (out_len + 4 > out_max) return 0;
    out[out_len++] = c0;
    out[out_len++] = c1;
    out[out_len++] = c2;
    out[out_len++] = c3;
  }
  if (out_len < out_max) out[out_len] = '\0';
  return out_len;
}

static int8_t b64_val(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
  if (c >= 'a' && c <= 'z') return static_cast<int8_t>(26 + (c - 'a'));
  if (c >= '0' && c <= '9') return static_cast<int8_t>(52 + (c - '0'));
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) {
  if (!in || !out) return 0;
  size_t out_len = 0;
  uint32_t buf = 0;
  uint8_t buf_len = 0;
  uint8_t pad = 0;

  for (size_t i = 0; i < in_len; ++i) {
    char c = in[i];
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
    if (c == '=') {
      pad++;
      continue;
    }
    int8_t v = b64_val(c);
    if (v < 0) return 0;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    buf_len += 6;
    if (buf_len >= 24) {
      if (out_len + 3 > out_max) return 0;
      out[out_len++] = static_cast<uint8_t>((buf >> 16) & 0xFF);
      out[out_len++] = static_cast<uint8_t>((buf >> 8) & 0xFF);
      out[out_len++] = static_cast<uint8_t>(buf & 0xFF);
      buf = 0;
      buf_len = 0;
    }
  }

  if (pad) {
    // Handle padding by trimming output.
    if (pad > 2) return 0;
    if (out_len < pad) return 0;
    out_len -= pad;
  }
  return out_len;
}

}  // namespace showduino

