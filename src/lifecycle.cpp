#include "rates/lifecycle.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace rates {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::invalid_argument(message);
}
template <class Event> bool is_replay(const Event &event, const std::vector<Event> &journal) {
    require(!event.id.empty(), "Event id must be nonempty");
    const auto found =
        std::find_if(journal.begin(), journal.end(), [&](const auto &old) { return old.id == event.id; });
    if (found != journal.end()) {
        require(*found == event, "Conflicting duplicate event id");
        return true;
    }
    require(journal.empty() || event.at >= journal.back().at, "Events must be chronological");
    return false;
}
} // namespace
bool ClearingLedger::apply(const ClearingEvent &event) {
    if (is_replay(event, journal_))
        return false;
    auto next = *this; // Strong exception guarantee: invalid events never change the ledger.
    next.apply_new(event);
    require(std::isfinite(next.cash_), "Clearing cash overflow");
    next.journal_.push_back(event);
    *this = std::move(next);
    return true;
}
void ClearingLedger::apply_new(const ClearingEvent &e) {
    require(!e.trade.empty() && std::isfinite(e.amount), "Trade id and finite cash amount required");
    if (e.kind == ClearingEventKind::Submit) {
        require(e.reference.empty() && !e.due && e.amount == 0, "Unexpected submission fields");
        require(trades_.emplace(e.trade, ClearedTrade{ClearingState::Submitted, {}}).second,
                "Duplicate trade");
        return;
    }
    const auto found = trades_.find(e.trade);
    require(found != trades_.end(), "Unknown clearing trade");
    auto &trade = found->second;
    const auto add_payment = [&]() {
        require(!e.reference.empty() && e.due && *e.due >= e.at.date(),
                "Payment needs a unique reference and nonpast due date");
        require(payments_.emplace(e.reference, ClearingPayment{e.trade, *e.due, e.amount}).second,
                "Duplicate payment reference");
    };
    switch (e.kind) {
    case ClearingEventKind::Accept:
        require(trade.state == ClearingState::Submitted && e.due && *e.due >= e.at.date() &&
                    e.reference.empty() && e.amount == 0,
                "Acceptance requires submitted trade and final payment date");
        trade.state = ClearingState::Accepted;
        trade.final_payment = e.due;
        break;
    case ClearingEventKind::Reject:
        require(trade.state == ClearingState::Submitted && !e.reference.empty() && !e.due && e.amount == 0,
                "Rejection requires submitted trade and reason");
        trade.state = ClearingState::Rejected;
        break;
    case ClearingEventKind::RegisterPayment:
        require(trade.state == ClearingState::Accepted && trade.final_payment && e.due &&
                    *e.due <= *trade.final_payment,
                "Payment must belong to accepted trade and fall within final payment date");
        add_payment();
        break;
    case ClearingEventKind::SettlePayment: {
        require(!e.due && e.amount == 0,
                "Settlement references the recorded obligation; no replacement amount/date");
        require(trade.state == ClearingState::Accepted || trade.state == ClearingState::Terminated,
                "Trade cannot settle");
        const auto p = payments_.find(e.reference);
        require(p != payments_.end() && p->second.trade == e.trade, "Unknown payment for trade");
        auto &payment = p->second;
        require(!payment.settled && !payment.cancelled && e.at.date() >= payment.due,
                "Payment already resolved or not yet due");
        payment.settled = true;
        cash_ += payment.amount;
        break;
    }
    case ClearingEventKind::Terminate:
        require(trade.state == ClearingState::Accepted, "Only accepted trades can terminate");
        add_payment(); // Caller supplies the full closeout amount, replacing unpaid coupons.
        for (auto &[id, payment] : payments_)
            if (payment.trade == e.trade && !payment.settled && id != e.reference)
                payment.cancelled = true;
        trade.state = ClearingState::Terminated;
        trade.final_payment = e.due;
        break;
    case ClearingEventKind::Close:
        require((trade.state == ClearingState::Accepted || trade.state == ClearingState::Terminated) &&
                    trade.final_payment && e.at.date() >= *trade.final_payment && !e.due &&
                    e.reference.empty() && e.amount == 0,
                "Trade cannot close before its final payment date");
        for (const auto &[id, payment] : payments_) {
            (void)id;
            require(payment.trade != e.trade || payment.settled || payment.cancelled,
                    "Unsettled clearing payment");
        }
        trade.state = ClearingState::Closed;
        break;
    default:
        throw std::invalid_argument("Unknown clearing event kind");
    }
}
double ClearingLedger::net_exposure(const std::map<std::string, double> &pvs) const {
    double total = 0;
    std::size_t active = 0;
    for (const auto &[id, trade] : trades_) {
        if (trade.state != ClearingState::Accepted && trade.state != ClearingState::Terminated)
            continue;
        ++active;
        const auto found = pvs.find(id);
        require(found != pvs.end() && std::isfinite(found->second),
                "Every active trade needs a finite dirty PV");
        total += found->second;
    }
    require(pvs.size() == active && std::isfinite(total),
            "Unknown/inactive exposure trade or net exposure overflow");
    return total;
}
void CollateralAgreement::validate() const {
    for (const double value : {receive_threshold, post_threshold, minimum_transfer, rounding})
        require(std::isfinite(value) && value >= 0,
                "Collateral thresholds, MTA and rounding must be finite/nonnegative");
    require(settlement_days >= 0 && settlement_days <= 30, "Invalid collateral settlement lag");
    require(interest_day_count == DayCount::Actual360 || interest_day_count == DayCount::Actual365Fixed,
            "Cash collateral interest requires ACT/360 or ACT/365F");
}
CollateralAccount::CollateralAccount(Date inception, CollateralAgreement agreement)
    : agreement_(std::move(agreement)), inception_(inception), interest_through_(inception) {
    agreement_.validate();
}
double CollateralAccount::pending() const {
    double total = 0;
    for (const auto &[id, transfer] : transfers_) {
        (void)id;
        if (!transfer.settled && !transfer.cancelled)
            total += transfer.amount;
    }
    return total;
}
bool CollateralAccount::apply(const CollateralEvent &event) {
    if (is_replay(event, journal_))
        return false;
    auto next = *this;
    next.apply_new(event);
    require(std::isfinite(next.balance_) && std::isfinite(next.interest_) &&
                std::isfinite(next.interest_cash_) && std::isfinite(next.pending()) &&
                std::isfinite(next.unsecured_exposure()),
            "Collateral arithmetic overflow");
    next.journal_.push_back(event);
    *this = std::move(next);
    return true;
}
void CollateralAccount::apply_new(const CollateralEvent &e) {
    require(!closed_ && e.at.date() >= inception_ && std::isfinite(e.value),
            "Closed account, pre-inception event or nonfinite value");
    const auto make_call = [&](double amount) {
        require(std::isfinite(amount), "Margin call overflow");
        if (amount != 0)
            transfers_.emplace(
                e.id,
                MarginTransfer{agreement_.calendar.advance(e.at.date(), agreement_.settlement_days), amount});
    };
    switch (e.kind) {
    case CollateralEventKind::Mark: {
        require(e.reference.empty(), "Mark does not take a reference");
        exposure_ = e.value;
        double target = exposure_ >= 0 ? std::max(0., exposure_ - agreement_.receive_threshold)
                                       : std::min(0., exposure_ + agreement_.post_threshold);
        // Round the target to keep repeated unchanged marks stable, including with zero MTA.
        if (agreement_.rounding > 0)
            target = std::copysign(std::ceil(std::abs(target) / agreement_.rounding) * agreement_.rounding,
                                   target);
        const double call = target - balance_ - pending();
        if (call != 0 && std::abs(call) >= agreement_.minimum_transfer) {
            make_call(call);
        }
        break;
    }
    case CollateralEventKind::SettleTransfer:
    case CollateralEventKind::CancelTransfer: {
        require(e.value == 0, "Transfer resolution cannot replace the call amount");
        const auto found = transfers_.find(e.reference);
        require(found != transfers_.end() && !found->second.settled && !found->second.cancelled,
                "Unknown or resolved margin call");
        auto &call = found->second;
        if (e.kind == CollateralEventKind::SettleTransfer) {
            require(e.at.date() >= call.due && agreement_.calendar.is_business_day(e.at.date()),
                    "Transfer not due or nonbusiness settlement");
            require(interest_through_ == e.at.date(),
                    "Accrue interest to transfer date before changing principal");
            balance_ += call.amount;
            call.settled = true;
        } else {
            call.cancelled = true;
        }
        break;
    }
    case CollateralEventKind::AccrueInterest: {
        require(e.reference.empty() && e.at.date() > interest_through_,
                "Interest accrual must advance the date");
        const double fraction =
            year_fraction(interest_through_, e.at.date(), agreement_.interest_day_count).value;
        interest_ -= balance_ * e.value * fraction; // Simple interest on actual settled cash only.
        interest_through_ = e.at.date();
        break;
    }
    case CollateralEventKind::PayInterest:
        require(e.reference.empty() && e.value == 0 && interest_through_ == e.at.date() &&
                    agreement_.calendar.is_business_day(e.at.date()),
                "Accrue through a business payment date first");
        interest_cash_ += interest_;
        interest_ = 0;
        break;
    case CollateralEventKind::ReturnCollateral:
        require(e.reference.empty() && e.value == 0 && exposure_ == 0,
                "Return requires zero marked exposure");
        for (const auto &[id, call] : transfers_) {
            (void)id;
            require(call.settled || call.cancelled, "Resolve outstanding margin calls before final return");
        }
        make_call(-balance_); // Final return ignores MTA and rounding.
        break;
    case CollateralEventKind::Close:
        require(e.reference.empty() && e.value == 0 && exposure_ == 0 && balance_ == 0 && interest_ == 0 &&
                    interest_through_ == e.at.date(),
                "Cannot close with exposure, principal or interest outstanding");
        for (const auto &[id, call] : transfers_) {
            (void)id;
            require(call.settled || call.cancelled, "Cannot close with pending transfers");
        }
        closed_ = true;
        break;
    default:
        throw std::invalid_argument("Unknown collateral event kind");
    }
}
} // namespace rates
