/* inflate.h -- self-contained DEFLATE (RFC1951) + gzip (RFC1952).
 *
 * WHY WE ROLL OUR OWN:
 * SteamOS ships libz.so but NOT zlib.h. The port must read the game's own
 * gzip-wrapped CMPR containers, and it must not depend on dev headers that
 * do not exist on the target. So: 300 lines of decoder, zero dependencies.
 */
#ifndef CMR2_INFLATE_H
#define CMR2_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* Inflate a whole gzip stream. Returns malloc'd buffer, or NULL.
 * On success *outlen is the uncompressed size. Caller frees. */
uint8_t *gunzip_alloc(const uint8_t *in, size_t inlen, size_t *outlen);

/* Raw DEFLATE, no gzip wrapper. */
uint8_t *inflate_alloc(const uint8_t *in, size_t inlen, size_t *outlen);

#endif
