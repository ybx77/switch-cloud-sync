#pragma once

#include "common.hpp"

/// Small self-contained SHA-256 implementation (no external crypto dependency).
class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void update(const void *data, size_t len);
    /// Finishes the hash and returns 64 lowercase hex characters.
    std::string hexDigest();

private:
    void transform(const u8 *block);

    u32 h_[8];
    u64 len_;
    u8 buf_[64];
    size_t buf_len_;
};

std::string sha256File(const char *path);
std::string sha256Data(const void *data, size_t len);
