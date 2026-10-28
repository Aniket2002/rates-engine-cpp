#include "rates/curve_ambiguity.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace rates;
void bind_ambiguity(py::module_ &module) {
    py::class_<QuoteBand>(module, "QuoteBand")
        .def(py::init<MarketQuote, double, double>(), py::arg("quote"), py::arg("bid"), py::arg("ask"));
    py::class_<ForwardBounds>(module, "ForwardBounds")
        .def(py::init<double, double>(), py::arg("lower"), py::arg("upper"));
    py::class_<LinearConstraint>(module, "LinearConstraint")
        .def_readonly("label", &LinearConstraint::label)
        .def_readonly("coefficients", &LinearConstraint::coefficients)
        .def_readonly("upper", &LinearConstraint::upper);
    py::class_<CurveAmbiguityProblem>(module, "CurveAmbiguityProblem")
        .def_readonly("dates", &CurveAmbiguityProblem::dates)
        .def_readonly("constraints", &CurveAmbiguityProblem::constraints)
        .def_readonly("objective", &CurveAmbiguityProblem::objective)
        .def("witness", &CurveAmbiguityProblem::witness, py::arg("discounts"), py::arg("tolerance") = 1e-9);
    module.def("curve_ambiguity_problem", &curve_ambiguity_problem, py::arg("valuation"), py::arg("quotes"),
               py::arg("portfolio"), py::arg("forwards"), py::arg("quote_conventions") = Conventions{},
               py::arg("projection") = std::nullopt);
}
