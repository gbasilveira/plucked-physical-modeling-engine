// SPDX-License-Identifier: MIT
// B2-R7: the B1 loader accepts an instrument whose body the B2 compiler produced. The fixture is
// the test-monochord compiled at a coarse mesh (regenerate: compiler/tests/data/b2/regenerate.sh).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pme/loader/instrument_file.hpp"
#include "test_support.hpp"

namespace {

std::string describe(const std::vector<pme::loader::LoadError>& errors) {
    std::string text;
    for (const auto& e : errors) {
        text += e.path + " " + e.code + " " + e.message + "\n";
    }
    return text;
}

}  // namespace

TEST_CASE("B2-R7 loader accepts a B2-compiled instrument with verifyBlobs") {
    const auto file =
        std::filesystem::path(PME_B2_FIXTURES) / "compiled-test-monochord" / "instrument.json";
    REQUIRE(std::filesystem::exists(file));

    pme::loader::InstrumentFile out;
    std::vector<pme::loader::LoadError> errors;
    const pme::Status status =
        pme::loader::loadInstrumentFile(file, /*verifyBlobs=*/true, out, errors);
    INFO(describe(errors));
    REQUIRE(status == pme::Status::Ok);
    CHECK(errors.empty());

    CHECK(out.desc.name == "test-monochord");
    CHECK(out.desc.body.source == pme::BodySource::ModalTable);
    REQUIRE(out.blobs.size() == 1);
    const auto& blob = out.blobs[0];
    CHECK(out.desc.body.modalTableHash == blob.hash);
    CHECK(blob.tool == "pme_compiler.body");
    CHECK(blob.path == "blobs/body-modal-table.bin");
    CHECK(blob.arrays.size() == 7);  // omega, sigma, phi, points and the radiation grid
    for (const auto& a : blob.arrays) {
        CHECK(a.offset % 64 == 0);
    }

    SUBCASE("a flipped byte in the blob is caught by the hash check") {
        const auto dir = std::filesystem::temp_directory_path() / "pme_b2_r7_corrupt";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "blobs");
        std::filesystem::copy_file(file, dir / "instrument.json");
        std::string bytes = b1test::readFile(file.parent_path() / blob.path);
        REQUIRE_FALSE(bytes.empty());
        bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x01);
        {
            std::ofstream o(dir / blob.path, std::ios::binary);
            o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        pme::loader::InstrumentFile bad;
        std::vector<pme::loader::LoadError> badErrors;
        CHECK(pme::loader::loadInstrumentFile(dir / "instrument.json", true, bad, badErrors) ==
              pme::Status::InvalidArgument);
        CHECK_FALSE(badErrors.empty());
        std::filesystem::remove_all(dir);
    }
}
