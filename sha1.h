#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buf[64];
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void sha1_final(uint8_t digest[20], SHA1_CTX *ctx);

void sha1_hex(const uint8_t *sha1, char hex[41]);

void sha1_of_buf(const uint8_t *data, size_t len, uint8_t out[20]);

#endif 