#ifndef CIVICLIGHT_HASH_H
#define CIVICLIGHT_HASH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fungsi hash berantai kustom CivicNet: BLAKE2s + XOR + Groestl
void civiclight_hash(const void* input, size_t len, void* output);

#ifdef __cplusplus
}
#endif

#endif // CIVICLIGHT_HASH_H

