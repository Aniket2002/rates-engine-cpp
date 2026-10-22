#pragma once
#include "rates/history.hpp"
#include <map>
#include <vector>

namespace rates {
enum class ClearingState : unsigned char { Submitted, Accepted, Rejected, Terminated, Closed };
enum class ClearingEventKind : unsigned char {
    Submit,
    Accept,
    Reject,
    RegisterPayment,
    SettlePayment,
    Terminate,
    Close
};
struct ClearingEvent {
    std::string id;
    Timestamp at;
    ClearingEventKind kind;
    std::string trade;
    std::string reference;
    double amount = 0;
    std::optional<Date> due;
    bool operator==(const ClearingEvent &) const = default;
};
struct ClearedTrade {
    ClearingState state;
    std::optional<Date> final_payment;
};
struct ClearingPayment {
    std::string trade;
    Date due;
    double amount; // Positive = receive, negative = pay, from the account's perspective.
    bool settled = false;
    bool cancelled = false;
};
// Deterministic, in-memory contractual ledger for one currency/netting set.
// Acceptance and final cash-flow amounts are caller-supplied decisions, not CCP messages.
class ClearingLedger {
  public:
    bool apply(const ClearingEvent &event); // Identical event replay is a no-op; conflict fails.
    double net_exposure(const std::map<std::string, double> &dirty_pvs) const;
    const std::map<std::string, ClearedTrade> &trades() const { return trades_; }
    const std::map<std::string, ClearingPayment> &payments() const { return payments_; }
    const std::vector<ClearingEvent> &journal() const { return journal_; }
    double settled_cash() const { return cash_; }

  private:
    void apply_new(const ClearingEvent &event);
    std::map<std::string, ClearedTrade> trades_;
    std::map<std::string, ClearingPayment> payments_;
    std::vector<ClearingEvent> journal_;
    double cash_ = 0;
};
struct CollateralAgreement {
    double receive_threshold = 0;
    double post_threshold = 0;
    double minimum_transfer = 0;
    double rounding = 0; // Round target collateral magnitude away from zero; zero disables rounding.
    int settlement_days = 1;
    Calendar calendar{};
    DayCount interest_day_count = DayCount::Actual360;
    void validate() const;
};
enum class CollateralEventKind : unsigned char {
    Mark,
    SettleTransfer,
    CancelTransfer,
    AccrueInterest,
    PayInterest,
    ReturnCollateral,
    Close
};
struct CollateralEvent {
    std::string id;
    Timestamp at;
    CollateralEventKind kind;
    std::string reference;
    double value = 0; // Mark: signed exposure; AccrueInterest: decimal annual remuneration rate.
    bool operator==(const CollateralEvent &) const = default;
};
struct MarginTransfer {
    Date due;
    double amount;
    bool settled = false;
    bool cancelled = false;
};
// Cash collateral (collateralized-to-market), not settled-to-market variation.
// Received principal > 0. Interest cash owed on that principal is negative.
class CollateralAccount {
  public:
    explicit CollateralAccount(Date inception, CollateralAgreement agreement = {});
    bool apply(const CollateralEvent &event);
    double balance() const { return balance_; }
    double pending() const;
    double accrued_interest() const { return interest_; }
    double settled_interest() const { return interest_cash_; }
    double exposure() const { return exposure_; }
    double unsecured_exposure() const { return exposure_ - balance_; }
    Date interest_through() const { return interest_through_; }
    bool closed() const { return closed_; }
    const std::map<std::string, MarginTransfer> &transfers() const { return transfers_; }
    const std::vector<CollateralEvent> &journal() const { return journal_; }

  private:
    void apply_new(const CollateralEvent &event);
    CollateralAgreement agreement_;
    Date inception_, interest_through_;
    double balance_ = 0, interest_ = 0, interest_cash_ = 0, exposure_ = 0;
    bool closed_ = false;
    std::map<std::string, MarginTransfer> transfers_;
    std::vector<CollateralEvent> journal_;
};
} // namespace rates
