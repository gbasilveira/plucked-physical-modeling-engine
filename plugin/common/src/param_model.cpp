// SPDX-License-Identifier: MIT
// SPEC sections implemented: SPEC-API "Parameter classes" (registry-backed parameter model).
#include "pme/plugin/param_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace pme::plugin {

ParamModel::ParamModel()
    : registry_(paramRegistry()),
      count_(registry_.size()),
      values_(std::make_unique<std::atomic<double>[]>(registry_.size())) {
    resetToDefaults();
}

const ParamInfo& ParamModel::info(std::size_t index) const noexcept { return registry_[index]; }

std::size_t ParamModel::indexOf(ParamId id) const noexcept {
    const auto it = std::lower_bound(registry_.begin(), registry_.end(), id,
                                     [](const ParamInfo& p, ParamId v) { return p.id < v; });
    if (it == registry_.end() || it->id != id) {
        return count_;
    }
    return static_cast<std::size_t>(it - registry_.begin());
}

double ParamModel::value(std::size_t index) const noexcept {
    return values_[index].load(std::memory_order_relaxed);
}

double ParamModel::setValue(std::size_t index, double v) noexcept {
    if (std::isnan(v)) {
        return value(index);
    }
    const double c = clamp(registry_[index], v);
    values_[index].store(c, std::memory_order_relaxed);
    return c;
}

void ParamModel::resetToDefaults() noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        values_[i].store(registry_[i].defaultValue, std::memory_order_relaxed);
    }
}

double ParamModel::clamp(const ParamInfo& info, double v) noexcept {
    if (std::isnan(v)) {
        return info.defaultValue;
    }
    return std::min(std::max(v, info.min), info.max);
}

std::size_t ParamModel::valueToText(const ParamInfo& info, double v, char* out,
                                    std::size_t capacity) noexcept {
    if (capacity == 0) {
        return 0;
    }
    const bool dimensionless = info.unit == "1";
    // The unit is a string_view into static storage and not NUL-terminated by contract.
    const int unitLen = static_cast<int>(info.unit.size());
    const int n = dimensionless
                      ? std::snprintf(out, capacity, "%.9g", v)
                      : std::snprintf(out, capacity, "%.9g %.*s", v, unitLen, info.unit.data());
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return std::min(static_cast<std::size_t>(n), capacity - 1);
}

bool ParamModel::textToValue(const ParamInfo& /*info*/, const char* text, double& out) noexcept {
    if (text == nullptr) {
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || std::isnan(v)) {
        return false;
    }
    out = v;
    return true;
}

std::string ParamModel::modulePath(const ParamInfo& info) {
    const std::string_view name = info.name;
    const std::size_t dot = name.rfind('.');
    std::string module =
        dot == std::string_view::npos ? std::string() : std::string(name.substr(0, dot));
    std::replace(module.begin(), module.end(), '.', '/');
    if (info.cls == ParamClass::Structural) {
        module = module.empty() ? "structural" : "structural/" + module;
    }
    return module;
}

}  // namespace pme::plugin
