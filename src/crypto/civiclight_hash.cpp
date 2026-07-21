#include "crypto/civiclight_hash.h"
#include "crypto/sha256.h"
#include <string.h>

extern "C" {
#include "crypto/yespower/yespower.h"
}

// ============ v1: Original algorithm (pre-fork) ============
void civiclight_hash_v1(const void* input, size_t len, void* output) {
    uint8_t hash1[32];
    uint8_t hash2[32];
    CSHA256().Write((const uint8_t*)input, len).Finalize(hash1);
    for (int i = 0; i < 32; i++) {
        hash1[i] ^= 0x5A;
    }
    CSHA256().Write(hash1, 32).Finalize(hash2);
    memcpy(output, hash2, 32);
}

// ============ v2: ASIC-resistant algorithm (post-fork) ============
static thread_local yespower_local_t yespower_local;
static thread_local bool yespower_local_initialized = false;

void civiclight_hash_v2(const void* input, size_t len, void* output) {
    uint8_t hash1[32];
    uint8_t xor_buf[32];
    uint8_t hash2[32];

    CSHA256().Write((const uint8_t*)input, len).Finalize(hash1);

    if (!yespower_local_initialized) {
        yespower_init_local(&yespower_local);
        yespower_local_initialized = true;
    }

    yespower_params_t params;
    params.version = YESPOWER_1_0;
    params.N = 2048;
    params.r = 8;
    params.pers = nullptr;
    params.perslen = 0;

    yespower_binary_t yp_out;
    yespower(&yespower_local, hash1, 32, &params, &yp_out);

    for (int i = 0; i < 32; i++) {
        xor_buf[i] = yp_out.uc[i] ^ hash1[i];
    }

    CSHA256().Write(xor_buf, 32).Finalize(hash2);
    memcpy(output, hash2, 32);
}
