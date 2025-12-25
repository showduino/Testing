#pragma once

#include <stddef.h>
#include <stdint.h>

namespace showduino {

// Classic COBS encode/decode (no delimiter included).
// Encode output contains no 0x00 bytes; caller appends 0x00 delimiter for UART framing.

inline size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output, size_t out_max) {
  if (!input || !output || out_max == 0) return 0;

  size_t read_index = 0;
  size_t write_index = 1;
  size_t code_index = 0;
  uint8_t code = 1;

  output[0] = 0;  // placeholder

  while (read_index < length) {
    if (write_index >= out_max) return 0;
    if (input[read_index] == 0) {
      output[code_index] = code;
      code = 1;
      code_index = write_index++;
      read_index++;
    } else {
      output[write_index++] = input[read_index++];
      code++;
      if (code == 0xFF) {
        output[code_index] = code;
        code = 1;
        code_index = write_index++;
        if (write_index >= out_max) return 0;
      }
    }
  }

  if (code_index >= out_max) return 0;
  output[code_index] = code;
  return write_index;
}

inline size_t cobs_decode(const uint8_t *input, size_t length, uint8_t *output, size_t out_max) {
  if (!input || !output) return 0;
  size_t read_index = 0;
  size_t write_index = 0;

  while (read_index < length) {
    uint8_t code = input[read_index];
    if (code == 0) return 0;
    read_index++;

    for (uint8_t i = 1; i < code; i++) {
      if (read_index >= length) return 0;
      if (write_index >= out_max) return 0;
      output[write_index++] = input[read_index++];
    }

    if (code != 0xFF && read_index < length) {
      if (write_index >= out_max) return 0;
      output[write_index++] = 0;
    }
  }

  return write_index;
}

}  // namespace showduino

