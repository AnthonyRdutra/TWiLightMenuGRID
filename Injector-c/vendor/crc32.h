/* Public domain CRC-32 (IEEE 802.3 / zlib polynomial 0xEDB88320), the same variant
 * Python's zlib.crc32() uses -- matches assetbind/rom_hash.py's hash_file() output. */
#ifndef INJECTOR_CRC32_H
#define INJECTOR_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Streaming update: start with crc = 0, feed chunks in order, final value is ready to use
 * directly (already fully complemented in/out, unlike the raw textbook algorithm). */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
