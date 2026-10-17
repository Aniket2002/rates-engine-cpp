#include "rates/risk_engine.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace rates;
PYBIND11_MODULE(pyrates, module) {
    module.doc() = "C++ single-curve rates engine. Rates are decimals; risk is currency per bp.";
    py::class_<Date>(module, "Date")
        .def(py::init([](const std::string &iso) { return Date::parse(iso); }))
        .def("__str__", &Date::str)
        .def("add_months", &Date::add_months, py::arg("months"), py::arg("preserve_end_of_month") = true)
        .def("add_days", &Date::add_days);
    py::enum_<DayCount>(module, "DayCount")
        .value("ACT360", DayCount::Actual360)
        .value("ACT365F", DayCount::Actual365Fixed)
        .value("ThirtyE360", DayCount::ThirtyE360);
    py::enum_<BusinessDay>(module, "BusinessDay")
        .value("Unadjusted", BusinessDay::Unadjusted)
        .value("Following", BusinessDay::Following)
        .value("ModifiedFollowing", BusinessDay::ModifiedFollowing);
    py::enum_<Side>(module, "Side").value("Payer", Side::Payer).value("Receiver", Side::Receiver);
    py::enum_<Instrument>(module, "Instrument")
        .value("Deposit", Instrument::Deposit)
        .value("Swap", Instrument::Swap);
    py::class_<Calendar>(module, "Calendar")
        .def(py::init<std::set<Date>>(), py::arg("holidays") = std::set<Date>{})
        .def("adjust", &Calendar::adjust);
    py::class_<Conventions>(module, "Conventions")
        .def(py::init<>())
        .def_readwrite("fixed_months", &Conventions::fixed_months)
        .def_readwrite("floating_months", &Conventions::floating_months)
        .def_readwrite("fixed_day_count", &Conventions::fixed_day_count)
        .def_readwrite("floating_day_count", &Conventions::floating_day_count)
        .def_readwrite("deposit_day_count", &Conventions::deposit_day_count)
        .def_readwrite("business_day", &Conventions::business_day)
        .def_readwrite("end_of_month", &Conventions::end_of_month)
        .def_readwrite("calendar", &Conventions::calendar);
    py::class_<MarketQuote>(module, "MarketQuote")
        .def(py::init([](Instrument instrument, const std::string &label, Date maturity, double rate) {
                 return MarketQuote{instrument, label, maturity, SimpleRate{rate}};
             }),
             py::arg("instrument"), py::arg("label"), py::arg("maturity"), py::arg("decimal_rate"));
    py::class_<Swap>(module, "Swap")
        .def(py::init([](Date effective, Date maturity, double notional, double coupon, Side side,
                         const Conventions &c) {
                 return Swap{effective, maturity, notional, SimpleRate{coupon}, side, c};
             }),
             py::arg("effective"), py::arg("maturity"), py::arg("notional"), py::arg("decimal_coupon"),
             py::arg("side") = Side::Payer, py::arg("conventions") = Conventions{});
    py::class_<DiscountCurve>(module, "DiscountCurve")
        .def(py::init([](Date valuation, const std::vector<std::pair<Date, double>> &input) {
                 std::vector<CurveNode> nodes;
                 for (const auto &[day, df] : input)
                     nodes.push_back({day, DiscountFactor{df}});
                 return DiscountCurve{valuation, nodes};
             }),
             py::arg("valuation"), py::arg("pillars"))
        .def("discount", [](const DiscountCurve &curve, Date day) { return curve.discount(day).value; })
        .def("discount_time",
             [](const DiscountCurve &curve, double time) { return curve.discount(YearFraction{time}).value; })
        .def("zero", [](const DiscountCurve &curve, Date day) { return curve.zero(day).value; })
        .def("forward", [](const DiscountCurve &curve, Date a, Date b,
                           DayCount dc) { return curve.forward(a, b, dc).value; })
        .def_property_readonly("nodes", [](const DiscountCurve &curve) {
            std::vector<std::pair<Date, double>> nodes;
            for (const auto &n : curve.nodes())
                nodes.emplace_back(n.date, n.discount.value);
            return nodes;
        });
    py::class_<CalibrationRow>(module, "CalibrationRow")
        .def_readonly("pillar", &CalibrationRow::pillar)
        .def_readonly("model_quote", &CalibrationRow::model_quote)
        .def_readonly("residual", &CalibrationRow::residual)
        .def_readonly("iterations", &CalibrationRow::iterations)
        .def_readonly("converged", &CalibrationRow::converged);
    py::class_<BootstrapResult>(module, "BootstrapResult")
        .def_readonly("curve", &BootstrapResult::curve)
        .def_readonly("diagnostics", &BootstrapResult::diagnostics);
    py::class_<CurveBootstrapper>(module, "CurveBootstrapper")
        .def(py::init([](const Conventions &c) { return CurveBootstrapper{c}; }),
             py::arg("conventions") = Conventions{})
        .def("build", &CurveBootstrapper::build);
    py::class_<AccrualPeriod>(module, "AccrualPeriod")
        .def_readonly("start", &AccrualPeriod::start)
        .def_readonly("end", &AccrualPeriod::end)
        .def_readonly("payment", &AccrualPeriod::payment)
        .def_property_readonly("year_fraction", [](const AccrualPeriod &p) { return p.accrual.value; });
    py::class_<CashFlow>(module, "CashFlow")
        .def_readonly("period", &CashFlow::period)
        .def_readonly("rate", &CashFlow::decimal_rate)
        .def_readonly("amount", &CashFlow::amount)
        .def_readonly("discount", &CashFlow::discount)
        .def_readonly("pv", &CashFlow::present_value);
    py::class_<SwapResult>(module, "SwapResult")
        .def_readonly("pv", &SwapResult::pv)
        .def_readonly("fixed_pv", &SwapResult::fixed_pv)
        .def_readonly("floating_pv", &SwapResult::floating_pv)
        .def_readonly("par_rate", &SwapResult::par_rate)
        .def_readonly("annuity", &SwapResult::annuity)
        .def_readonly("fixed_flows", &SwapResult::fixed_flows)
        .def_readonly("floating_flows", &SwapResult::floating_flows);
    py::class_<PreparedSwap>(module, "PreparedSwap").def(py::init<Swap>());
    module.def("price", py::overload_cast<const DiscountCurve &, const Swap &>(&price));
    module.def("price_prepared", py::overload_cast<const DiscountCurve &, const PreparedSwap &>(&price));
    py::class_<RiskBucket>(module, "RiskBucket")
        .def_readonly("label", &RiskBucket::label)
        .def_readonly("maturity", &RiskBucket::maturity)
        .def_readonly("dv01", &RiskBucket::dv01);
    py::class_<RiskReport>(module, "RiskReport")
        .def_readonly("base", &RiskReport::base)
        .def_readonly("parallel_quote_dv01", &RiskReport::parallel_quote_dv01)
        .def_readonly("quote_buckets", &RiskReport::quote_buckets)
        .def_readonly("zero_nodes", &RiskReport::zero_nodes);
    py::class_<RiskScenarios>(module, "RiskScenarios")
        .def_property_readonly("calibration_count", &RiskScenarios::calibration_count)
        .def("report", py::overload_cast<const Swap &, std::string>(&RiskScenarios::report, py::const_))
        .def("portfolio", &RiskScenarios::portfolio);
    py::class_<RiskEngine>(module, "RiskEngine")
        .def(py::init<CurveBootstrapper>(), py::arg("bootstrapper") = CurveBootstrapper{})
        .def(
            "prepare_scenarios",
            [](const RiskEngine &r, Date v, const std::vector<MarketQuote> &q, double bump) {
                return r.prepare_scenarios(v, q, BasisPoints{bump});
            },
            py::arg("valuation"), py::arg("quotes"), py::arg("bump_bps") = 1.0)
        .def(
            "portfolio_parallel_dv01",
            [](const RiskEngine &r, Date v, const std::vector<MarketQuote> &q,
               const std::vector<Swap> &trades,
               double bump) { return r.portfolio_parallel_dv01(v, q, trades, BasisPoints{bump}); },
            py::arg("valuation"), py::arg("quotes"), py::arg("trades"), py::arg("bump_bps") = 1.0)
        .def(
            "parallel_quote_dv01",
            [](const RiskEngine &r, Date v, const std::vector<MarketQuote> &q, const Swap &t, double bump) {
                return r.parallel_quote_dv01(v, q, t, BasisPoints{bump});
            },
            py::arg("valuation"), py::arg("quotes"), py::arg("trade"), py::arg("bump_bps") = 1.0)
        .def(
            "quote_risk",
            [](const RiskEngine &r, Date v, const std::vector<MarketQuote> &q, const Swap &t, double bump) {
                return r.quote_risk(v, q, t, BasisPoints{bump});
            },
            py::arg("valuation"), py::arg("quotes"), py::arg("trade"), py::arg("bump_bps") = 1.0)
        .def(
            "zero_risk",
            [](const RiskEngine &r, const DiscountCurve &c, const Swap &t, double bump) {
                return r.zero_risk(c, t, BasisPoints{bump});
            },
            py::arg("curve"), py::arg("trade"), py::arg("bump_bps") = 1.0);
}
