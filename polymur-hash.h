/*
    PolymurHash version 1.0

    Copyright (c) 2023 Orson Peters
    
    This software is provided 'as-is', without any express or implied warranty. In
    no event will the authors be held liable for any damages arising from the use of
    this software.
    
    Permission is granted to anyone to use this software for any purpose, including
    commercial applications, and to alter it and redistribute it freely, subject to
    the following restrictions:
    
    1. The origin of this software must not be misrepresented; you must not claim
        that you wrote the original software. If you use this software in a product,
        an acknowledgment in the product documentation would be appreciated but is
        not required.
    
    2. Altered source versions must be plainly marked as such, and must not be
        misrepresented as being the original software.
    
    3. This notice may not be removed or altered from any source distribution.
*/

#ifndef POLYMUR_HASH_H
#define POLYMUR_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if defined(_MSC_VER)
    #include <intrin.h>
    #ifdef _M_X64
        #pragma intrinsic(_umul128)
    #endif
#endif
    
// ---------- PolymurHash public API ----------
typedef struct {
    uint64_t k, k2, k7, s;
} PolymurHashParams;

// Expands a 64-bit or 128-bit seed to a set of parameters for hash evaluation.
static inline void polymur_init_params(PolymurHashParams* p, uint64_t k_seed, uint64_t s_seed);
static inline void polymur_init_params_from_seed(PolymurHashParams* p, uint64_t seed);

// Computes the full hash of buf. The tweak is added to the hash before final
// mixing, allowing different outputs much faster than re-seeding. No claims are
// made about the collision probability between hashes with different tweaks.
static inline uint64_t polymur_hash(const uint8_t* buf, size_t len, const PolymurHashParams* p, uint64_t tweak);


// ---------- Cross-platform compatibility ----------
#if (defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER))
    #define POLYMUR_LIKELY(x) (__builtin_expect(!!(x), 1))
    #define POLYMUR_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
    #define POLYMUR_LIKELY(x) (!!(x))
    #define POLYMUR_UNLIKELY(x) (!!(x))
#endif

// No #ifdefs needed, modern compilers all optimize this away.
static inline int polymur_is_little_endian(void) {
    uint32_t v = 1;
    return *(char*) &v;
}

static inline uint32_t polymur_bswap32(uint32_t v) {
    return ((v >> 24) & 0x000000ffUL) | ((v >>  8) & 0x0000ff00UL) | ((v <<  8) & 0x00ff0000UL) | ((v << 24) & 0xff000000UL);
}

static inline uint64_t polymur_bswap64(uint64_t v) {
    return (((uint64_t) polymur_bswap32(v)) << 32) | polymur_bswap32(v >> 32);
}

static inline uint32_t polymur_load_le_u32(const uint8_t* p) {
    uint32_t v = 0; memcpy(&v, p, 4);
    return polymur_is_little_endian() ? v : polymur_bswap32(v);
}

static inline uint64_t polymur_load_le_u64(const uint8_t* p) {
    uint64_t v = 0; memcpy(&v, p, 8);
    return polymur_is_little_endian() ? v : polymur_bswap64(v);
}

// Loads 1 to 8 bytes from buf with length len > 0 as a 64-bit little-endian integer.
static inline uint64_t polymur_load_le_u64_1_8(const uint8_t* buf, size_t len) {
    if (len < 4) {
        uint64_t v = buf[0];
        v |= buf[len / 2] << 8 * (len / 2);
        v |= buf[len - 1] << 8 * (len - 1);
        return v;
    }

    uint64_t lo = polymur_load_le_u32(buf);
    uint64_t hi = polymur_load_le_u32(buf + len - 4);
    return lo | (hi << 8 * (len - 4));
}


// ---------- Integer arithmetic ----------
#define POLYMUR_P611 ((1ULL << 61) - 1)

#if defined(__SIZEOF_INT128__)
    #define polymur_u128_t __uint128_t

    static inline polymur_u128_t polymur_add128(polymur_u128_t a, polymur_u128_t b) {
        return a + b;
    }

    static inline polymur_u128_t polymur_mul128(uint64_t a, uint64_t b) {
        return ((polymur_u128_t) a) * ((polymur_u128_t) b);
    }

    static inline uint64_t polymur_red611(polymur_u128_t x) {
        return (((uint64_t) x) & POLYMUR_P611) + ((uint64_t) (x >> 61));
    }
#else
    typedef struct {
        uint64_t lo;
        uint64_t hi;
    } polymur_u128_t;

    static inline polymur_u128_t polymur_add128(polymur_u128_t a, polymur_u128_t b) {
        a.lo += b.lo;
        a.hi += b.hi + (a.lo < b.lo);
        return a;
    }

    static inline polymur_u128_t polymur_mul128(uint64_t a, uint64_t b) {
        polymur_u128_t ret;
        #if defined(_MSC_VER) && defined(_M_X64)
            ret.lo = _umul128(a, b, &ret.hi);
        #elif defined(_MSC_VER) && defined(_M_ARM64)
            ret.lo = a * b;
            ret.hi = __umulh(a, b);
        #else
            uint64_t lo_lo = (a & 0xffffffffULL) * (b & 0xffffffffULL);
            uint64_t hi_lo = (a >> 32)           * (b & 0xffffffffULL);
            uint64_t lo_hi = (a & 0xffffffffULL) * (b >> 32);
            uint64_t hi_hi = (a >> 32)           * (b >> 32);
            uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xffffffffULL) + lo_hi;
            ret.hi =         (hi_lo >> 32) + (cross >> 32)           + hi_hi;
            ret.lo = (cross << 32) | (lo_lo & 0xffffffffULL);
        #endif
        return ret;
    }

    static inline uint64_t polymur_red611(polymur_u128_t x) {
        #if defined(_MSC_VER) && defined(_M_X64)
            return (((uint64_t) x.lo) & POLYMUR_P611) + __shiftright128(x.lo, x.hi, 61);
        #else
            return (x.lo & POLYMUR_P611) + ((x.lo >> 61) | (x.hi << 3));
        #endif
    }
#endif

static inline uint64_t polymur_extrared611(uint64_t x) {
    return (x & POLYMUR_P611) + (x >> 61);
}


// ---------- Hash function ----------
#define POLYMUR_ARBITRARY1 0x6a09e667f3bcc908ULL // Completely arbitrary, these
#define POLYMUR_ARBITRARY2 0xbb67ae8584caa73bULL // are taken from SHA-2, and
#define POLYMUR_ARBITRARY3 0x3c6ef372fe94f82bULL // are the fractional bits of
#define POLYMUR_ARBITRARY4 0xa54ff53a5f1d36f1ULL // sqrt(p), p = 2, 3, 5, 7.

static inline uint64_t polymur_mix(uint64_t x) {
    // Mixing function from https://jonkagstrom.com/mx3/mx3_rev2.html.
	x ^= x >> 32;
	x *= 0xe9846af9b1a615dULL;
	x ^= x >> 32;
	x *= 0xe9846af9b1a615dULL;
	x ^= x >> 28;   
    return x;
}

static inline void polymur_init_params(PolymurHashParams* p, uint64_t k_seed, uint64_t s_seed) {
    p->s = s_seed ^ POLYMUR_ARBITRARY1; // People love to pass zero.
    
    // POLYMUR_POW37[i] = 37^(2^i) mod (2^61 - 1)
    // Could be replaced by a 512 byte LUT, costs ~400 byte overhead but 2x
    // faster seeding. However, seeding is rather rare, so I chose not to.
    uint64_t POLYMUR_POW37[64];
    POLYMUR_POW37[0] = 37; POLYMUR_POW37[32] = 559096694736811184ULL;
    for (int i = 0; i < 31; ++i) {
        POLYMUR_POW37[i+ 1] = polymur_extrared611(polymur_red611(polymur_mul128(POLYMUR_POW37[i],    POLYMUR_POW37[i])));
        POLYMUR_POW37[i+33] = polymur_extrared611(polymur_red611(polymur_mul128(POLYMUR_POW37[i+32], POLYMUR_POW37[i+32])));
    }
    
    while (1) {
        // Choose a random exponent coprime to 2^61 - 2. ~35.3% success rate.
        k_seed += POLYMUR_ARBITRARY2;
        uint64_t e = (k_seed >> 3) | 1; // e < 2^61, odd.
        if (e % 3 == 0) continue;
        if (!(e % 5 && e % 7)) continue;
        if (!(e % 11 && e % 13 && e % 31)) continue;
        if (!(e % 41 && e % 61 && e % 151 && e % 331 && e % 1321)) continue;
        
        // Compute k = 37^e mod 2^61 - 1. Since e is coprime with the order of
        // the multiplicative group mod 2^61 - 1 and 37 is a generator, this
        // results in another generator of the group.
        uint64_t ka = 1, kb = 1;
        for (int i = 0; e; i += 2, e >>= 2) {
            if (e & 1) ka = polymur_extrared611(polymur_red611(polymur_mul128(ka, POLYMUR_POW37[i])));
            if (e & 2) kb = polymur_extrared611(polymur_red611(polymur_mul128(kb, POLYMUR_POW37[i+1])));
        }
        uint64_t k = polymur_extrared611(polymur_red611(polymur_mul128(ka, kb)));

        // ~46.875% success rate. Bound on k^7 needed for efficient reduction.
        p->k = polymur_extrared611(k);
        p->k2 = polymur_extrared611(polymur_red611(polymur_mul128(p->k,  p->k)));
        uint64_t k3 =               polymur_red611(polymur_mul128(p->k,  p->k2));
        uint64_t k4 =               polymur_red611(polymur_mul128(p->k2, p->k2));
        p->k7 = polymur_extrared611(polymur_red611(polymur_mul128(k3, k4)));
        if (p->k7 < (1ULL << 60) - (1ULL << 56)) break;
        // Our key space is log2(totient(2^61 - 2) * (2^60-2^56)/2^61) ~= 57.4 bits.
    }
}

static inline void polymur_init_params_from_seed(PolymurHashParams* p, uint64_t seed) {
    polymur_init_params(p, polymur_mix(seed + POLYMUR_ARBITRARY3), polymur_mix(seed + POLYMUR_ARBITRARY4));
}

static inline uint64_t polymur_hash_poly611(const uint8_t* buf, size_t len, const PolymurHashParams* p, uint64_t tweak) {
    uint64_t m[7];
    uint64_t poly_acc = tweak;

    if (POLYMUR_LIKELY(len <= 7)) {
        if (len == 0) return 0;
        m[0] = polymur_load_le_u64_1_8(buf, len);
        return poly_acc + polymur_red611(polymur_mul128(p->k + m[0], p->k2 + len));
    }
    
    uint64_t k3 = polymur_red611(polymur_mul128( p->k, p->k2));
    uint64_t k4 = polymur_red611(polymur_mul128(p->k2, p->k2));
    if (POLYMUR_UNLIKELY(len >= 50)) {
        const uint64_t k5 = polymur_extrared611(polymur_red611(polymur_mul128(p->k,  k4)));
        const uint64_t k6 = polymur_extrared611(polymur_red611(polymur_mul128(p->k2, k4)));
        k3 = polymur_extrared611(k3);
        k4 = polymur_extrared611(k4);
        uint64_t h = 0;
        do {
            for (int i = 0; i < 7; ++i) m[i] = polymur_load_le_u64(buf + 7*i) & 0x00ffffffffffffffULL;
            polymur_u128_t t0 = polymur_mul128(p->k  + m[0], k6 + m[1]);
            polymur_u128_t t1 = polymur_mul128(p->k2 + m[2], k5 + m[3]);
            polymur_u128_t t2 = polymur_mul128(   k3 + m[4], k4 + m[5]);
            polymur_u128_t t3 = polymur_mul128(   h  + m[6], p->k7);
            polymur_u128_t  s = polymur_add128(polymur_add128(t0, t1), polymur_add128(t2, t3));
            h = polymur_red611(s);
            len -= 49;
            buf += 49;
        } while (len >= 50);
        const uint64_t k14 = polymur_red611(polymur_mul128(p->k7, p->k7));
        uint64_t hk14 = polymur_red611(polymur_mul128(polymur_extrared611(h), k14));
        poly_acc += polymur_extrared611(hk14);
    }
    
    if (POLYMUR_LIKELY(len >= 8)) {
        m[0] = polymur_load_le_u64(buf) & 0x00ffffffffffffffULL;
        m[1] = polymur_load_le_u64(buf + (len - 7) / 2) & 0x00ffffffffffffffULL;
        m[2] = polymur_load_le_u64(buf + len - 8) >> 8;
        polymur_u128_t t0 = polymur_mul128(p->k2 + m[0], p->k7 + m[1]);
        polymur_u128_t t1 = polymur_mul128(p->k  + m[2],    k3 + len);
        if (POLYMUR_LIKELY(len <= 21)) return poly_acc + polymur_red611(polymur_add128(t0, t1));
        m[3] = polymur_load_le_u64(buf +  7) & 0x00ffffffffffffffULL;
        m[4] = polymur_load_le_u64(buf + 14) & 0x00ffffffffffffffULL;
        m[5] = polymur_load_le_u64(buf + len - 21) & 0x00ffffffffffffffULL;
        m[6] = polymur_load_le_u64(buf + len - 14) & 0x00ffffffffffffffULL;
        uint64_t t0r = polymur_red611(t0);
        polymur_u128_t t2 = polymur_mul128(p->k2 + m[3], p->k7 + m[4]);
        polymur_u128_t t3 = polymur_mul128(  t0r + m[5],    k4 + m[6]);
        polymur_u128_t s = polymur_add128(polymur_add128(t1, t2), t3);
        return poly_acc + polymur_red611(s);
    }

    m[0] = polymur_load_le_u64_1_8(buf, len);
    return poly_acc + polymur_red611(polymur_mul128(p->k + m[0], p->k2 + len));
}

static inline uint64_t polymur_hash(const uint8_t* buf, size_t len, const PolymurHashParams* p, uint64_t tweak) {
    if (len == 0) return 0;
    uint64_t h = polymur_hash_poly611(buf, len, p, tweak);
    return polymur_mix(h) + p->s;
}

#ifdef __cplusplus
}
#endif

#endif
