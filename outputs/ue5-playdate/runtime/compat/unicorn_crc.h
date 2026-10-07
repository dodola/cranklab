#pragma once
/* Function-like macro keeps QEMU's DEF_HELPER/HELPER(crc32) identifiers intact.
   Only the actual conflicting CRC function/calls receive a private symbol. */
#define crc32(crc, data, length) crank_unicorn_crc32(crc, data, length)
