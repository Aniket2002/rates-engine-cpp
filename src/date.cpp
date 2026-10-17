#include "rates/date.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rates {
using namespace std::chrono;
Date::Date(int y, unsigned m, unsigned d) {
    const year_month_day date{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
    if (y < 1900 || y > 2199 || !date.ok())
        throw std::invalid_argument("Invalid date or year outside 1900..2199");
    days_ = sys_days{date};
}
Date Date::parse(const std::string &s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        throw std::invalid_argument("Expected ISO date YYYY-MM-DD: " + s);
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            throw std::invalid_argument("Invalid ISO date: " + s);
    return Date{std::stoi(s.substr(0, 4)), static_cast<unsigned>(std::stoi(s.substr(5, 2))),
                static_cast<unsigned>(std::stoi(s.substr(8, 2)))};
}
int Date::year() const {
    return int(year_month_day{days_}.year());
}
unsigned Date::month() const {
    return unsigned(year_month_day{days_}.month());
}
unsigned Date::day() const {
    return unsigned(year_month_day{days_}.day());
}
std::string Date::str() const {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << year() << '-' << std::setw(2) << month() << '-'
        << std::setw(2) << day();
    return out.str();
}
Date Date::add_days(int n) const {
    if (n < -110000 || n > 110000)
        throw std::out_of_range("Day increment outside supported range");
    const year_month_day d{days_ + days{n}};
    return Date{int(d.year()), unsigned(d.month()), unsigned(d.day())};
}
bool Date::is_end_of_month() const {
    const year_month_day d{days_};
    return d.day() == year_month_day_last{d.year(), month_day_last{d.month()}}.day();
}
Date Date::add_months(int n, bool eom) const {
    if (n < -3600 || n > 3600)
        throw std::out_of_range("Month increment outside supported range");
    const year_month_day d{days_};
    const auto ym = d.year() / d.month() + months{n};
    const auto last_day = unsigned(year_month_day_last{ym.year(), month_day_last{ym.month()}}.day());
    return Date{int(ym.year()), unsigned(ym.month()),
                eom && is_end_of_month() ? last_day : std::min(day(), last_day)};
}
bool Date::is_weekend() const {
    const auto wd = weekday{days_};
    return wd == Saturday || wd == Sunday;
}
int Date::operator-(Date other) const {
    return static_cast<int>((days_ - other.days_).count());
}
Tenor Tenor::years(int y) {
    if (y < -300 || y > 300)
        throw std::invalid_argument("Year tenor outside supported range");
    return {12 * y};
}
Calendar::Calendar(std::set<Date> holidays) : holidays_(std::move(holidays)) {}
bool Calendar::is_business_day(Date date) const {
    return !date.is_weekend() && !holidays_.contains(date);
}
Date Calendar::adjust(Date date, BusinessDay convention) const {
    if (convention == BusinessDay::Unadjusted)
        return date;
    if (convention != BusinessDay::Following && convention != BusinessDay::ModifiedFollowing)
        throw std::invalid_argument("Invalid business-day convention");
    Date adjusted = date;
    while (!is_business_day(adjusted))
        adjusted = adjusted.add_days(1);
    if (convention == BusinessDay::ModifiedFollowing && adjusted.month() != date.month()) {
        adjusted = date;
        while (!is_business_day(adjusted))
            adjusted = adjusted.add_days(-1);
    }
    return adjusted;
}
} // namespace rates
