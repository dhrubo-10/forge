/* sha1.c — SHA-1 implementation
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Core of FORGE's content-addressed storage. Every blob, tree, and
 * commit is identified by the SHA-1 of its serialised form.
 * Based on Steve Reid's public domain implementation (1995).
 */
#include "sha1.h"
#include <string.h>

#define ROL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

#define BLK0(i) (block[i] = \
    (ROL32(block[i], 24) & 0xFF00FF00) | (ROL32(block[i], 8) & 0x00FF00FF))
#define BLK(i) (block[(i)&15] = ROL32( \
    block[((i)+13)&15] ^ block[((i)+8)&15] ^ block[((i)+2)&15] ^ block[(i)&15], 1))

#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+BLK0(i)+0x5A827999+ROL32(v,5); w=ROL32(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+BLK(i) +0x5A827999+ROL32(v,5); w=ROL32(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)              +BLK(i)+0x6ED9EBA1+ROL32(v,5); w=ROL32(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))   +BLK(i)+0x8F1BBCDC+ROL32(v,5); w=ROL32(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)              +BLK(i)+0xCA62C1D6+ROL32(v,5); w=ROL32(w,30);

static void sha1_transform(uint32_t state[5], const uint8_t buf[64])
{
    uint32_t a, b, c, d, e;
    uint32_t block[16];
    int i;

    for (i = 0; i < 16; i++)
        block[i] = ((uint32_t)buf[i*4+0] << 24) | ((uint32_t)buf[i*4+1] << 16)
                 | ((uint32_t)buf[i*4+2] <<  8) |  (uint32_t)buf[i*4+3];

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    a = b = c = d = e = 0;
}

void sha1_init(SHA1_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len)
{
    uint32_t i, j;
    j = ctx->count[0];
    if ((ctx->count[0] += (uint32_t)(len << 3)) < j) ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);
    j = (j >> 3) & 63;
    if ((j + len) > 63) {
        memcpy(&ctx->buf[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buf);
        for (; i + 63 < len; i += 64)
            sha1_transform(ctx->state, data + i);
        j = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buf[j], &data[i], len - i);
}

void sha1_final(uint8_t digest[20], SHA1_CTX *ctx)
{
    uint8_t finalcount[8];
    uint32_t i;
    for (i = 0; i < 8; i++)
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)]
                        >> ((3 - (i & 3)) * 8)) & 255);
    sha1_update(ctx, (const uint8_t *)"\200", 1);
    while ((ctx->count[0] & 504) != 448)
        sha1_update(ctx, (const uint8_t *)"\0", 1);
    sha1_update(ctx, finalcount, 8);
    for (i = 0; i < 20; i++)
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    memset(ctx, 0, sizeof(*ctx));
}

void sha1_hex(const uint8_t *sha1, char hex[41])
{
    static const char hc[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++) {
        hex[i * 2]     = hc[sha1[i] >> 4];
        hex[i * 2 + 1] = hc[sha1[i] & 0xf];
    }
    hex[40] = '\0';
}

void sha1_of_buf(const uint8_t *data, size_t len, uint8_t out[20])
{
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(out, &ctx);
}