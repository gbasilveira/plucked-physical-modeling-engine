// SPDX-License-Identifier: MIT
// P0.4 stub of compileString (SPEC-STR "Contract"); replaced by phase A2.
#include "pme/string.hpp"

namespace pme {

Status compileString(const StringParams& /*params*/, double /*fs*/, double /*fMax*/,
                     std::span<const double> /*registeredPoints*/, StringModes& /*out*/) {
    return Status::NotImplemented;
}

}  // namespace pme
