// SPDX-License-Identifier: MIT
// P0.4 stubs of compileBody and buildLocalBridge (SPEC-BODY "Contracts"); replaced by phase B.
#include "pme/body.hpp"

namespace pme {

Status compileBody(const BodyParams& /*params*/, std::span<const Point> /*bridgePoints*/,
                   std::span<const Point> /*channels*/, BodyModes& /*out*/) {
    return Status::NotImplemented;
}

Status buildLocalBridge(const BodyModes& /*body*/, int /*point*/, int /*maxModes*/,
                        double /*fSplit*/, LocalBridge& /*out*/) {
    return Status::NotImplemented;
}

}  // namespace pme
