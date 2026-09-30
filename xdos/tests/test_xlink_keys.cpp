// SPDX-License-Identifier: Apache-2.0
// Verifies the vendor product-key file parses correctly.
// Uses the real 20260223_Product_Key_XLK0151.txt shipped in mib/xlink_keys/.

#include "subsystems/xlink_keys.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("XLink key catalog: real vendor file loads all 23 entries", "[xlink]") {
    // Locate the file — tests run from build/, mib is one level up
    namespace fs = std::filesystem;
    fs::path candidates[] = {
        "mib/xlink_keys/20260223_Product_Key_XLK0151.txt",
        "../mib/xlink_keys/20260223_Product_Key_XLK0151.txt",
        "../../mib/xlink_keys/20260223_Product_Key_XLK0151.txt",
    };
    std::string path;
    for (auto& c : candidates) if (fs::exists(c)) { path = c.string(); break; }
    REQUIRE_FALSE(path.empty());

    auto cat = xdos::xlink::KeyCatalog::load(path);
    REQUIRE(cat.has_value());

    // Metadata
    REQUIRE(cat->serial_number == "2223010151");
    REQUIRE(cat->mac == "00-05-43-01-52-AD");
    REQUIRE(cat->unit_model.find("XLK-SATX18C") != std::string::npos);

    // 23 (XmtSymbFreq, RcvSymbFreq) configurations documented in the file
    REQUIRE(cat->entries.size() == 23);

    // Spot-check specific entries against the raw file content
    auto* e1 = cat->find(2048, 512);
    REQUIRE(e1 != nullptr);
    REQUIRE(e1->raw_key_text == "ad563c56-ea8bf594-953e59c4-071d177f");
    // First key word 0xAD563C56 → LE bytes 56 3C 56 AD
    REQUIRE(e1->key[0] == 0x56);
    REQUIRE(e1->key[1] == 0x3C);
    REQUIRE(e1->key[3] == 0xAD);

    auto* e2 = cat->find(2048, 64);
    REQUIRE(e2 != nullptr);
    REQUIRE(e2->raw_key_text == "a9573c54-6a8bb594-953e59c4-07151377");

    // XmtSymbFreq=32/RcvSymbFreq=32 special-case entry from the file
    auto* e3 = cat->find(32, 32);
    REQUIRE(e3 != nullptr);
    REQUIRE(e3->raw_key_text == "eb77bc56-ee8bf594-943e59c7-0c9d1377");

    // Unsupported configuration
    REQUIRE(cat->find(9999, 9999) == nullptr);

    // Feature flags parsed for at least the first entry
    REQUIRE_FALSE(e1->features.empty());
    bool has_xmtch0 = false, has_spi = false;
    for (auto const& f : e1->features) {
        if (f == "XmtCh0_En") has_xmtch0 = true;
        if (f == "Use SPI")   has_spi = true;
    }
    REQUIRE(has_xmtch0);
    REQUIRE(has_spi);
}
