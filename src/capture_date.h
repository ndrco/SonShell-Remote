#pragma once
#include <iomanip>
#include <sstream>
#include <string>

// Camera local calendar date, independent of the computer's timezone/locale.
inline std::string capture_date_folder(unsigned year, unsigned month, unsigned day) {
  if (year < 1900 || year > 9999 || month < 1 || month > 12 || day < 1) return {};
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (day > days[month - 1] + (month == 2 && leap ? 1u : 0u)) return {};
  std::ostringstream out;
  out << year << '-' << std::setw(2) << std::setfill('0') << month << '-' << std::setw(2) << day;
  return out.str();
}
