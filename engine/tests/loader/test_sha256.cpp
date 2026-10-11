// SPDX-License-Identifier: MIT
// SHA-256 against the FIPS 180-4 example vectors and the padding boundaries.
#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <string>

#include "pme/loader/sha256.hpp"

namespace {

std::string hashOf(const std::string& text) {
    return pme::loader::sha256Hex(std::as_bytes(std::span<const char>(text)));
}

}  // namespace

TEST_CASE("B1-R3 SHA-256 of the FIPS 180-4 one-block message abc") {
    CHECK(hashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("B1-R3 SHA-256 of the empty message") {
    CHECK(hashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("B1-R3 SHA-256 of the FIPS 180-4 two-block message") {
    CHECK(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("B1-R3 SHA-256 of one million letters a fed in uneven pieces") {
    pme::loader::Sha256 sha;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left > 0) {
        const std::size_t take = left < chunk.size() ? left : chunk.size();
        sha.update(std::as_bytes(std::span<const char>(chunk.data(), take)));
        left -= take;
    }
    CHECK(pme::loader::toHex(sha.finish()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("B1-R3 SHA-256 padding boundaries at 55 to 64 bytes") {
    // Reference digests computed with Python hashlib.
    CHECK(hashOf(std::string(55, 'x')) ==
          "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
    CHECK(hashOf(std::string(56, 'x')) ==
          "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
    CHECK(hashOf(std::string(63, 'x')) ==
          "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2");
    CHECK(hashOf(std::string(64, 'x')) ==
          "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
}
