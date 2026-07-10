#include "crypto/civiclight_hash.h"
#include "crypto/sha256.h"
#include <string.h>

void civiclight_hash(const void* input, size_t len, void* output) {
    // Menambahkan [32] agar menjadi array penampung 256-bit hash yang valid
    uint8_t hash1[32];
    uint8_t hash2[32];

    // Tahap 1: Jalankan SHA256 pertama
    CSHA256().Write((const uint8_t*)input, len).Finalize(hash1);

    // Tahap 2: Penguncian Bitwise XOR (0x5A) kustom CivicNet
    for (int i = 0; i < 32; i++) {
        hash1[i] ^= 0x5A;
    }

    // Tahap 3: Finalisasi dengan SHA256 kedua
    CSHA256().Write(hash1, 32).Finalize(hash2);

    // Salin hasil akhir 32-byte ke memori output blok blockchain
    memcpy(output, hash2, 32);
}

