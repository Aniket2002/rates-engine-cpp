#pragma once
#include <chrono>
#include <compare>
#include <set>
#include <string>

namespace rates {
// Gregorian dates in [1900-01-01, 2199-12-31]; no intraday time component.
class Date {
  public:
    Date(int year, unsigned month, unsigned day);
    static Date parse(const std::string &iso);
    std::string str() const;
    int year() const;
    unsigned month() const;
    unsigned day() const;
    Date add_days(int days) const;
    Date add_months(int months, bool preserve_end_of_month = true) const;
    bool is_end_of_month() const;
    bool is_weekend() const;
    int operator-(Date other) const;
    auto operator<=>(const Date &) const = default;

  private:
    std::chrono::sys_days days_;
};

struct Tenor {
    int months;
    static Tenor years(int years);
};
enum class BusinessDay : unsigned char { Unadjusted, Following, ModifiedFollowing };
class Calendar {
  public:
    explicit Calendar(std::set<Date> holidays = {});
    bool is_business_day(Date date) const;
    Date adjust(Date date, BusinessDay convention) const;
    // Count business days excluding the starting date; zero applies the adjustment.
    Date advance(Date date, int business_days, BusinessDay adjustment = BusinessDay::Following) const;
    const std::set<Date> &holidays() const { return holidays_; }

  private:
    std::set<Date> holidays_;
};
} // namespace rates
