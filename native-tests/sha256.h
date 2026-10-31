#pragma once

// Minimal SHA-256 (FIPS 180-4) for D5 corpus integrity verification.
// Test-only code — the corpus manifest pins each clip's SHA-256 and the
// loader refuses clips whose bytes do not match. Not used by the RT path.

#include <cstddef>
#include <cstdint>
#include <string>

namespace d5sha {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t len);
    // Finalizes and returns the 64-char lowercase hex digest.
    std::string hexDigest();

private:
    void block(const std::uint8_t* p);

    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    std::size_t bufLen_ = 0;
    std::uint64_t totalLen_ = 0;  // bytes
    bool done_ = false;
};

// Convenience: hash a whole file. Returns "" on unreadable path.
std::string fileSha256(const std::string& path);

}  // namespace d5sha
