// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-INST "Instrument file" (sample-rate-dependent data is never
// cached), SPEC-API "Compile functions", SPEC-BODY "Local bridge model". D5-R4.
//
// The key encoding is written field by field. Each struct is bound with a structured binding that
// names every member, so adding a member to BodyParams, PlateParams, RibDesc, BridgeBeamDesc or
// Point is a compile error here until the new member is encoded (and kCacheFormatVersion bumped).
#include <algorithm>
#include <cmath>

#include "byte_io.hpp"
#include "pme/loader/sha256.hpp"
#include "pme/plugin/library/cache.hpp"
#include "pme/version.hpp"

namespace pme::plugin::library {
namespace {

using detail::ByteWriter;

void put(ByteWriter& w, const Point& p) {
    const auto& [x, y, z] = p;
    w.f64(x);
    w.f64(y);
    w.f64(z);
}

void put(ByteWriter& w, const RibDesc& r) {
    const auto& [start, end, width, height, youngs, density] = r;
    put(w, start);
    put(w, end);
    w.f64(width);
    w.f64(height);
    w.f64(youngs);
    w.f64(density);
}

void put(ByteWriter& w, const BridgeBeamDesc& b) {
    const auto& [path, width, height, youngs, density] = b;
    w.u64(path.size());
    for (const Point& p : path) {
        put(w, p);
    }
    w.f64(width);
    w.f64(height);
    w.f64(youngs);
    w.f64(density);
}

void put(ByteWriter& w, const PlateParams& plate) {
    const auto& [lengthX, lengthY, thickness, grainAngle, youngsL, youngsR, shearLR, poissonLR,
                 poissonRL, density, moisture, dEdMcL, dEdMcR, dGdMc, dRhodMc, lossFactor,
                 rimStiffness, fMax, ribs, outline, thicknessNx, thicknessNy, thicknessGrid,
                 bridges, rimRotationalStiffness] = plate;
    for (const double v :
         {lengthX, lengthY, thickness, grainAngle, youngsL, youngsR, shearLR, poissonLR, poissonRL,
          density, moisture, dEdMcL, dEdMcR, dGdMc, dRhodMc, lossFactor, rimStiffness, fMax}) {
        w.f64(v);
    }
    w.u64(ribs.size());
    for (const RibDesc& r : ribs) {
        put(w, r);
    }
    w.u64(outline.size());
    for (const Point& p : outline) {
        put(w, p);
    }
    w.i32(thicknessNx);
    w.i32(thicknessNy);
    w.f64s(thicknessGrid);
    w.u64(bridges.size());
    for (const BridgeBeamDesc& b : bridges) {
        put(w, b);
    }
    w.f64(rimRotationalStiffness);
}

void put(ByteWriter& w, const BodyParams& body) {
    const auto& [source, plate, modalTableHash] = body;
    w.u8(static_cast<std::uint8_t>(source));
    put(w, plate);
    w.str(modalTableHash);
}

void putHeader(ByteWriter& w, std::string_view tag, std::string_view producerVersion) {
    w.str(tag);
    w.u32(kCacheFormatVersion);
    w.i32(interfacesVersionMajor);
    w.str(producerVersion);
}

CacheKey digest(const std::string& bytes) {
    loader::Sha256 sha;
    sha.update(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
    return sha.finish();
}

}  // namespace

std::string toHex(const CacheKey& key) { return loader::toHex(key); }

CacheKey bodyKey(const BodyParams& params, std::span<const Point> bridgePoints,
                 std::span<const Point> channels, std::string_view producerVersion) {
    std::string bytes;
    ByteWriter w(bytes);
    putHeader(w, "pme-cache-key-body", producerVersion);
    put(w, params);
    w.u64(bridgePoints.size());
    for (const Point& p : bridgePoints) {
        put(w, p);
    }
    w.u64(channels.size());
    for (const Point& p : channels) {
        put(w, p);
    }
    return digest(bytes);
}

CacheKey localBridgeKey(const CacheKey& body, int point, int maxModes, double fSplit) {
    std::string bytes;
    ByteWriter w(bytes);
    w.str("pme-cache-key-local-bridge");
    w.u32(kCacheFormatVersion);
    w.i32(interfacesVersionMajor);
    w.raw(body.data(), body.size());
    w.i32(point);
    w.i32(maxModes);
    w.f64(fSplit);
    return digest(bytes);
}

Status bodyCacheKeys(const InstrumentDesc& desc, const ParamOverrides& overrides,
                     std::string_view producerVersion, BodyCacheKeys& out, Report& report) {
    try {
        constexpr ParamId kLocalBridgeModes = 7001;  // body.localBridgeModes (structural)
        BodyCacheKeys keys;
        std::vector<Point> points;
        points.reserve(desc.bridgePoints.size());
        for (const BridgePointDesc& b : desc.bridgePoints) {
            points.push_back(b.position);
        }
        std::vector<Point> channels;
        channels.reserve(desc.channels.size());
        for (const ChannelDesc& c : desc.channels) {
            channels.push_back(c.position);
        }
        keys.maxModes = desc.defaults.localBridgeModes;
        for (const auto& [id, value] : overrides) {
            if (id != kLocalBridgeModes) {
                continue;
            }
            if (const ParamInfo* info = findParam(id); info != nullptr && std::isfinite(value)) {
                keys.maxModes =
                    static_cast<int>(std::lround(std::clamp(value, info->min, info->max)));
            }
        }
        keys.fSplit = desc.defaults.localBridgeFSplit;
        keys.body = bodyKey(desc.body, points, channels, producerVersion);
        keys.localBridges.reserve(points.size());
        for (std::size_t b = 0; b < points.size(); ++b) {
            keys.localBridges.push_back(
                localBridgeKey(keys.body, static_cast<int>(b), keys.maxModes, keys.fSplit));
        }
        out = std::move(keys);
        return Status::Ok;
    } catch (...) {
        report.error("", code::kCache, "unexpected failure while computing cache keys");
        return Status::InvalidArgument;
    }
}

}  // namespace pme::plugin::library
