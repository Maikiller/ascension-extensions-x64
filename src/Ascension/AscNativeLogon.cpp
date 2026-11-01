// Ascension's own logon protocol - native reconstruction (see AscNativeLogon.hpp).
//
// WHAT THIS INSTALLS (all byte-checked against the deployed client, Ascension.exe D4CA3DD1):
//
//   * 0x8CCE00  hello serializer. The exe builds the login packet (appends [00][08], then calls
//              this) and sends whatever the serializer appended. The reconstruction appends the
//              custom record the campaign captured and verified live: [size u16][FC F4 F4 E6]
//              [tag 16][masked payload][trailer 4]. The masked payload is the client hello the
//              server parses (login, X25519 public key, nonce, password tag, ChaCha20-encrypted
//              password); see BuildHelloRecord.
//   * 0x9A83E0  key material. In stock SRP6 this computes K from the challenge; when the custom
//              handshake is in play it must instead copy the 40-byte session key (key_session,
//              the campaign's ks40) into the auth object at ecx+0x20 (the exe passes
//              auth-context+0xA8 as ecx, so the key lands at auth-context+0xC8 - exactly where
//              the reply handler later hands it to the connection).
//   * 0x9A88C0  server-proof check. Stock: compare the 20-byte SRP6 M2. Custom: verify the 32-byte
//              HMAC-SHA256(key_derived, "OK") the server replies with. Nonzero return = fail.
//   * Four length gates 0x14 -> 0x20 (0x8CC7FA, 0x8CC856, 0xDFC024, 0xDFC034) so the reply handler
//              reads and passes a 32-byte proof field. With 0x14 the check structurally fails.
//
// CRYPTO (all derived from the campaign's verified references - re/ascens_crypto.py,
// re/fake_auth8_pure.py, re/ascension_auth_probe.py, and the server's AscensionAuth.h):
//   secret  = 32 random bytes; client_public = X25519(secret, base)
//   shared  = X25519(secret, server_public)          server_public = the KC1 slot value
//   key_derived = derive(KC3, shared, IN4, 32)       (HMAC-SHA256-based, Ascension's KDF)
//   key_session = derive(KC3, shared, IN5, 40)
//   proof       = HMAC-SHA256(key_derived, "OK")
//   hello payload encrypted with ChaCha20(KC2, NC2, counter 1) XOR 0xED over all but the last 4
//   bytes; the password with ChaCha20(key_derived, nonce, counter 1).
// The 'server_public' is a build setting: ASCENSION_NATIVE_LOGON_SERVER_KEY (64 hex chars - the
// value the server prints at startup; the default is the campaign's live-tested key).
//
// DELIBERATE LIMITS (a test/translation build, not a match for the original's obfuscated code):
//   - The record's constant fields (platform, locale, timezone bias, IP) are the values the
//     verified reference sent; the server does not read them.
//   - The three fingerprint/tag fields are fresh random bytes per connection (the reference
//     implementations and the server treat them as opaque).
//   - The stock fallbacks (SRP6 key computation, M2 check) are not kept: this is installed only
//     for servers that speak the custom handshake, and the genuine DLL replaces the same sites.

#if __has_include(<PatchConfig.hpp>)
#include <Ascension/AscX64.hpp>
#include <PatchConfig.hpp>
#endif
#ifndef ASCENSION_NATIVE_LOGON
#define ASCENSION_NATIVE_LOGON 0
#endif

#include <Ascension/AscNativeLogon.hpp>

#if ASCENSION_NATIVE_LOGON

#define _CRT_RAND_S
#include <stdlib.h>

#include <Windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <Ascension/AscLog.hpp>

#ifndef ASCENSION_NATIVE_LOGON_SERVER_KEY
// The campaign's live-tested server public key (the pair priv 22dfa316... / pub 1a03d4c7...).
#define ASCENSION_NATIVE_LOGON_SERVER_KEY "1a03d4c71f289f39046fad519f8af3e92b793709a8a340e492b4ff5ff55e1537"
#endif

namespace
{
    // ---------------------------------------------------------------------------------------------
    // Cryptographic primitives (self-contained; MSVC x86, no external dependencies).
    // ---------------------------------------------------------------------------------------------

    inline uint32_t LoadBe32(const uint8_t* p)
    {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    inline void StoreBe32(uint8_t* p, uint32_t v)
    {
        p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
    }

    inline uint32_t LoadLe32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    inline void StoreLe32(uint8_t* p, uint32_t v)
    {
        p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
    }

    // ---- SHA-256 ----

    inline uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    struct Sha256Context
    {
        uint32_t state[8];
        uint64_t bitLength;
        uint8_t buffer[64];
        size_t buffered;

        void Init()
        {
            static const uint32_t iv[8] =
            {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
            };
            memcpy(state, iv, sizeof(state));
            bitLength = 0;
            buffered = 0;
        }

        static void Compress(uint32_t state[8], const uint8_t block[64])
        {
            static const uint32_t k[64] =
            {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
            };

            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = LoadBe32(block + i * 4);
            for (int i = 16; i < 64; ++i)
            {
                uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
            uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
            for (int i = 0; i < 64; ++i)
            {
                uint32_t s1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
                uint32_t ch = (e & f) ^ (~e & g);
                uint32_t t1 = h + s1 + ch + k[i] + w[i];
                uint32_t s0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t t2 = s0 + maj;
                h = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }
            state[0] += a; state[1] += b; state[2] += c; state[3] += d;
            state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        }

        void Update(const uint8_t* data, size_t length)
        {
            bitLength += uint64_t(length) * 8;
            while (length > 0)
            {
                size_t take = 64 - buffered;
                if (take > length)
                    take = length;
                memcpy(buffer + buffered, data, take);
                buffered += take;
                data += take;
                length -= take;
                if (buffered == 64)
                {
                    Compress(state, buffer);
                    buffered = 0;
                }
            }
        }

        void Final(uint8_t out[32])
        {
            uint64_t bits = bitLength;
            uint8_t one = 0x80;
            Update(&one, 1);
            uint8_t zero = 0;
            while (buffered != 56)
                Update(&zero, 1);
            uint8_t lengthBytes[8];
            for (int i = 0; i < 8; ++i)
                lengthBytes[i] = uint8_t(bits >> (8 * (7 - i)));
            Update(lengthBytes, 8);          // note: bitLength field no longer mattering
            for (int i = 0; i < 8; ++i)
                StoreBe32(out + i * 4, state[i]);
        }
    };

    void Sha256(uint8_t out[32], const uint8_t* data, size_t length)
    {
        Sha256Context ctx;
        ctx.Init();
        ctx.Update(data, length);
        ctx.Final(out);
    }

    void HmacSha256(uint8_t out[32], const uint8_t* key, size_t keyLength, const uint8_t* message, size_t messageLength)
    {
        uint8_t block[64];
        memset(block, 0, sizeof(block));
        if (keyLength > sizeof(block))
        {
            Sha256(block, key, keyLength);       // long keys are hashed first
        }
        else
        {
            memcpy(block, key, keyLength);
        }

        uint8_t pad[64];
        uint8_t inner[32];

        for (int i = 0; i < 64; ++i)
            pad[i] = block[i] ^ 0x36;
        Sha256Context ctx;
        ctx.Init();
        ctx.Update(pad, sizeof(pad));
        ctx.Update(message, messageLength);
        ctx.Final(inner);

        for (int i = 0; i < 64; ++i)
            pad[i] = block[i] ^ 0x5c;
        ctx.Init();
        ctx.Update(pad, sizeof(pad));
        ctx.Update(inner, sizeof(inner));
        ctx.Final(out);
    }

    // ---- ChaCha20 (IETF layout: 32-bit counter, 96-bit nonce, 20 rounds) ----

    inline uint32_t Rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    inline void ChaChaQuarter(uint32_t* w, int a, int b, int c, int d)
    {
        w[a] += w[b]; w[d] = Rotl32(w[d] ^ w[a], 16);
        w[c] += w[d]; w[b] = Rotl32(w[b] ^ w[c], 12);
        w[a] += w[b]; w[d] = Rotl32(w[d] ^ w[a], 8);
        w[c] += w[d]; w[b] = Rotl32(w[b] ^ w[c], 7);
    }

    void ChaCha20Stream(uint8_t* out, size_t length, const uint8_t key[32], const uint8_t nonce[12], uint32_t counter)
    {
        uint32_t state[16];
        state[0] = 0x61707865; state[1] = 0x3320646e; state[2] = 0x79622d32; state[3] = 0x6b206574;
        for (int i = 0; i < 8; ++i)
            state[4 + i] = LoadLe32(key + i * 4);
        for (int i = 0; i < 3; ++i)
            state[13 + i] = LoadLe32(nonce + i * 4);
        state[12] = counter;

        size_t written = 0;
        while (written < length)
        {
            uint32_t w[16];
            memcpy(w, state, sizeof(w));
            for (int round = 0; round < 10; ++round)
            {
                ChaChaQuarter(w, 0, 4, 8, 12); ChaChaQuarter(w, 1, 5, 9, 13);
                ChaChaQuarter(w, 2, 6, 10, 14); ChaChaQuarter(w, 3, 7, 11, 15);
                ChaChaQuarter(w, 0, 5, 10, 15); ChaChaQuarter(w, 1, 6, 11, 12);
                ChaChaQuarter(w, 2, 7, 8, 13); ChaChaQuarter(w, 3, 4, 9, 14);
            }
            for (int i = 0; i < 16; ++i)
                w[i] += state[i];

            size_t take = 64;
            if (take > length - written)
                take = length - written;
            for (size_t i = 0; i < take; ++i)
                out[written + i] = uint8_t(w[i / 4] >> (8 * (i & 3)));
            written += take;
            ++state[12];
        }
    }

    // ---- X25519 (RFC 7748), curve25519-donna style 25.5-bit limbs ----

    typedef int32_t fe[10];

    inline void FeZero(fe h) { for (int i = 0; i < 10; ++i) h[i] = 0; }
    inline void FeOne(fe h) { FeZero(h); h[0] = 1; }
    inline void FeCopy(fe h, const fe f) { for (int i = 0; i < 10; ++i) h[i] = f[i]; }
    inline void FeAdd(fe h, const fe f, const fe g) { for (int i = 0; i < 10; ++i) h[i] = f[i] + g[i]; }
    inline void FeSub(fe h, const fe f, const fe g) { for (int i = 0; i < 10; ++i) h[i] = f[i] - g[i]; }

    inline void FeCswap(fe f, fe g, int swap)
    {
        int32_t mask = -swap;
        for (int i = 0; i < 10; ++i)
        {
            int32_t x = mask & (f[i] ^ g[i]);
            f[i] ^= x;
            g[i] ^= x;
        }
    }

    void FeMul(fe h, const fe f, const fe g)
    {
        int32_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
        int32_t f5 = f[5], f6 = f[6], f7 = f[7], f8 = f[8], f9 = f[9];
        int32_t g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
        int32_t g5 = g[5], g6 = g[6], g7 = g[7], g8 = g[8], g9 = g[9];

        const int64_t g1_19 = 19 * (int64_t)g1, g2_19 = 19 * (int64_t)g2, g3_19 = 19 * (int64_t)g3, g4_19 = 19 * (int64_t)g4, g5_19 = 19 * (int64_t)g5;
        const int64_t g6_19 = 19 * (int64_t)g6, g7_19 = 19 * (int64_t)g7, g8_19 = 19 * (int64_t)g8, g9_19 = 19 * (int64_t)g9;
        const int64_t f1_2 = 2 * (int64_t)f1, f3_2 = 2 * (int64_t)f3, f5_2 = 2 * (int64_t)f5, f7_2 = 2 * (int64_t)f7, f9_2 = 2 * (int64_t)f9;

        int64_t h0 = (int64_t)f0 * g0 + (int64_t)f1_2 * g9_19 + (int64_t)f2 * g8_19 + (int64_t)f3_2 * g7_19 +
            (int64_t)f4 * g6_19 + (int64_t)f5_2 * g5_19 + (int64_t)f6 * g4_19 + (int64_t)f7_2 * g3_19 +
            (int64_t)f8 * g2_19 + (int64_t)f9_2 * g1_19;
        int64_t h1 = (int64_t)f0 * g1 + (int64_t)f1 * g0 + (int64_t)f2 * g9_19 + (int64_t)f3 * g8_19 +
            (int64_t)f4 * g7_19 + (int64_t)f5 * g6_19 + (int64_t)f6 * g5_19 + (int64_t)f7 * g4_19 +
            (int64_t)f8 * g3_19 + (int64_t)f9 * g2_19;
        int64_t h2 = (int64_t)f0 * g2 + (int64_t)f1_2 * g1 + (int64_t)f2 * g0 + (int64_t)f3_2 * g9_19 +
            (int64_t)f4 * g8_19 + (int64_t)f5_2 * g7_19 + (int64_t)f6 * g6_19 + (int64_t)f7_2 * g5_19 +
            (int64_t)f8 * g4_19 + (int64_t)f9_2 * g3_19;
        int64_t h3 = (int64_t)f0 * g3 + (int64_t)f1 * g2 + (int64_t)f2 * g1 + (int64_t)f3 * g0 +
            (int64_t)f4 * g9_19 + (int64_t)f5 * g8_19 + (int64_t)f6 * g7_19 + (int64_t)f7 * g6_19 +
            (int64_t)f8 * g5_19 + (int64_t)f9 * g4_19;
        int64_t h4 = (int64_t)f0 * g4 + (int64_t)f1_2 * g3 + (int64_t)f2 * g2 + (int64_t)f3_2 * g1 +
            (int64_t)f4 * g0 + (int64_t)f5_2 * g9_19 + (int64_t)f6 * g8_19 + (int64_t)f7_2 * g7_19 +
            (int64_t)f8 * g6_19 + (int64_t)f9_2 * g5_19;
        int64_t h5 = (int64_t)f0 * g5 + (int64_t)f1 * g4 + (int64_t)f2 * g3 + (int64_t)f3 * g2 +
            (int64_t)f4 * g1 + (int64_t)f5 * g0 + (int64_t)f6 * g9_19 + (int64_t)f7 * g8_19 +
            (int64_t)f8 * g7_19 + (int64_t)f9 * g6_19;
        int64_t h6 = (int64_t)f0 * g6 + (int64_t)f1_2 * g5 + (int64_t)f2 * g4 + (int64_t)f3_2 * g3 +
            (int64_t)f4 * g2 + (int64_t)f5_2 * g1 + (int64_t)f6 * g0 + (int64_t)f7_2 * g9_19 +
            (int64_t)f8 * g8_19 + (int64_t)f9_2 * g7_19;
        int64_t h7 = (int64_t)f0 * g7 + (int64_t)f1 * g6 + (int64_t)f2 * g5 + (int64_t)f3 * g4 +
            (int64_t)f4 * g3 + (int64_t)f5 * g2 + (int64_t)f6 * g1 + (int64_t)f7 * g0 +
            (int64_t)f8 * g9_19 + (int64_t)f9 * g8_19;
        int64_t h8 = (int64_t)f0 * g8 + (int64_t)f1_2 * g7 + (int64_t)f2 * g6 + (int64_t)f3_2 * g5 +
            (int64_t)f4 * g4 + (int64_t)f5_2 * g3 + (int64_t)f6 * g2 + (int64_t)f7_2 * g1 +
            (int64_t)f8 * g0 + (int64_t)f9_2 * g9_19;
        int64_t h9 = (int64_t)f0 * g9 + (int64_t)f1 * g8 + (int64_t)f2 * g7 + (int64_t)f3 * g6 +
            (int64_t)f4 * g5 + (int64_t)f5 * g4 + (int64_t)f6 * g3 + (int64_t)f7 * g2 +
            (int64_t)f8 * g1 + (int64_t)f9 * g0;

        int64_t c;
        c = h0 >> 26; h1 += c; h0 -= c << 26;
        c = h1 >> 25; h2 += c; h1 -= c << 25;
        c = h2 >> 26; h3 += c; h2 -= c << 26;
        c = h3 >> 25; h4 += c; h3 -= c << 25;
        c = h4 >> 26; h5 += c; h4 -= c << 26;
        c = h5 >> 25; h6 += c; h5 -= c << 25;
        c = h6 >> 26; h7 += c; h6 -= c << 26;
        c = h7 >> 25; h8 += c; h7 -= c << 25;
        c = h8 >> 26; h9 += c; h8 -= c << 26;
        c = h9 >> 25; h0 += c * 19; h9 -= c << 25;
        c = h0 >> 26; h1 += c; h0 -= c << 26;

        h[0] = (int32_t)h0; h[1] = (int32_t)h1; h[2] = (int32_t)h2; h[3] = (int32_t)h3; h[4] = (int32_t)h4;
        h[5] = (int32_t)h5; h[6] = (int32_t)h6; h[7] = (int32_t)h7; h[8] = (int32_t)h8; h[9] = (int32_t)h9;
    }

    inline void FeSquare(fe h, const fe f) { FeMul(h, f, f); }

    void FeMulSmall(fe h, const fe f, int32_t s)
    {
        int64_t h0 = (int64_t)f[0] * s, h1 = (int64_t)f[1] * s, h2 = (int64_t)f[2] * s, h3 = (int64_t)f[3] * s;
        int64_t h4 = (int64_t)f[4] * s, h5 = (int64_t)f[5] * s, h6 = (int64_t)f[6] * s, h7 = (int64_t)f[7] * s;
        int64_t h8 = (int64_t)f[8] * s, h9 = (int64_t)f[9] * s;

        int64_t c;
        c = h0 >> 26; h1 += c; h0 -= c << 26;
        c = h1 >> 25; h2 += c; h1 -= c << 25;
        c = h2 >> 26; h3 += c; h2 -= c << 26;
        c = h3 >> 25; h4 += c; h3 -= c << 25;
        c = h4 >> 26; h5 += c; h4 -= c << 26;
        c = h5 >> 25; h6 += c; h5 -= c << 25;
        c = h6 >> 26; h7 += c; h6 -= c << 26;
        c = h7 >> 25; h8 += c; h7 -= c << 25;
        c = h8 >> 26; h9 += c; h8 -= c << 26;
        c = h9 >> 25; h0 += c * 19; h9 -= c << 25;
        c = h0 >> 26; h1 += c; h0 -= c << 26;

        h[0] = (int32_t)h0; h[1] = (int32_t)h1; h[2] = (int32_t)h2; h[3] = (int32_t)h3; h[4] = (int32_t)h4;
        h[5] = (int32_t)h5; h[6] = (int32_t)h6; h[7] = (int32_t)h7; h[8] = (int32_t)h8; h[9] = (int32_t)h9;
    }

    void FeFromBytes(fe h, const uint8_t* s)
    {
        int64_t h0 = LoadLe32(s) & 0x3ffffff;
        int64_t h1 = (LoadLe32(s + 3) >> 2) & 0x1ffffff;
        int64_t h2 = (LoadLe32(s + 6) >> 3) & 0x3ffffff;
        int64_t h3 = (LoadLe32(s + 9) >> 5) & 0x1ffffff;
        int64_t h4 = (LoadLe32(s + 12) >> 6) & 0x3ffffff;
        int64_t h5 = LoadLe32(s + 16) & 0x1ffffff;
        int64_t h6 = (LoadLe32(s + 19) >> 1) & 0x3ffffff;
        int64_t h7 = (LoadLe32(s + 22) >> 3) & 0x1ffffff;
        int64_t h8 = (LoadLe32(s + 25) >> 4) & 0x3ffffff;
        int64_t h9 = (LoadLe32(s + 28) >> 6) & 0x1ffffff;

        h[0] = (int32_t)h0; h[1] = (int32_t)h1; h[2] = (int32_t)h2; h[3] = (int32_t)h3; h[4] = (int32_t)h4;
        h[5] = (int32_t)h5; h[6] = (int32_t)h6; h[7] = (int32_t)h7; h[8] = (int32_t)h8; h[9] = (int32_t)h9;
    }

    void FeToBytes(uint8_t* s, fe h)
    {
        int32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
        int32_t h5 = h[5], h6 = h[6], h7 = h[7], h8 = h[8], h9 = h[9];

        int32_t q;
        q = (19 * h9 + (1 << 24)) >> 25;
        q = (h0 + q) >> 26;
        q = (h1 + q) >> 25;
        q = (h2 + q) >> 26;
        q = (h3 + q) >> 25;
        q = (h4 + q) >> 26;
        q = (h5 + q) >> 25;
        q = (h6 + q) >> 26;
        q = (h7 + q) >> 25;
        q = (h8 + q) >> 26;
        q = (h9 + q) >> 25;
        h0 += 19 * q;

        int32_t c;
        c = h0 >> 26; h1 += c; h0 -= c << 26;
        c = h1 >> 25; h2 += c; h1 -= c << 25;
        c = h2 >> 26; h3 += c; h2 -= c << 26;
        c = h3 >> 25; h4 += c; h3 -= c << 25;
        c = h4 >> 26; h5 += c; h4 -= c << 26;
        c = h5 >> 25; h6 += c; h5 -= c << 25;
        c = h6 >> 26; h7 += c; h6 -= c << 26;
        c = h7 >> 25; h8 += c; h7 -= c << 25;
        c = h8 >> 26; h9 += c; h8 -= c << 26;
        c = h9 >> 25; h9 -= c << 25;

        s[0] = (uint8_t)(h0 >> 0); s[1] = (uint8_t)(h0 >> 8); s[2] = (uint8_t)(h0 >> 16); s[3] = (uint8_t)((h0 >> 24) | (h1 << 2));
        s[4] = (uint8_t)(h1 >> 6); s[5] = (uint8_t)(h1 >> 14); s[6] = (uint8_t)((h1 >> 22) | (h2 << 3));
        s[7] = (uint8_t)(h2 >> 5); s[8] = (uint8_t)(h2 >> 13); s[9] = (uint8_t)((h2 >> 21) | (h3 << 5));
        s[10] = (uint8_t)(h3 >> 3); s[11] = (uint8_t)(h3 >> 11); s[12] = (uint8_t)((h3 >> 19) | (h4 << 6));
        s[13] = (uint8_t)(h4 >> 2); s[14] = (uint8_t)(h4 >> 10); s[15] = (uint8_t)(h4 >> 18);
        s[16] = (uint8_t)(h5 >> 0); s[17] = (uint8_t)(h5 >> 8); s[18] = (uint8_t)(h5 >> 16); s[19] = (uint8_t)((h5 >> 24) | (h6 << 1));
        s[20] = (uint8_t)(h6 >> 7); s[21] = (uint8_t)(h6 >> 15); s[22] = (uint8_t)((h6 >> 23) | (h7 << 3));
        s[23] = (uint8_t)(h7 >> 5); s[24] = (uint8_t)(h7 >> 13); s[25] = (uint8_t)((h7 >> 21) | (h8 << 4));
        s[26] = (uint8_t)(h8 >> 4); s[27] = (uint8_t)(h8 >> 12); s[28] = (uint8_t)((h8 >> 20) | (h9 << 6));
        s[29] = (uint8_t)(h9 >> 2); s[30] = (uint8_t)(h9 >> 10); s[31] = (uint8_t)(h9 >> 18);
    }

    void FeInvert(fe out, const fe z)
    {
        // z^(p-2), p-2 = 2^255 - 21 = little-endian {0xEB, FF x30, 0x7F}
        static const uint8_t kExponent[32] =
        {
            0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F
        };

        fe r;
        FeOne(r);
        for (int bit = 254; bit >= 0; --bit)
        {
            FeSquare(r, r);
            if ((kExponent[bit >> 3] >> (bit & 7)) & 1)
                FeMul(r, r, z);
        }
        FeCopy(out, r);
    }

    // scalarMult: out = scalar * point (RFC 7748 X25519). Returns false for a low-order result.
    bool X25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
    {
        uint8_t k[32];
        memcpy(k, scalar, sizeof(k));
        k[0] &= 248;
        k[31] &= 127;
        k[31] |= 64;

        uint8_t u[32];
        memcpy(u, point, sizeof(u));
        u[31] &= 127;

        fe x1, x2, z2, x3, z3;
        fe a, aa, b, bb, e, c, d, da, cb;
        FeFromBytes(x1, u);
        FeOne(x2);
        FeZero(z2);
        FeCopy(x3, x1);
        FeOne(z3);

        int swap = 0;
        for (int t = 254; t >= 0; --t)
        {
            int kt = (k[t >> 3] >> (t & 7)) & 1;
            swap ^= kt;
            FeCswap(x2, x3, swap);
            FeCswap(z2, z3, swap);
            swap = kt;

            FeAdd(a, x2, z2);        // A  = x2 + z2
            FeSquare(aa, a);         // AA = A^2
            FeSub(b, x2, z2);        // B  = x2 - z2
            FeSquare(bb, b);         // BB = B^2
            FeSub(e, aa, bb);        // E  = AA - BB
            FeAdd(c, x3, z3);        // C  = x3 + z3
            FeSub(d, x3, z3);        // D  = x3 - z3
            FeMul(da, d, a);         // DA = D * A
            FeMul(cb, c, b);         // CB = C * B

            FeAdd(a, da, cb);        // x3 = (DA + CB)^2
            FeSquare(x3, a);
            FeSub(a, da, cb);        // z3 = x1 * (DA - CB)^2
            FeSquare(a, a);
            FeMul(z3, x1, a);

            FeMul(x2, aa, bb);       // x2 = AA * BB
            FeMulSmall(a, e, 121665);// z2 = E * (AA + a24 * E)
            FeAdd(a, aa, a);
            FeMul(z2, e, a);
        }
        FeCswap(x2, x3, swap);
        FeCswap(z2, z3, swap);

        FeInvert(z2, z2);
        FeMul(x2, x2, z2);
        FeToBytes(out, x2);

        uint8_t acc = 0;
        for (int i = 0; i < 32; ++i)
            acc |= out[i];
        return acc != 0;
    }

    void X25519Base(uint8_t out[32], const uint8_t scalar[32])
    {
        uint8_t base[32];
        memset(base, 0, sizeof(base));
        base[0] = 9;
        X25519(out, scalar, base);
    }

    // ---- Ascension's key derivation (HMAC-SHA256 based, ascens_crypto.py derive_key) ----

    void DeriveKey(uint8_t* out, size_t size, const uint8_t key[32], const uint8_t* input1, size_t input1Length,
        const uint8_t* input2, size_t input2Length)
    {
        uint8_t interim[32];
        HmacSha256(interim, key, 32, input1, input1Length);

        uint8_t material[96];
        memcpy(material, input2, input2Length);
        material[input2Length] = 0x01;
        uint8_t result[32];
        HmacSha256(result, interim, 32, material, input2Length + 1);

        if (size <= 32)
        {
            memcpy(out, result, size);
            return;
        }

        size_t p = 0;
        memcpy(material + p, result, 32);
        p += 32;
        memcpy(material + p, input2, input2Length);
        p += input2Length;
        material[p++] = 0x02;
        uint8_t result2[32];
        HmacSha256(result2, interim, 32, material, p);

        memcpy(out, result, 32);
        memcpy(out + 32, result2, size - 32);
    }

    void RandomBytes(uint8_t* out, size_t length)
    {
        size_t i = 0;
        while (i < length)
        {
            unsigned int value = 0;
            if (rand_s(&value) != 0)
                value = GetTickCount() ^ uint32_t(uintptr_t(out + i));
            for (int b = 0; b < 4 && i < length; ++b)
                out[i++] = uint8_t(value >> (8 * b));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Protocol constants (byte-exact copies of the campaign's verified values; see the server's
    // AscensionAuth.h where the same arrays appear).
    // ---------------------------------------------------------------------------------------------

    const uint8_t KC2[32] =
    {
        0x33, 0xba, 0x31, 0x28, 0xee, 0x61, 0x4b, 0x58, 0x45, 0xe0, 0x6b, 0x0d, 0xad, 0x17, 0x6a, 0x9c,
        0x79, 0x34, 0x4d, 0xd7, 0xa7, 0xa1, 0xe2, 0xe8, 0xd8, 0xad, 0x09, 0x7d, 0xa9, 0xb5, 0x7f, 0x01
    };
    const uint8_t NC2[12] = { 0x92, 0x01, 0x00, 0x8e, 0xca, 0xfa, 0x7d, 0x60, 0xe0, 0xac, 0xc8, 0x1e };
    const uint8_t KC3[32] =
    {
        0x66, 0xd5, 0x2b, 0x01, 0xe0, 0x06, 0xcd, 0x24, 0x6f, 0x09, 0x00, 0x25, 0xd6, 0x31, 0x2c, 0x62,
        0xd1, 0x3e, 0x84, 0x7c, 0x98, 0x05, 0x95, 0x6a, 0x1c, 0x5a, 0x10, 0x36, 0x4b, 0xaa, 0x7d, 0x82
    };
    const uint8_t IN4[32] =   // info: key_derived
    {
        0xe8, 0x15, 0x73, 0x9f, 0x8e, 0xc8, 0x10, 0x72, 0x1b, 0x93, 0x55, 0x4c, 0xa2, 0xea, 0xc5, 0x97,
        0xe0, 0x5f, 0x37, 0x52, 0x61, 0xdd, 0x72, 0xff, 0x30, 0x83, 0x7d, 0xf9, 0x51, 0xc7, 0xa5, 0xed
    };
    const uint8_t IN5[32] =   // info: key_session
    {
        0x26, 0x98, 0x6c, 0x8a, 0x73, 0xd2, 0x4b, 0xc4, 0x1c, 0xf3, 0x86, 0xbc, 0xb5, 0x84, 0x92, 0x41,
        0x6f, 0xb5, 0x79, 0x78, 0x4e, 0x19, 0x57, 0x70, 0x1a, 0x88, 0x9d, 0x97, 0xb6, 0x55, 0x01, 0x40
    };

    // The exe global holding the login name exactly as the client would send it (uppercased, any
    // '#...' suffix already stripped by the sender). Ascension.exe is fixed at 0x400000.
    const uintptr_t kAccountNameGlobal = 0x00D45CD0;

    // The exe's packet store appender: (store, src, size) with ecx = store. Handles capacity
    // growth through the store's own vtable.
    typedef void* (__thiscall* BulkAppendFn)(void* store, const void* source, size_t size);
    const uintptr_t kBulkAppendAddress = 0x0047B1C0;

    // ---------------------------------------------------------------------------------------------
    // Session material.
    // ---------------------------------------------------------------------------------------------

    struct Session
    {
        bool valid;
        uint8_t clientPublic[32];
        uint8_t keyDerived[32];     // 'kd', the HMAC key for the server proof
        uint8_t keySession[40];     // 'ks40', the 40-byte session key handed to the connection
        uint8_t proof[32];          // HMAC-SHA256(keyDerived, "OK") the server must return
    };

    Session g_session = {};
    uint8_t g_serverPublicKey[32] = {};
    bool g_serverKeyValid = false;

    bool ParseServerKey()
    {
        const char* hex = ASCENSION_NATIVE_LOGON_SERVER_KEY;
        if (strlen(hex) != 64)
            return false;
        for (int i = 0; i < 32; ++i)
        {
            char hi = hex[i * 2];
            char lo = hex[i * 2 + 1];
            auto nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = nibble(hi), l = nibble(lo);
            if (h < 0 || l < 0)
                return false;
            g_serverPublicKey[i] = uint8_t((h << 4) | l);
        }
        return true;
    }

    bool DeriveSession(Session& session, const uint8_t secret[32], const uint8_t serverPublic[32])
    {
        uint8_t shared[32];
        if (!X25519(shared, secret, serverPublic))
            return false;

        X25519Base(session.clientPublic, secret);
        DeriveKey(session.keyDerived, 32, KC3, shared, sizeof(shared), IN4, sizeof(IN4));
        DeriveKey(session.keySession, 40, KC3, shared, sizeof(shared), IN5, sizeof(IN5));
        HmacSha256(session.proof, session.keyDerived, 32, reinterpret_cast<const uint8_t*>("OK"), 2);
        session.valid = true;
        return true;
    }

    // Builds the custom hello record (everything the serializer appends after the exe's [00][08]):
    //   [size u16][FC F4 F4 E6][tag 16][masked payload][trailer 4]
    // payload (612 bytes for a 7-char password):
    //   version token 256 | "WoW" 0 | 3 3 5 | build u16 | "x86" 0 | "niW" 0 | "SUne" |
    //   u32 tz bias | ip 4 | login 256 | client public 32 | nonce 12 | password tag 16 |
    //   password length u32 | ChaCha20(key_derived, nonce) encrypted password
    size_t BuildHelloRecord(const uint8_t serverPublic[32], const uint8_t keyDerived[32], const uint8_t clientPublic[32],
        const uint8_t nonce[12], const uint8_t tag[16], const uint8_t passwordTag[16],
        const char* account, const char* password, uint8_t* out, size_t outCapacity)
    {
        uint8_t clear[1024];
        memset(clear, 0, sizeof(clear));
        size_t p = 0;

        static const char hexDigits[] = "0123456789abcdef";
        clear[0] = '1'; clear[1] = '|'; clear[2] = '1'; clear[3] = '|';
        for (int i = 0; i < 32; ++i)
        {
            clear[4 + i * 2] = uint8_t(hexDigits[serverPublic[i] >> 4]);
            clear[5 + i * 2] = uint8_t(hexDigits[serverPublic[i] & 0x0F]);
        }
        p = 256;

        memcpy(clear + p, "WoW\0", 4); p += 4;
        clear[p++] = 3; clear[p++] = 3; clear[p++] = 5;
        clear[p++] = 0x34; clear[p++] = 0x30;                                  // build 12340, little-endian
        memcpy(clear + p, "x86\0", 4); p += 4;
        memcpy(clear + p, "niW\0", 4); p += 4;                                 // as the genuine client writes it
        memcpy(clear + p, "SUne", 4); p += 4;                                  // as the genuine client writes it
        clear[p++] = 60; clear[p++] = 0; clear[p++] = 0; clear[p++] = 0;       // timezone bias
        clear[p++] = 127; clear[p++] = 0; clear[p++] = 0; clear[p++] = 1;      // address (loopback testing)

        size_t accountLength = strnlen(account, 255);
        memcpy(clear + p, account, accountLength);
        p += 256;

        memcpy(clear + p, clientPublic, 32); p += 32;
        memcpy(clear + p, nonce, 12); p += 12;
        memcpy(clear + p, passwordTag, 16); p += 16;

        size_t passwordLength = strnlen(password, 255);
        clear[p++] = uint8_t(passwordLength); clear[p++] = uint8_t(passwordLength >> 8);
        clear[p++] = 0; clear[p++] = 0;
        if (passwordLength > 0)
        {
            uint8_t stream[256];
            ChaCha20Stream(stream, passwordLength, keyDerived, nonce, 1);
            for (size_t i = 0; i < passwordLength; ++i)
                clear[p + i] = uint8_t(password[i]) ^ stream[i];
        }
        p += passwordLength;

        // p == 605 + passwordLength; everything but the last 4 bytes is masked with the transport
        // keystream and the 0xED constant, the last 4 bytes go out in clear (as the client sends it).
        size_t cipherLength = p - 4;
        size_t recordLength = 2 + 4 + 16 + cipherLength + 4;
        if (recordLength > outCapacity)
            return 0;

        size_t r = 0;
        uint32_t sizeField = uint32_t(625 + passwordLength);
        out[r++] = uint8_t(sizeField); out[r++] = uint8_t(sizeField >> 8);
        out[r++] = 0xFC; out[r++] = 0xF4; out[r++] = 0xF4; out[r++] = 0xE6;
        memcpy(out + r, tag, 16); r += 16;

        uint8_t transport[1024];
        ChaCha20Stream(transport, cipherLength, KC2, NC2, 1);
        for (size_t i = 0; i < cipherLength; ++i)
            out[r + i] = uint8_t(clear[i] ^ transport[i] ^ 0xED);
        r += cipherLength;

        memcpy(out + r, clear + cipherLength, 4); r += 4;
        return r;
    }

    // ---------------------------------------------------------------------------------------------
    // The three exe thunk replacements.
    // ---------------------------------------------------------------------------------------------

    // 0x8CCE00 - hello serialization. Called by the exe's sender with ecx = connection,
    // (store, loginInfo[]) on the stack; the exe then sends everything that was appended.
    // (MSVC rejects __thiscall on free functions; __fastcall with a dummy second parameter is
    // the standard equivalent - ecx = this, edx ignored, stack arguments and cleanup identical.)
    int __fastcall HookHelloSender(void* connection ASC_EDX_P(void* /*edx*/), void* store, void* loginInfo)
    {
        (void)connection;
        if (store == nullptr)
            return 0;

        const char* account = reinterpret_cast<const char*>(kAccountNameGlobal);
        const char* password = "";
        if (loginInfo != nullptr)
        {
            const char* candidate = *reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(loginInfo) + 4);
            if (candidate != nullptr)
                password = candidate;
        }

        Session session;
        uint8_t secret[32];
        RandomBytes(secret, sizeof(secret));
        if (!DeriveSession(session, secret, g_serverPublicKey))
        {
            AscLog::Printf("native-logon: hello hook: key derivation failed, custom hello NOT written");
            return 0;
        }
        g_session = session;

        uint8_t nonce[12], tag[16], passwordTag[16];
        RandomBytes(nonce, sizeof(nonce));
        RandomBytes(tag, sizeof(tag));
        RandomBytes(passwordTag, sizeof(passwordTag));

        uint8_t record[1200];
        size_t recordLength = BuildHelloRecord(g_serverPublicKey, session.keyDerived, session.clientPublic,
            nonce, tag, passwordTag, account, password, record, sizeof(record));
        if (recordLength == 0)
        {
            AscLog::Printf("native-logon: hello hook: record build failed, custom hello NOT written");
            return 0;
        }

        reinterpret_cast<BulkAppendFn>(kBulkAppendAddress)(store, record, recordLength);
        AscLog::Printf("native-logon: custom hello written (%zu bytes, login '%s', password %zu chars)",
            recordLength, account, strnlen(password, 255));
        return 0;
    }

    // 0x9A83E0 - key material. The exe calls this from its challenge handler with ecx = the auth
    // context + 0xA8 and nine stack arguments (the stock version computes the SRP6 session key).
    // The custom handshake copies the 40-byte key_session to ecx+0x20 (= auth context + 0xC8).
    // SRP6_Client::CalculateProof(const u8*, unsigned, const u8*, unsigned, const u8*, unsigned, const u8*, unsigned, SRP6_Random&)
    int __fastcall HookKeyMaterial(void* authContext ASC_EDX_P(void* /*edx*/), const uint8_t*, unsigned int, const uint8_t*, unsigned int,
                                   const uint8_t*, unsigned int, const uint8_t*, unsigned int, void*)
    {
        if (!g_session.valid)
        {
            AscLog::Printf("native-logon: key-material hook: no derived session, failing the handshake");
            return -1;
        }
        memcpy(static_cast<uint8_t*>(authContext) + 0x20, g_session.keySession, sizeof(g_session.keySession));
        AscLog::Printf("native-logon: session key (key_session) installed into the auth object");
        return 0;
    }

    // 0x9A88C0 - server proof check. The exe's reply handler reads the proof field and calls this
    // with (field, size); nonzero return = failure (the handler then reports error 0xB).
#if ASC_X64
    int HookProofCheck(void* /*self*/, const uint8_t* field, unsigned int size)   // x64: this em rcx
#else
    int __stdcall HookProofCheck(const uint8_t* field, unsigned int size)
#endif
    {
        if (size != 32)
        {
            AscLog::Printf("native-logon: proof check: unexpected field size %u (expected 32) - FAIL", size);
            return 1;
        }
        if (!g_session.valid)
        {
            AscLog::Printf("native-logon: proof check: no derived session - FAIL");
            return 1;
        }

        uint8_t difference = 0;
        for (int i = 0; i < 32; ++i)
            difference |= field[i] ^ g_session.proof[i];
        AscLog::Printf("native-logon: proof check %s (HMAC-SHA256(key_derived, \"OK\"))",
            difference == 0 ? "PASSED" : "FAILED");
        return difference == 0 ? 0 : 1;
    }

    // ---------------------------------------------------------------------------------------------
    // Self-test - pins the primitives to the campaign's live-verified values. If any of this
    // fails, Install() refuses to patch anything and the client keeps stock SRP6.
    // ---------------------------------------------------------------------------------------------

    bool HexEqual(const uint8_t* data, size_t length, const char* hex)
    {
        if (strlen(hex) != length * 2)
            return false;
        for (size_t i = 0; i < length; ++i)
        {
            char buf[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
            unsigned long value = strtoul(buf, nullptr, 16);
            if (data[i] != uint8_t(value))
                return false;
        }
        return true;
    }

    bool SelfTest()
    {
        bool ok = true;

        // SHA-256 FIPS vector.
        {
            uint8_t digest[32];
            Sha256(digest, reinterpret_cast<const uint8_t*>("abc"), 3);
            ok &= HexEqual(digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        }

        // HMAC-SHA256 RFC 4231 case 1.
        {
            uint8_t key[20];
            memset(key, 0x0b, sizeof(key));
            uint8_t digest[32];
            HmacSha256(digest, key, sizeof(key), reinterpret_cast<const uint8_t*>("Hi There"), 8);
            ok &= HexEqual(digest, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
        }

        // ChaCha20 with the protocol's transport key - the live client's first 16 bytes.
        {
            uint8_t stream[16];
            ChaCha20Stream(stream, sizeof(stream), KC2, NC2, 1);
            ok &= HexEqual(stream, 16, "71ece9e1b158ae06620fe4a5e6f3f420");
        }

        // X25519 RFC 7748 vectors (base point + Diffie-Hellman).
        {
            uint8_t scalar[32], expect[32], out[32];
            const char* alicePriv = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
            for (int i = 0; i < 32; ++i)
            {
                char buf[3] = { alicePriv[i * 2], alicePriv[i * 2 + 1], 0 };
                scalar[i] = uint8_t(strtoul(buf, nullptr, 16));
            }
            X25519Base(out, scalar);
            ok &= HexEqual(out, 32, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");

            const char* bobPub = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
            for (int i = 0; i < 32; ++i)
            {
                char buf[3] = { bobPub[i * 2], bobPub[i * 2 + 1], 0 };
                expect[i] = uint8_t(strtoul(buf, nullptr, 16));
            }
            X25519(out, scalar, expect);
            ok &= HexEqual(out, 32, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
        }

        // The full session derivation, modulo the record, against the campaign's live pair
        // (the build's default key): secret 22dfa316... against server key 1a03d4c7...
        {
            const char* secretHex = "22dfa316a864ff398609b0b9ea1edec46ec6f7097a3d215dddfa1674e3d7f0f5";
            uint8_t secret[32];
            for (int i = 0; i < 32; ++i)
            {
                char buf[3] = { secretHex[i * 2], secretHex[i * 2 + 1], 0 };
                secret[i] = uint8_t(strtoul(buf, nullptr, 16));
            }
            uint8_t serverPublic[32];
            for (int i = 0; i < 32; ++i)
            {
                char buf[3] = { ASCENSION_NATIVE_LOGON_SERVER_KEY[i * 2], ASCENSION_NATIVE_LOGON_SERVER_KEY[i * 2 + 1], 0 };
                serverPublic[i] = uint8_t(strtoul(buf, nullptr, 16));
            }

            Session session;
            ok &= DeriveSession(session, secret, serverPublic);
            ok &= HexEqual(session.clientPublic, 32, "1a03d4c71f289f39046fad519f8af3e92b793709a8a340e492b4ff5ff55e1537");
            ok &= HexEqual(session.keyDerived, 32, "efed6b4faffe5508e8f4186b31e6e7bfd612ce59170e2c448a66f25f4b82dbea");
            ok &= HexEqual(session.keySession, 40, "2c110c92d89a13491f3f1ef7aa87f13f7dc7846fc938e06f9098e85baf798e7a155ab33992991163");
            ok &= HexEqual(session.proof, 32, "a86c61cf8bfdb8f3ed4c5edd301f1b6219ee1c3ae6f0b25142e7416cc8e35869");

            // The whole hello record, byte for byte, against the reference implementation.
            uint8_t nonce[12], tag[16], passwordTag[16];
            for (int i = 0; i < 12; ++i) nonce[i] = uint8_t(i);
            for (int i = 0; i < 16; ++i) tag[i] = uint8_t(16 + i);
            for (int i = 0; i < 16; ++i) passwordTag[i] = uint8_t(32 + i);

            uint8_t record[1200];
            size_t length = BuildHelloRecord(serverPublic, session.keyDerived, session.clientPublic,
                nonce, tag, passwordTag, "LOCAL", "copilot", record, sizeof(record));

            static const char kRecordHex[] =
                "7802fcf4f4e6101112131415161718191a1b1c1d1e1fad7d35706dd473d8ebd66a7f3a782bf502684f5b194c19910abe6ea4"
                "a013b307d8c2ebf3998f3cdca3a29ca76b0698ba1b5cd06ae1f7b37b95f060e919b8a700f585ea18836cc89af0f87710248d"
                "81daa1b6163ff88d194ae6c7a5856a5cfbcba3f5cd3f644bb97a7f412ceea511d8c58290ed83a41f909a0199e62b28543438"
                "6b0a408dbf89b731a3404b23d173fee87d6cdd6634cdb0079437bc236cd1db346307b234f8ff6fc697c268de45178f8493f0"
                "b60f4ce4998f037f6bb385d178ac9e91dfe40aedc7007064d9d74dba5777776835c5001e774d9767e15cc5f8a2b9d51ac0ba"
                "f584bf6902a4df54114d87238046d0aee0a32fa155f6868d197968c366cda47f0cfe00341775b497bd170d072a2988b025bd"
                "8b470abae873d564e92bae8c06e2b5ab81c32acb0842b237219774c3a0c3f0c5a7c2c26ddf2c1ff33ffaa4dcaa2163229c2d"
                "8687eb5891902b8d5f131bb3c6cecc9c95a158199f8553f22138735ee7a96417a90e0cb7a96fd189aed18ba124720068b8b2"
                "06f1f0b65d2a397778d15144cd4cf530a16bd2a46dd4e975c48d0ce6488d9bbaedbca73cce9dde67e5d95577c5341fa982a0"
                "f726b3fd581e77e355a9670ae88602312b124ecf9caf51a603500b15b8e1daf7c6895d143c56ac7757e8d8e9f258e42d8d0c"
                "40782dc714d750f56e36d51e28938921de162cbe7af586fcf32e5bb69f1af225d9b4a375a2ad2a8764bf429251c844710d4c"
                "ea5a79c0929f25c70281747599695e4d37842451233beb458401cf51b9a720d81d85e9b9fca8c9513e8d70f88c688af39449"
                "5e48f787d61d1f6f5c5c10a8dfec34576d8b9bf768ad2d37d7acb67c9465fafbe88b";

            ok &= (length == strlen(kRecordHex) / 2) && HexEqual(record, length, kRecordHex);
        }

        if (!ok)
            AscLog::Printf("native-logon: crypto self-test FAILED");
        return ok;
    }

    // ---------------------------------------------------------------------------------------------
    // Patch tools (the same protect / write / flush pattern as the other attach code).
    // ---------------------------------------------------------------------------------------------

    bool WriteMemory(uint32_t address, const uint8_t* bytes, size_t length)
    {
        void* target = reinterpret_cast<void*>(uintptr_t(address));
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        memcpy(target, bytes, length);
        VirtualProtect(target, length, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), target, length);
        return true;
    }

    bool JumpHook(uint32_t site, const uint8_t* expect, size_t expectLength, const void* target, const char* what)
    {
        const uint8_t* code = reinterpret_cast<const uint8_t*>(uintptr_t(site));
        if (memcmp(code, expect, expectLength) != 0)
        {
            if (code[0] == 0xE9)
            {
                AscLog::Printf("native-logon: %s: already patched", what);
                return true;
            }
            AscLog::Printf("native-logon: %s: unexpected bytes at 0x%08X (%02X %02X %02X %02X %02X) - NOT patched",
                what, site, code[0], code[1], code[2], code[3], code[4]);
            return false;
        }

        uint8_t patch[5];
        patch[0] = 0xE9;
        int32_t relative = int32_t(intptr_t(target) - intptr_t(site + 5));
        memcpy(patch + 1, &relative, sizeof(relative));
        if (!WriteMemory(site, patch, sizeof(patch)))
        {
            AscLog::Printf("native-logon: %s: write failed at 0x%08X", what, site);
            return false;
        }
        AscLog::Printf("native-logon: %s: 0x%08X -> %p", what, site, target);
        return true;
    }

    bool WriteConstant(uint32_t site, const uint8_t* expect, size_t length, const uint8_t* bytes, const char* what)
    {
        const uint8_t* code = reinterpret_cast<const uint8_t*>(uintptr_t(site));
        if (memcmp(code, bytes, length) == 0)
        {
            AscLog::Printf("native-logon: %s: already applied", what);
            return true;
        }
        if (memcmp(code, expect, length) != 0)
        {
            AscLog::Printf("native-logon: %s: unexpected bytes at 0x%08X - NOT patched", what, site);
            return false;
        }
        if (!WriteMemory(site, bytes, length))
        {
            AscLog::Printf("native-logon: %s: write failed at 0x%08X", what, site);
            return false;
        }
        AscLog::Printf("native-logon: %s: 0x%08X patched", what, site);
        return true;
    }
}

namespace AscNativeLogon
{
    void Install()
    {
        static bool installed = false;
        if (installed)
            return;

        AscLog::Printf("native-logon: installing Ascension's own logon protocol (custom auth reconstruction)");
#if ASC_X64
        // x64: JumpHook/WriteConstant escrevem E9 rel32 e bytes x86 em enderecos 12340 crus; no WowClient64 os
        // sites sao outros (CalculateProof/VerifyServerProof por MinHook + os 4 gates por WriteCodeX64). Nao
        // portado -- a opcao ASCENSION_NATIVE_LOGON fica sem efeito e o cliente usa o SRP6 de fabrica.
        AscLog::Printf("native-logon: x64 nao portado -- cliente mantem SRP6 de fabrica");
        return;
#endif

        if (!SelfTest())
        {
            AscLog::Printf("native-logon: self-test failed, not installing (client keeps stock SRP6)");
            return;
        }
        if (!ParseServerKey())
        {
            AscLog::Printf("native-logon: ASCENSION_NATIVE_LOGON_SERVER_KEY is not 64 hex characters, not installing");
            return;
        }
        g_serverKeyValid = true;

        bool ok = true;

        // The three exe thunks the genuine DLL replaces (byte patterns from the deployed client,
        // Ascension.exe D4CA3DD1; Ascension.exe loads at its fixed base 0x400000).
        static const uint8_t expectHello[] = { 0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x8B, 0x75, 0x08 };  // push ebp; mov ebp,esp; push ecx; push ebx; push esi; mov esi,[ebp+8]
        static const uint8_t expectKey[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x5C, 0x01, 0x00, 0x00 };    // push ebp; mov ebp,esp; sub esp,0x15C
        static const uint8_t expectProof[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x83, 0x7D, 0x0C, 0x14 }; // ...; cmp dword ptr [ebp+0Ch], 14h
        ok &= JumpHook(0x008CCE00, expectHello, sizeof(expectHello), &HookHelloSender, "hello serializer 0x8CCE00");
        ok &= JumpHook(0x009A83E0, expectKey, sizeof(expectKey), &HookKeyMaterial, "key material 0x9A83E0");
        ok &= JumpHook(0x009A88C0, expectProof, sizeof(expectProof), &HookProofCheck, "proof check 0x9A88C0");

        // The four handshake length gates: the reply checker must read and pass 32 bytes (0x20)
        // instead of the stock SRP6 20 (0x14). Two inline pushes in the reply handler and two
        // out-of-line trampolines in the exe's .local section.
        static const uint8_t expectPush14[] = { 0x6A, 0x14 };
        static const uint8_t expectImm14[] = { 0x14, 0x00, 0x00, 0x00 };
        static const uint8_t newPush20[] = { 0x6A, 0x20 };
        static const uint8_t newImm20[] = { 0x20, 0x00, 0x00, 0x00 };
        ok &= WriteConstant(0x008CC7FA, expectPush14, sizeof(expectPush14), newPush20, "proof length gate 0x8CC7FA");
        ok &= WriteConstant(0x008CC856, expectPush14, sizeof(expectPush14), newPush20, "proof length gate 0x8CC856");
        ok &= WriteConstant(0x00DFC024, expectImm14, sizeof(expectImm14), newImm20, "read length trampoline 0xDFC024");
        ok &= WriteConstant(0x00DFC034, expectImm14, sizeof(expectImm14), newImm20, "check length trampoline 0xDFC034");

        installed = ok;
        AscLog::Printf(ok
            ? "native-logon: installed (custom X25519/HMAC handshake, server key %s)"
            : "native-logon: INCOMPLETE - some sites were left untouched, see the log above",
            ASCENSION_NATIVE_LOGON_SERVER_KEY);
    }
}

#else

namespace AscNativeLogon
{
    void Install() {}
}

#endif
