#include "rates/basis.hpp"
#include "rates/lifecycle.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace rates;
void bind_lifecycle(py::module_ &m) {
    py::class_<BasisSwap>(m, "BasisSwap")
        .def(py::init(
                 [](Date a, Date b, double n, double s, Side side, const SofrConventions &bc,
                    const SofrConventions &oc) { return BasisSwap{a, b, n, SimpleRate{s}, side, bc, oc}; }),
             py::arg("effective"), py::arg("maturity"), py::arg("notional"), py::arg("spread"),
             py::arg("side") = Side::Payer, py::arg("base_conventions") = SofrConventions{},
             py::arg("other_conventions") = SofrConventions{})
        .def_readonly("effective", &BasisSwap::effective)
        .def_readonly("maturity", &BasisSwap::maturity);
    py::class_<BasisResult>(m, "BasisResult")
        .def_readonly("base_leg", &BasisResult::base_leg)
        .def_readonly("other_leg", &BasisResult::other_leg)
        .def_readonly("spread_pv", &BasisResult::spread_pv)
        .def_readonly("pv", &BasisResult::pv)
        .def_readonly("accrued", &BasisResult::accrued)
        .def_readonly("clean_pv", &BasisResult::clean_pv)
        .def_readonly("par_spread", &BasisResult::par_spread);
    m.def("price_basis", &price_basis, py::arg("discount"), py::arg("base_projection"),
          py::arg("other_projection"), py::arg("swap"), py::arg("base_fixings"), py::arg("other_fixings"),
          py::arg("as_of"), py::arg("include_valuation_date_payments") = false);
    py::class_<BasisQuote>(m, "BasisQuote")
        .def(py::init([](const std::string &label, int months, double spread) {
                 return BasisQuote{label, months, SimpleRate{spread}};
             }),
             py::arg("label"), py::arg("tenor_months"), py::arg("spread"));
    py::class_<BasisCalibrationRow>(m, "BasisCalibrationRow")
        .def_readonly("effective", &BasisCalibrationRow::effective)
        .def_readonly("pillar", &BasisCalibrationRow::pillar)
        .def_readonly("model_quote", &BasisCalibrationRow::model_quote)
        .def_readonly("residual", &BasisCalibrationRow::residual);
    py::class_<BasisBootstrapResult>(m, "BasisBootstrapResult")
        .def_readonly("curve", &BasisBootstrapResult::curve)
        .def_readonly("diagnostics", &BasisBootstrapResult::diagnostics);
    py::class_<BasisCurveBootstrapper>(m, "BasisCurveBootstrapper")
        .def(py::init([](const SofrConventions &b, const SofrConventions &o) {
                 return BasisCurveBootstrapper{b, o};
             }),
             py::arg("base_conventions") = SofrConventions{},
             py::arg("other_conventions") = SofrConventions{})
        .def("build", &BasisCurveBootstrapper::build, py::arg("discount"), py::arg("base_projection"),
             py::arg("as_of"), py::arg("quotes"), py::arg("base_fixings") = FixingStore{},
             py::arg("other_fixings") = FixingStore{})
        .def(
            "instrument",
            [](const BasisCurveBootstrapper &b, Date d, int months, double n, double s, Side side) {
                return b.instrument(d, months, n, SimpleRate{s}, side);
            },
            py::arg("trade_date"), py::arg("tenor_months"), py::arg("notional"), py::arg("spread"),
            py::arg("side") = Side::Payer);
    py::class_<BasisQuoteRisk>(m, "BasisQuoteRisk")
        .def_readonly("base", &BasisQuoteRisk::base)
        .def_readonly("discount_parallel_dv01", &BasisQuoteRisk::discount_parallel_dv01)
        .def_readonly("base_parallel_dv01", &BasisQuoteRisk::base_parallel_dv01)
        .def_readonly("basis_parallel_dv01", &BasisQuoteRisk::basis_parallel_dv01)
        .def_readonly("discount_quotes", &BasisQuoteRisk::discount_quotes)
        .def_readonly("base_quotes", &BasisQuoteRisk::base_quotes)
        .def_readonly("basis_quotes", &BasisQuoteRisk::basis_quotes)
        .def_readonly("bump_bps", &BasisQuoteRisk::bump_bps);
    m.def(
        "basis_quote_risk",
        [](const SofrCurveBootstrapper &d, const SofrCurveBootstrapper &b, const BasisCurveBootstrapper &o,
           const std::vector<SofrQuote> &dq, const std::vector<SofrQuote> &bq,
           const std::vector<BasisQuote> &oq, const BasisSwap &s, const FixingStore &df,
           const FixingStore &bf, const FixingStore &of, Timestamp t, double bump, bool include) {
            return basis_quote_risk(d, b, o, dq, bq, oq, s, df, bf, of, t, BasisPoints{bump}, include);
        },
        py::arg("discount_builder"), py::arg("base_builder"), py::arg("basis_builder"),
        py::arg("discount_quotes"), py::arg("base_quotes"), py::arg("basis_quotes"), py::arg("swap"),
        py::arg("discount_fixings"), py::arg("base_fixings"), py::arg("other_fixings"), py::arg("as_of"),
        py::arg("bump_bps") = 1., py::arg("include_valuation_date_payments") = false);
    py::enum_<ClearingState>(m, "ClearingState")
        .value("Submitted", ClearingState::Submitted)
        .value("Accepted", ClearingState::Accepted)
        .value("Rejected", ClearingState::Rejected)
        .value("Terminated", ClearingState::Terminated)
        .value("Closed", ClearingState::Closed);
    py::enum_<ClearingEventKind>(m, "ClearingEventKind")
        .value("Submit", ClearingEventKind::Submit)
        .value("Accept", ClearingEventKind::Accept)
        .value("Reject", ClearingEventKind::Reject)
        .value("RegisterPayment", ClearingEventKind::RegisterPayment)
        .value("SettlePayment", ClearingEventKind::SettlePayment)
        .value("Terminate", ClearingEventKind::Terminate)
        .value("Close", ClearingEventKind::Close);
    py::class_<ClearingEvent>(m, "ClearingEvent")
        .def(py::init<std::string, Timestamp, ClearingEventKind, std::string, std::string, double,
                      std::optional<Date>>(),
             py::arg("id"), py::arg("at"), py::arg("kind"), py::arg("trade"), py::arg("reference") = "",
             py::arg("amount") = 0., py::arg("due") = std::nullopt)
        .def_readonly("id", &ClearingEvent::id)
        .def_readonly("at", &ClearingEvent::at)
        .def_readonly("kind", &ClearingEvent::kind)
        .def_readonly("trade", &ClearingEvent::trade)
        .def_readonly("reference", &ClearingEvent::reference)
        .def_readonly("amount", &ClearingEvent::amount)
        .def_readonly("due", &ClearingEvent::due);
    py::class_<ClearedTrade>(m, "ClearedTrade")
        .def_readonly("state", &ClearedTrade::state)
        .def_readonly("final_payment", &ClearedTrade::final_payment);
    py::class_<ClearingPayment>(m, "ClearingPayment")
        .def_readonly("trade", &ClearingPayment::trade)
        .def_readonly("due", &ClearingPayment::due)
        .def_readonly("amount", &ClearingPayment::amount)
        .def_readonly("settled", &ClearingPayment::settled)
        .def_readonly("cancelled", &ClearingPayment::cancelled);
    py::class_<ClearingLedger>(m, "ClearingLedger")
        .def(py::init<>())
        .def("apply", &ClearingLedger::apply)
        .def("net_exposure", &ClearingLedger::net_exposure)
        .def_property_readonly("trades", &ClearingLedger::trades)
        .def_property_readonly("payments", &ClearingLedger::payments)
        .def_property_readonly("journal", &ClearingLedger::journal)
        .def_property_readonly("settled_cash", &ClearingLedger::settled_cash);
    py::class_<CollateralAgreement>(m, "CollateralAgreement")
        .def(py::init<>())
        .def_readwrite("receive_threshold", &CollateralAgreement::receive_threshold)
        .def_readwrite("post_threshold", &CollateralAgreement::post_threshold)
        .def_readwrite("minimum_transfer", &CollateralAgreement::minimum_transfer)
        .def_readwrite("rounding", &CollateralAgreement::rounding)
        .def_readwrite("settlement_days", &CollateralAgreement::settlement_days)
        .def_readwrite("calendar", &CollateralAgreement::calendar)
        .def_readwrite("interest_day_count", &CollateralAgreement::interest_day_count);
    py::enum_<CollateralEventKind>(m, "CollateralEventKind")
        .value("Mark", CollateralEventKind::Mark)
        .value("SettleTransfer", CollateralEventKind::SettleTransfer)
        .value("CancelTransfer", CollateralEventKind::CancelTransfer)
        .value("AccrueInterest", CollateralEventKind::AccrueInterest)
        .value("PayInterest", CollateralEventKind::PayInterest)
        .value("ReturnCollateral", CollateralEventKind::ReturnCollateral)
        .value("Close", CollateralEventKind::Close);
    py::class_<CollateralEvent>(m, "CollateralEvent")
        .def(py::init<std::string, Timestamp, CollateralEventKind, std::string, double>(), py::arg("id"),
             py::arg("at"), py::arg("kind"), py::arg("reference") = "", py::arg("value") = 0.)
        .def_readonly("id", &CollateralEvent::id)
        .def_readonly("at", &CollateralEvent::at)
        .def_readonly("kind", &CollateralEvent::kind)
        .def_readonly("reference", &CollateralEvent::reference)
        .def_readonly("value", &CollateralEvent::value);
    py::class_<MarginTransfer>(m, "MarginTransfer")
        .def_readonly("due", &MarginTransfer::due)
        .def_readonly("amount", &MarginTransfer::amount)
        .def_readonly("settled", &MarginTransfer::settled)
        .def_readonly("cancelled", &MarginTransfer::cancelled);
    py::class_<CollateralAccount>(m, "CollateralAccount")
        .def(py::init<Date, CollateralAgreement>(), py::arg("inception"),
             py::arg("agreement") = CollateralAgreement{})
        .def("apply", &CollateralAccount::apply)
        .def_property_readonly("balance", &CollateralAccount::balance)
        .def_property_readonly("pending", &CollateralAccount::pending)
        .def_property_readonly("accrued_interest", &CollateralAccount::accrued_interest)
        .def_property_readonly("settled_interest", &CollateralAccount::settled_interest)
        .def_property_readonly("exposure", &CollateralAccount::exposure)
        .def_property_readonly("unsecured_exposure", &CollateralAccount::unsecured_exposure)
        .def_property_readonly("interest_through", &CollateralAccount::interest_through)
        .def_property_readonly("closed", &CollateralAccount::closed)
        .def_property_readonly("transfers", &CollateralAccount::transfers)
        .def_property_readonly("journal", &CollateralAccount::journal);
}
