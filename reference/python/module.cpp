// SPDX-License-Identifier: MIT
// Python bindings of the reference solver (P0.5-R7), nanobind. Module `pme_reference`:
//
//   render(scenario: dict | str) -> dict
//
// The scenario is the JSON scenario of reference/README.md, as a dict or a JSON string. The result
// holds NumPy float64 arrays for the observation signals and the energy trace, and the features
// dict that the CLI writes to its --features file (the same text, parsed).
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <cstddef>
#include <cstring>
#include <string>

#include "pme/analysis/golden.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/features.hpp"
#include "pme/ref/scenario.hpp"
#include "pme/ref/simulation.hpp"

namespace nb = nanobind;

namespace {

using Array = nb::ndarray<nb::numpy, double, nb::ndim<1>>;

Array to_array(const std::vector<double>& v) {
    auto* data = new double[v.empty() ? 1 : v.size()];
    if (!v.empty()) {
        std::memcpy(data, v.data(), v.size() * sizeof(double));
    }
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return Array(data, {v.size()}, owner);
}

nb::dict render_py(const nb::object& scenario) {
    std::string text;
    if (nb::isinstance<nb::str>(scenario)) {
        text = nb::cast<std::string>(scenario);
    } else {
        nb::module_ json = nb::module_::import_("json");
        text = nb::cast<std::string>(json.attr("dumps")(scenario));
    }
    pme::ref::Scenario sc = pme::ref::parse_scenario(text);
    pme::ref::Rendered rendered;
    {
        nb::gil_scoped_release release;
        rendered = pme::ref::render_scenario(sc);
    }
    const pme::ref::Results& r = rendered.results;
    nb::dict out;
    out["name"] = r.name;
    out["fs"] = r.fs;
    out["oversampling"] = r.oversampling;
    out["sample_rate_hz"] = r.sim_rate;
    out["dt"] = r.dt;
    out["steps"] = r.steps;
    nb::dict signals;
    for (const auto& s : r.signals) {
        signals[nb::str(s.name.c_str())] = to_array(s.data);
    }
    out["signals"] = signals;
    nb::dict energy;
    const pme::ref::EnergyTrace& e = r.energy;
    energy["total"] = to_array(e.total);
    energy["string"] = to_array(e.string);
    energy["bridge"] = to_array(e.bridge);
    energy["plate"] = to_array(e.plate);
    energy["hammer"] = to_array(e.hammer);
    energy["contact"] = to_array(e.contact);
    energy["loss"] = to_array(e.loss);
    energy["balance"] = to_array(e.balance);
    energy["max_relative_step"] = pme::ref::max_relative_energy_step(e);
    energy["max_relative_imbalance"] = pme::ref::max_relative_energy_imbalance(e);
    out["energy"] = energy;
    nb::module_ json = nb::module_::import_("json");
    out["features"] = json.attr("loads")(pme::analysis::serialize_golden(rendered.features));
    return out;
}

}  // namespace

NB_MODULE(pme_reference, m) {
    m.doc() = "Independent float64 reference solver of the plucked physical modelling engine.";
    nb::exception<pme::ref::RefError>(m, "RefError", PyExc_ValueError);
    m.def("render", &render_py, nb::arg("scenario"),
          "render(scenario: dict | str) -> dict\n\n"
          "Simulates a scenario (the JSON scenario format as a dict or JSON text) and returns "
          "'signals' (NumPy arrays by observation name), 'energy' (arrays and monitors), "
          "'features' (the golden-feature dict) and the sampling metadata. Raises RefError "
          "(a ValueError) for scenarios the reference does not model.");
    m.def("version", []() { return std::string(pme::ref::kProducerVersion); });
}
