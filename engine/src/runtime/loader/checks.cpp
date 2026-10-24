// SPDX-License-Identifier: MIT
// Cross-field rules of the instrument file (B1-R1; docs/dev/instrument-format.md, "Cross-field
// rules"). They run only on a structurally valid file, so every member has a legal type. The Python
// loader (pme_compiler.schema.validate) implements the same rules with the same paths and codes.
#include <set>

#include "reader.hpp"

namespace pme::loader::detail {

namespace {

void fail(ErrorList& errors, std::string path, Code code, std::string message) {
    errors.add(LoadError{std::move(path), toString(code), std::move(message)});
}

// SPEC-API "Event addressing": key is unique among groups with key >= 0.
void checkKeysUnique(const InstrumentDesc& d, ErrorList& errors) {
    std::set<int> seen;
    for (std::size_t g = 0; g < d.groups.size(); ++g) {
        const int key = d.groups[g].key;
        if (key >= 0 && !seen.insert(key).second) {
            fail(errors, "/instrument/groups/" + std::to_string(g) + "/key", Code::Duplicate,
                 "key " + std::to_string(key) + " is used by an earlier group");
        }
    }
}

// SI-0010 (orchestrator default): the tuning rule applies to speaking (AtL) segments only, and a
// rear or duplex (At0) segment takes the tension of the speaking segment just before it. A group's
// first segment must therefore be a speaking segment. This is the only place the rule is checked.
void checkSegmentOrder(const NoteGroupDesc& group, const std::string& groupPath,
                       ErrorList& errors) {
    if (!group.segments.empty() && group.segments.front().string.bridgeEnd == BridgeEnd::At0) {
        fail(errors, groupPath + "/segments/0/string/bridgeEnd", Code::Structure,
             "the first segment of a group must be a speaking segment (AtL); a rear segment "
             "continues the speaking segment before it (SI-0010)");
    }
}

void checkSegment(const SegmentDesc& seg, const std::string& path, std::size_t bridgePoints,
                  ErrorList& errors) {
    if (seg.bridgePoint >= 0 && static_cast<std::size_t>(seg.bridgePoint) >= bridgePoints) {
        fail(errors, path + "/bridgePoint", Code::Reference,
             "expected an index below " + std::to_string(bridgePoints) + " (bridgePoints size)");
    }
    if (seg.string.dOuter < seg.string.dCore) {
        fail(errors, path + "/string/dOuter", Code::Range,
             "expected an outer diameter of at least dCore");
    }
    for (std::size_t k = 0; k < seg.barriers.size(); ++k) {
        const BarrierParams& b = seg.barriers[k];
        if (b.x.size() != b.b.size() || b.x.size() != b.w.size()) {
            fail(errors, path + "/barriers/" + std::to_string(k), Code::Structure,
                 "expected x, b and w to have the same length");
        }
    }
}

void checkPlate(const BodyParams& body, ErrorList& errors) {
    const PlateParams& p = body.plate;
    const std::string path = "/instrument/body/plate";
    if (!p.thicknessGrid.empty()) {
        if (p.thicknessNx < 2) {
            fail(errors, path + "/thicknessNx", Code::Range,
                 "expected at least 2 grid points when thicknessGrid is given");
        }
        if (p.thicknessNy < 2) {
            fail(errors, path + "/thicknessNy", Code::Range,
                 "expected at least 2 grid points when thicknessGrid is given");
        }
        if (p.thicknessNx >= 2 && p.thicknessNy >= 2 &&
            p.thicknessGrid.size() !=
                static_cast<std::size_t>(p.thicknessNx) * static_cast<std::size_t>(p.thicknessNy)) {
            fail(errors, path + "/thicknessGrid", Code::Structure,
                 "expected thicknessNx * thicknessNy = " +
                     std::to_string(static_cast<std::size_t>(p.thicknessNx) *
                                    static_cast<std::size_t>(p.thicknessNy)) +
                     " values");
        }
    }
    if (p.outline.size() == 1 || p.outline.size() == 2) {
        fail(errors, path + "/outline", Code::Structure,
             "expected no vertices (rectangle) or at least 3");
    }
    if (body.source != BodySource::PlateModel) {
        return;
    }
    // The plate is only meaningful for the PlateModel source; the member bounds are therefore
    // "at least 0" and strict positivity is required here.
    const auto positive = [&](double v, const char* key) {
        if (!(v > 0.0)) {
            fail(errors, path + "/" + key, Code::Range,
                 "expected a value greater than 0 when source is PlateModel");
        }
    };
    positive(p.youngsL, "youngsL");
    positive(p.youngsR, "youngsR");
    positive(p.shearLR, "shearLR");
    positive(p.density, "density");
    positive(p.fMax, "fMax");
    if (p.thicknessGrid.empty()) {
        positive(p.thickness, "thickness");
    }
    if (p.outline.empty()) {
        positive(p.lengthX, "lengthX");
        positive(p.lengthY, "lengthY");
    }
}

void checkModalHash(const InstrumentFile& file, ErrorList& errors) {
    const BodyParams& body = file.desc.body;
    const std::string path = "/instrument/body/modalTableHash";
    if (body.source == BodySource::PlateModel) {
        if (!body.modalTableHash.empty()) {
            fail(errors, path, Code::Structure, "expected an empty hash when source is PlateModel");
        }
        return;
    }
    bool found = false;
    for (const BlobEntry& b : file.blobs) {
        found = found || b.hash == body.modalTableHash;
    }
    if (!found) {
        fail(errors, path, Code::Reference, "expected the hash of an entry of blobs");
    }
}

}  // namespace

void checkCrossFieldRules(const InstrumentFile& file, ErrorList& errors) {
    const InstrumentDesc& d = file.desc;
    checkKeysUnique(d, errors);
    for (std::size_t g = 0; g < d.groups.size(); ++g) {
        const std::string groupPath = "/instrument/groups/" + std::to_string(g);
        checkSegmentOrder(d.groups[g], groupPath, errors);
        for (std::size_t s = 0; s < d.groups[g].segments.size(); ++s) {
            checkSegment(d.groups[g].segments[s], groupPath + "/segments/" + std::to_string(s),
                         d.bridgePoints.size(), errors);
        }
    }
    checkPlate(d.body, errors);
    checkModalHash(file, errors);
}

}  // namespace pme::loader::detail
