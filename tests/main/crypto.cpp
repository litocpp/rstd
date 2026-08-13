#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::literals;

TEST(Crypto, Sha256MatchesPublishedVectors) {
    EXPECT_EQ(rstd::crypto::sha256_hex(""_str).as_str(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_str);
    EXPECT_EQ(rstd::crypto::sha256_hex("abc"_str).as_str(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"_str);
    EXPECT_EQ(
        rstd::crypto::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"_str)
            .as_str(),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"_str);
}
