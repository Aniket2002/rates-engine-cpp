#include "rates/sofr_curve.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace rates;
void bind_sofr(py::module_ &module) {
    py::class_<Timestamp>(module, "Timestamp")
        .def(py::init([](const std::string &s) { return Timestamp::parse(s); }))
        .def("__str__", &Timestamp::str);
    py::class_<FixingRecord>(module, "FixingRecord")
        .def(py::init(
                 [](Date date, Timestamp available, double rate, const std::string &source, int revision) {
                     return FixingRecord{date, available, SimpleRate{rate}, source, revision};
                 }),
             py::arg("date"), py::arg("available_at"), py::arg("rate"), py::arg("source"),
             py::arg("revision") = 0);
    py::class_<FixingStore>(module, "FixingStore")
        .def(py::init<std::vector<FixingRecord>>(), py::arg("records") = std::vector<FixingRecord>{});
    py::class_<SofrConventions>(module, "SofrConventions")
        .def(py::init<>())
        .def_readwrite("payment_months", &SofrConventions::payment_months)
        .def_readwrite("payment_lag_days", &SofrConventions::payment_lag_days)
        .def_readwrite("settlement_days", &SofrConventions::settlement_days)
        .def_readwrite("lookback_days", &SofrConventions::lookback_days)
        .def_readwrite("lockout_days", &SofrConventions::lockout_days)
        .def_readwrite("observation_shift", &SofrConventions::observation_shift)
        .def_readwrite("end_of_month", &SofrConventions::end_of_month)
        .def_readwrite("fixed_day_count", &SofrConventions::fixed_day_count)
        .def_readwrite("adjustment", &SofrConventions::adjustment)
        .def_readwrite("calendar", &SofrConventions::calendar)
        .def_readwrite("payment_calendar", &SofrConventions::payment_calendar);
    py::class_<SofrSwap>(module, "SofrSwap")
        .def(py::init([](Date effective, Date maturity, double notional, double coupon, Side side,
                         const SofrConventions &c) {
                 return SofrSwap{effective, maturity, notional, SimpleRate{coupon}, side, c};
             }),
             py::arg("effective"), py::arg("maturity"), py::arg("notional"), py::arg("coupon"),
             py::arg("side") = Side::Payer, py::arg("conventions") = SofrConventions{})
        .def_readonly("effective", &SofrSwap::effective)
        .def_readonly("maturity", &SofrSwap::maturity);
    module.def(
        "spot_sofr_swap",
        [](Date trade_date, int months, double notional, double coupon, Side side, const SofrConventions &c) {
            return spot_sofr_swap(trade_date, months, notional, SimpleRate{coupon}, side, c);
        },
        py::arg("trade_date"), py::arg("tenor_months"), py::arg("notional"), py::arg("coupon"),
        py::arg("side") = Side::Payer, py::arg("conventions") = SofrConventions{});
    py::class_<OvernightObservation>(module, "OvernightObservation")
        .def_readonly("accrual_start", &OvernightObservation::accrual_start)
        .def_readonly("accrual_end", &OvernightObservation::accrual_end)
        .def_readonly("fixing_date", &OvernightObservation::fixing_date)
        .def_readonly("observation_end", &OvernightObservation::observation_end)
        .def_readonly("weight", &OvernightObservation::weight)
        .def_readonly("rate", &OvernightObservation::rate)
        .def_readonly("realized", &OvernightObservation::realized);
    py::class_<SofrCoupon>(module, "SofrCoupon")
        .def_readonly("period", &SofrCoupon::period)
        .def_readonly("fixed_amount", &SofrCoupon::fixed_amount)
        .def_readonly("floating_amount", &SofrCoupon::floating_amount)
        .def_readonly("discount", &SofrCoupon::discount)
        .def_readonly("fixed_accrued", &SofrCoupon::fixed_accrued)
        .def_readonly("floating_accrued", &SofrCoupon::floating_accrued)
        .def_readonly("observations", &SofrCoupon::observations);
    py::class_<SofrResult>(module, "SofrResult")
        .def_readonly("fixed_pv", &SofrResult::fixed_pv)
        .def_readonly("floating_pv", &SofrResult::floating_pv)
        .def_readonly("pv", &SofrResult::pv)
        .def_readonly("accrued", &SofrResult::accrued)
        .def_readonly("clean_pv", &SofrResult::clean_pv)
        .def_readonly("par_rate", &SofrResult::par_rate)
        .def_readonly("coupons", &SofrResult::coupons)
        .def_readonly("settled_coupons", &SofrResult::settled_coupons);
    module.def("price_sofr", &price_sofr, py::arg("discount"), py::arg("projection"), py::arg("swap"),
               py::arg("fixings"), py::arg("as_of"), py::arg("include_valuation_date_payments") = false);
    module.def("sofr_payment_schedule", &sofr_payment_schedule, py::arg("swap"));
    py::class_<SofrQuote>(module, "SofrQuote")
        .def(py::init([](const std::string &label, int months, double rate) {
                 return SofrQuote{label, months, SimpleRate{rate}};
             }),
             py::arg("label"), py::arg("tenor_months"), py::arg("rate"));
    py::class_<SofrCalibrationRow>(module, "SofrCalibrationRow")
        .def_readonly("effective", &SofrCalibrationRow::effective)
        .def_readonly("pillar", &SofrCalibrationRow::pillar)
        .def_readonly("model_quote", &SofrCalibrationRow::model_quote)
        .def_readonly("residual", &SofrCalibrationRow::residual);
    py::class_<SofrBootstrapResult>(module, "SofrBootstrapResult")
        .def_readonly("curve", &SofrBootstrapResult::curve)
        .def_readonly("diagnostics", &SofrBootstrapResult::diagnostics);
    py::class_<SofrCurveBootstrapper>(module, "SofrCurveBootstrapper")
        .def(py::init([](const SofrConventions &c) { return SofrCurveBootstrapper{c}; }),
             py::arg("conventions") = SofrConventions{})
        .def("build", &SofrCurveBootstrapper::build, py::arg("as_of"), py::arg("quotes"),
             py::arg("fixings") = FixingStore{})
        .def("build_forward", &SofrCurveBootstrapper::build_forward, py::arg("discount"), py::arg("as_of"),
             py::arg("quotes"), py::arg("fixings") = FixingStore{});
    py::class_<SofrQuoteRisk>(module, "SofrQuoteRisk")
        .def_readonly("base", &SofrQuoteRisk::base)
        .def_readonly("parallel_dv01", &SofrQuoteRisk::parallel_dv01)
        .def_readonly("quote_buckets", &SofrQuoteRisk::quote_buckets);
    module.def(
        "sofr_quote_risk",
        [](const SofrCurveBootstrapper &b, const std::vector<SofrQuote> &q, const SofrSwap &s,
           const FixingStore &f, Timestamp t,
           double bump) { return sofr_quote_risk(b, q, s, f, t, BasisPoints{bump}); },
        py::arg("builder"), py::arg("quotes"), py::arg("swap"), py::arg("fixings"), py::arg("as_of"),
        py::arg("bump_bps") = 1.0);
    py::class_<SofrMultiCurveQuoteRisk>(module, "SofrMultiCurveQuoteRisk")
        .def_readonly("base", &SofrMultiCurveQuoteRisk::base)
        .def_readonly("discount_parallel_dv01", &SofrMultiCurveQuoteRisk::discount_parallel_dv01)
        .def_readonly("projection_parallel_dv01", &SofrMultiCurveQuoteRisk::projection_parallel_dv01)
        .def_readonly("discount_quotes", &SofrMultiCurveQuoteRisk::discount_quotes)
        .def_readonly("projection_quotes", &SofrMultiCurveQuoteRisk::projection_quotes)
        .def_readonly("bump_bps", &SofrMultiCurveQuoteRisk::bump_bps);
    module.def(
        "sofr_multi_curve_quote_risk",
        [](const SofrCurveBootstrapper &d, const SofrCurveBootstrapper &p, const std::vector<SofrQuote> &dq,
           const std::vector<SofrQuote> &pq, const SofrSwap &s, const FixingStore &f, Timestamp t,
           double bump, bool include_today) {
            return sofr_multi_curve_quote_risk(d, p, dq, pq, s, f, t, BasisPoints{bump}, include_today);
        },
        py::arg("discount_builder"), py::arg("projection_builder"), py::arg("discount_quotes"),
        py::arg("projection_quotes"), py::arg("swap"), py::arg("fixings"), py::arg("as_of"),
        py::arg("bump_bps") = 1.0, py::arg("include_valuation_date_payments") = false);
    py::class_<SofrCurveRisk>(module, "SofrCurveRisk")
        .def_readonly("base", &SofrCurveRisk::base)
        .def_readonly("discount_zero", &SofrCurveRisk::discount_zero)
        .def_readonly("projection_zero", &SofrCurveRisk::projection_zero);
    module.def(
        "sofr_curve_risk",
        [](const DiscountCurve &d, const DiscountCurve &p, const SofrSwap &s, const FixingStore &f,
           Timestamp t, double bump) { return sofr_curve_risk(d, p, s, f, t, BasisPoints{bump}); },
        py::arg("discount"), py::arg("projection"), py::arg("swap"), py::arg("fixings"), py::arg("as_of"),
        py::arg("bump_bps") = 1.0);
}
