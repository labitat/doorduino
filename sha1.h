/* Public API for Steve Reid's public domain SHA-1 implementation */
/* This file is in the public domain */
#ifndef _SHA1_H
#define _SHA1_H

#define SHA1_DIGEST_LENGTH 20
#define SHA1_BLOCKSIZE 64

struct sha1_context {
    uint32_t state[5];
    uint16_t length;
    char     buf[SHA1_BLOCKSIZE];
};

#ifndef ALLINONE
void sha1_init(struct sha1_context *ctx);
void sha1_update(struct sha1_context *ctx, const char *data, size_t len);
void sha1_final(struct sha1_context *ctx, char out[SHA1_BLOCKSIZE]);
#endif

#endif
