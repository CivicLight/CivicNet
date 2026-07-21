#ifndef CIVICLIGHT_HASH_H
#define CIVICLIGHT_HASH_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// v1: original algorithm - SHA256 -> XOR(0x5A) -> SHA256
void civiclight_hash_v1(const void* input, size_t len, void* output);
// v2: SHA256 -> yespower -> XOR -> SHA256 (ASIC-resistant upgrade)
void civiclight_hash_v2(const void* input, size_t len, void* output);
#ifdef __cplusplus
}
#endif
#endif // CIVICLIGHT_HASH_H
