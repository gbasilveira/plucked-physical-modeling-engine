// SPDX-License-Identifier: MIT
// Error collection, validating reader and canonical number/text formatting (B1-R4, B1-R5).
#include "reader.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>

namespace pme::loader::detail {

const char* toString(Code code) noexcept {
    switch (code) {
        case Code::Parse:
            return "parse";
        case Code::Type:
            return "type";
        case Code::Missing:
            return "missing";
        case Code::UnknownKey:
            return "unknown_key";
        case Code::Range:
            return "range";
        case Code::Enum:
            return "enum";
        case Code::Version:
            return "version";
        case Code::Migration:
            return "migration";
        case Code::Reference:
            return "reference";
        case Code::Duplicate:
            return "duplicate";
        case Code::Structure:
            return "structure";
        case Code::Blob:
            return "blob";
    }
    return "structure";
}

std::string escapePointer(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// ---- ErrorList --------------------------------------------------------------------------------

void ErrorList::add(LoadError error) { items_.push_back(std::move(error)); }

std::vector<LoadError> ErrorList::take() {
    // Sort by (path, code), keep the first message of each pair, cap at kMaxErrors (B1-R5). The
    // Python loader applies the same rule, so both report the same (path, code) pairs.
    std::stable_sort(items_.begin(), items_.end(), [](const LoadError& a, const LoadError& b) {
        return a.path != b.path ? a.path < b.path : a.code < b.code;
    });
    // A `type` error at a path hides every other error at that path (the value has no usable type);
    // the list is sorted by path, so the errors of one path are adjacent.
    std::vector<LoadError> filtered;
    std::size_t begin = 0;
    while (begin < items_.size()) {
        std::size_t end = begin;
        bool hasType = false;
        while (end < items_.size() && items_[end].path == items_[begin].path) {
            hasType = hasType || items_[end].code == "type";
            ++end;
        }
        for (std::size_t i = begin; i < end; ++i) {
            const bool duplicate = i > begin && items_[i].code == items_[i - 1].code;
            if (!duplicate && (!hasType || items_[i].code == "type")) {
                filtered.push_back(items_[i]);
            }
        }
        begin = end;
    }
    if (filtered.size() > kMaxErrors) {
        filtered.resize(kMaxErrors);
    }
    items_.clear();
    return filtered;
}

// ---- Reader -----------------------------------------------------------------------------------

void Reader::error(Code code, std::string message) const {
    errors_->add(LoadError{path_, toString(code), std::move(message)});
}

void Reader::errorAt(std::string_view key, Code code, std::string message) const {
    errors_->add(LoadError{path_ + "/" + escapePointer(key), toString(code), std::move(message)});
}

Reader Reader::child(std::string_view key, const Json& node) const {
    return Reader(*errors_, node, path_ + "/" + escapePointer(key));
}

Reader Reader::child(std::size_t index, const Json& node) const {
    return Reader(*errors_, node, path_ + "/" + std::to_string(index));
}

// ---- ObjectScope ------------------------------------------------------------------------------

namespace {

bool within(double v, const Bound& b) {
    if (b.hasLo && (b.loExclusive ? v <= b.lo : v < b.lo)) {
        return false;
    }
    if (b.hasHi && (b.hiExclusive ? v >= b.hi : v > b.hi)) {
        return false;
    }
    return true;
}

std::string describe(const Bound& b) {
    std::string text = "expected a number";
    if (b.hasLo) {
        text += std::string(b.loExclusive ? " greater than " : " of at least ") + formatDouble(b.lo);
    }
    if (b.hasHi) {
        text += std::string(b.hasLo ? " and" : "") +
                (b.hiExclusive ? " less than " : " of at most ") + formatDouble(b.hi);
    }
    return text;
}

bool isHash(const std::string& s) {
    if (s.size() != 71 || s.compare(0, 7, "sha256:") != 0) {
        return false;
    }
    return std::all_of(s.begin() + 7, s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

}  // namespace

ObjectScope::ObjectScope(const Reader& r) : r_(&r) {
    if (!r.node().is_object()) {
        r.error(Code::Type, "expected an object");
        ok_ = false;
    }
}

std::size_t ObjectScope::size() const noexcept { return ok_ ? r_->node().size() : 0; }

bool ObjectScope::has(std::string_view key) const {
    return ok_ && r_->node().contains(std::string(key));
}

const Json* ObjectScope::member(std::string_view key, bool required) {
    known_.push_back(key);
    if (!ok_) {
        return nullptr;
    }
    const Json& node = r_->node();
    const auto it = node.find(std::string(key));
    if (it == node.end()) {
        if (required) {
            r_->errorAt(key, Code::Missing, "required member is missing");
        }
        return nullptr;
    }
    return &*it;
}

const Json* ObjectScope::arrayMember(std::string_view key, bool required, std::size_t minItems) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return nullptr;
    }
    if (!m->is_array()) {
        r_->errorAt(key, Code::Type, "expected an array");
        return nullptr;
    }
    if (m->size() < minItems) {
        r_->errorAt(key, Code::Structure,
                    "expected at least " + std::to_string(minItems) + " items");
    }
    return m;
}

void ObjectScope::number(std::string_view key, double& out, bool required, const Bound& bound) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return;
    }
    const Reader c = r_->child(key, *m);
    if (!m->is_number()) {
        c.error(Code::Type, "expected a number");
        return;
    }
    const double v = m->get<double>();
    if (!within(v, bound)) {
        c.error(Code::Range, describe(bound));
        return;
    }
    out = v;
}

void ObjectScope::boolean(std::string_view key, bool& out, bool required) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return;
    }
    if (!m->is_boolean()) {
        r_->child(key, *m).error(Code::Type, "expected true or false");
        return;
    }
    out = m->get<bool>();
}

void ObjectScope::text(std::string_view key, std::string& out, bool required, TextRule rule) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return;
    }
    const Reader c = r_->child(key, *m);
    if (!m->is_string()) {
        c.error(Code::Type, "expected a string");
        return;
    }
    const std::string& s = m->get_ref<const std::string&>();
    const bool bad = (rule == TextRule::Hash && !isHash(s)) ||
                     (rule == TextRule::HashOrEmpty && !s.empty() && !isHash(s));
    if (bad) {
        c.error(Code::Type, "expected 'sha256:' followed by 64 lower-case hex digits");
        return;
    }
    out = s;
}

void ObjectScope::numbers(std::string_view key, std::vector<double>& out, bool required,
                          const Bound& element, std::size_t minItems) {
    const Json* m = arrayMember(key, required, minItems);
    if (m == nullptr) {
        return;
    }
    const Reader c = r_->child(key, *m);
    std::vector<double> result;
    result.reserve(m->size());
    for (std::size_t i = 0; i < m->size(); ++i) {
        const Json& item = (*m)[i];
        if (!item.is_number()) {
            c.child(i, item).error(Code::Type, "expected a number");
            continue;
        }
        const double v = item.get<double>();
        if (!within(v, element)) {
            c.child(i, item).error(Code::Range, describe(element));
            continue;
        }
        result.push_back(v);
    }
    out = std::move(result);
}

void ObjectScope::choice(std::string_view key, std::string& out, bool required,
                         const std::vector<std::string_view>& values) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return;
    }
    const Reader c = r_->child(key, *m);
    if (!m->is_string()) {
        c.error(Code::Type, "expected a string");
        return;
    }
    const std::string& s = m->get_ref<const std::string&>();
    if (std::find(values.begin(), values.end(), std::string_view(s)) == values.end()) {
        std::string list;
        for (const std::string_view v : values) {
            list += (list.empty() ? "" : ", ") + std::string(v);
        }
        c.error(Code::Enum, "expected one of: " + list);
        return;
    }
    out = s;
}

std::string ObjectScope::join(const char* const* names, std::size_t count) {
    std::string list;
    for (std::size_t i = 0; i < count; ++i) {
        list += (list.empty() ? "" : ", ") + std::string(names[i]);
    }
    return list;
}

bool ObjectScope::checkInteger(const Reader& c, std::int64_t& out, const IntRule& rule) {
    const Json& n = c.node();
    std::int64_t v = 0;
    constexpr auto kInt64Max = std::numeric_limits<std::int64_t>::max();
    if (n.is_number_unsigned()) {
        const auto u = n.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(kInt64Max)) {
            c.error(Code::Range, "integer out of range");
            return false;
        }
        v = static_cast<std::int64_t>(u);
    } else if (n.is_number_integer()) {
        v = n.get<std::int64_t>();
    } else if (n.is_number_float()) {
        const double d = n.get<double>();
        if (std::floor(d) != d) {
            c.error(Code::Type, "expected an integer");
            return false;
        }
        if (d < -9.0e18 || d > 9.0e18) {
            c.error(Code::Range, "integer out of range");
            return false;
        }
        v = static_cast<std::int64_t>(d);
    } else {
        c.error(Code::Type, "expected an integer");
        return false;
    }
    bool good = true;
    if ((rule.hasLo && v < rule.lo) || (rule.hasHi && v > rule.hi)) {
        std::string text = "expected an integer";
        if (rule.hasLo) {
            text += " of at least " + std::to_string(rule.lo);
        }
        if (rule.hasHi) {
            text += std::string(rule.hasLo ? " and" : "") + " of at most " + std::to_string(rule.hi);
        }
        c.error(Code::Range, text);
        good = false;
    }
    if (rule.hasOneOf && v != rule.oneOf[0] && v != rule.oneOf[1]) {
        c.error(Code::Enum, "expected one of: " + std::to_string(rule.oneOf[0]) + ", " +
                                std::to_string(rule.oneOf[1]));
        good = false;
    }
    if (good) {
        out = v;
    }
    return good;
}

bool ObjectScope::readInteger(std::string_view key, std::int64_t& out, bool required,
                              const IntRule& rule) {
    const Json* m = member(key, required);
    if (m == nullptr) {
        return false;
    }
    return checkInteger(r_->child(key, *m), out, rule);
}

void ObjectScope::finish() {
    if (!ok_) {
        return;
    }
    for (const auto& item : r_->node().items()) {
        const std::string& key = item.key();
        if (std::find(known_.begin(), known_.end(), std::string_view(key)) == known_.end()) {
            r_->errorAt(key, Code::UnknownKey, "unknown member");
        }
    }
}

// ---- Canonical number and text formatting -----------------------------------------------------

std::string formatDouble(double value) {
    if (value == 0.0) {
        return std::signbit(value) ? "-0.0" : "0.0";
    }
    constexpr int kMinExp = -4;  // nlohmann/json format_buffer thresholds
    constexpr int kMaxExp = 15;
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), std::fabs(value),
                                   std::chars_format::scientific);
    const std::string text(buf.data(), res.ptr);
    // text is d[.ddd]e[+-]XX; recover the digits and the decimal point position n with
    // value = 0.DIGITS * 10^n.
    const std::size_t epos = text.find('e');
    std::string digits;
    for (std::size_t i = 0; i < epos; ++i) {
        if (text[i] != '.') {
            digits.push_back(text[i]);
        }
    }
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    const int exp10 = std::atoi(text.c_str() + epos + 1);
    const int n = exp10 + 1;
    const int k = static_cast<int>(digits.size());
    std::string body;
    if (k <= n && n <= kMaxExp) {
        body = digits + std::string(static_cast<std::size_t>(n - k), '0') + ".0";
    } else if (0 < n && n <= kMaxExp) {
        body = digits.substr(0, static_cast<std::size_t>(n)) + "." +
               digits.substr(static_cast<std::size_t>(n));
    } else if (kMinExp < n && n <= 0) {
        body = "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    } else {
        const int e = n - 1;
        body = digits.substr(0, 1);
        if (k > 1) {
            body += "." + digits.substr(1);
        }
        const std::string mag = std::to_string(std::abs(e));
        body += std::string("e") + (e < 0 ? "-" : "+") + (mag.size() < 2 ? "0" : "") + mag;
    }
    return std::signbit(value) ? "-" + body : body;
}

namespace {

std::string quote(const std::string& s) {
    return OJson(s).dump(-1, ' ', false, OJson::error_handler_t::replace);
}

void append(const OJson& v, std::size_t indent, std::string& out) {
    const std::string pad(indent + 2, ' ');
    const std::string end(indent, ' ');
    switch (v.type()) {
        case OJson::value_t::object: {
            if (v.empty()) {
                out += "{}";
                return;
            }
            out += "{\n";
            bool first = true;
            for (const auto& item : v.items()) {
                out += first ? "" : ",\n";
                first = false;
                out += pad + quote(item.key()) + ": ";
                append(item.value(), indent + 2, out);
            }
            out += "\n" + end + "}";
            return;
        }
        case OJson::value_t::array: {
            if (v.empty()) {
                out += "[]";
                return;
            }
            out += "[\n";
            bool first = true;
            for (const auto& item : v) {
                out += first ? "" : ",\n";
                first = false;
                out += pad;
                append(item, indent + 2, out);
            }
            out += "\n" + end + "]";
            return;
        }
        case OJson::value_t::string:
            out += quote(v.get_ref<const std::string&>());
            return;
        case OJson::value_t::boolean:
            out += v.get<bool>() ? "true" : "false";
            return;
        case OJson::value_t::number_unsigned:
            out += std::to_string(v.get<std::uint64_t>());
            return;
        case OJson::value_t::number_integer:
            out += std::to_string(v.get<std::int64_t>());
            return;
        case OJson::value_t::number_float:
            out += formatDouble(v.get<double>());
            return;
        default:
            out += "null";
            return;
    }
}

}  // namespace

std::string serialise(const OJson& root) {
    std::string out;
    append(root, 0, out);
    out.push_back('\n');
    return out;
}

}  // namespace pme::loader::detail
