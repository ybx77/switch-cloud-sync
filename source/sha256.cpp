#include "sha256.hpp"

namespace {

inline u32 rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

const u32 kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

}  // namespace

void Sha256::reset() {
    h_[0] = 0x6a09e667u;
    h_[1] = 0xbb67ae85u;
    h_[2] = 0x3c6ef372u;
    h_[3] = 0xa54ff53au;
    h_[4] = 0x510e527fu;
    h_[5] = 0x9b05688cu;
    h_[6] = 0x1f83d9abu;
    h_[7] = 0x5be0cd19u;
    len_ = 0;
    buf_len_ = 0;
}

void Sha256::transform(const u8 *block) {
    u32 w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | (u32)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    u32 e = h_[4], f = h_[5], g = h_[6], h = h_[7];

    for (int i = 0; i < 64; ++i) {
        u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + s1 + ch + kK[i] + w[i];
        u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void *data, size_t len) {
    const u8 *p = (const u8 *)data;
    len_ += len;

    if (buf_len_ > 0) {
        size_t need = 64 - buf_len_;
        size_t take = len < need ? len : need;
        memcpy(buf_ + buf_len_, p, take);
        buf_len_ += take;
        p += take;
        len -= take;
        if (buf_len_ == 64) {
            transform(buf_);
            buf_len_ = 0;
        }
    }

    while (len >= 64) {
        transform(p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        memcpy(buf_, p, len);
        buf_len_ = len;
    }
}

std::string Sha256::hexDigest() {
    u64 bit_len = len_ * 8;

    u8 pad = 0x80;
    update(&pad, 1);
    u8 zero = 0;
    while (buf_len_ != 56) update(&zero, 1);

    u8 len_buf[8];
    for (int i = 0; i < 8; ++i) len_buf[i] = (u8)(bit_len >> (56 - 8 * i));
    update(len_buf, 8);

    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            u8 byte = (u8)(h_[i] >> (28 - 4 * j));
            out[i * 8 + j] = kHex[byte & 0x0F];
        }
    }
    return out;
}

std::string sha256File(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return std::string();

    Sha256 sha;
    static u8 buf[0x10000];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha.update(buf, n);
    }
    fclose(f);
    return sha.hexDigest();
}

std::string sha256Data(const void *data, size_t len) {
    Sha256 sha;
    sha.update(data, len);
    return sha.hexDigest();
}
