/* Public domain CRC-32 (IEEE 802.3 / zlib polynomial 0xEDB88320). Table-driven, generated once
 * at first use (no static 1KB table baked into the binary). */
#include "crc32.h"

static uint32_t table[256];
static int table_ready = 0;

static void build_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
    if (!table_ready) build_table();
    const unsigned char *p = (const unsigned char *)data;
    /* Caller passes the running value already complemented (see crc32_update usage: start at 0,
     * we complement in/out internally per call so partial sums still compose correctly). */
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
