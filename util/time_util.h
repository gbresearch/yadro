//-----------------------------------------------------------------------------
//  Copyright (C) 2024, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//
//  Permission is hereby granted, free of charge, to any person or organization
//  obtaining a copy of the software and accompanying documentation covered by
//  this license (the "Software") to use, reproduce, display, distribute,
//  execute, and transmit the Software, and to prepare derivative works of the
//  Software, and to permit third-parties to whom the Software is furnished to
//  do so, all subject to the following:
//
//  The copyright notices in the Software and this entire statement, including
//  the above license grant, this restriction and the following disclaimer,
//  must be included in all copies of the Software, in whole or in part, and
//  all derivative works of the Software, unless such copies or derivative
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once
#include <chrono>
#include <sstream>
#include <thread>

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    inline auto time_stamp()
    {
        using namespace std::chrono_literals;
        using namespace std::chrono;
        auto now = system_clock::now();
        auto tstamp{ system_clock::to_time_t(now) };
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        return (std::ostringstream{} << "[" << std::put_time(std::localtime(&tstamp), "%F %T")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << "] [pid: " << ::_getpid() << ", tid: " << std::this_thread::get_id() << "]").str();
    }

    //-------------------------------------------------------------------------
    // DateTime conversion
    inline auto datetime_to_chrono(double datetime)
    {
        using namespace std::chrono_literals;
        auto days = unsigned(datetime);
        auto hours = unsigned((datetime - days) * 24);
        auto mins = unsigned(((datetime - days) * 24 - hours) * 60);
        auto secs = std::lround((((datetime - days) * 24 - hours) * 60 - mins) * 60);
        return std::chrono::sys_days{ 1899y / 12 / 30 } + std::chrono::days(days)
            + std::chrono::hours(hours)
            + std::chrono::minutes(mins)
            + std::chrono::seconds(secs);
    }

    //-------------------------------------------------------------------------
    inline std::tm unpack_time(std::chrono::time_point<std::chrono::system_clock> tp = std::chrono::system_clock::now())
    {
        std::time_t tp_c = std::chrono::system_clock::to_time_t(tp);
        return *std::localtime(&tp_c);
    }

    //-------------------------------------------------------------------------
    inline std::chrono::system_clock::time_point to_time_point(int year, int month, int day) {
        // Create a tm structure representing the specified date
        std::tm tm = {};
        tm.tm_year = year - 1900;  // tm_year is years since 1900
        tm.tm_mon = month - 1;     // tm_mon is 0-based (0 = January)
        tm.tm_mday = day;          // tm_mday is 1-based

        // Convert the tm structure to time_t, which represents time in seconds since the epoch
        std::time_t time_t_date = std::mktime(&tm);

        // Convert the time_t to a std::chrono::time_point
        return std::chrono::system_clock::from_time_t(time_t_date);
    }

    //-------------------------------------------------------------------------
    // ISO 8601: https://en.wikipedia.org/wiki/ISO_week_date
    inline constexpr int week_of_year(int y, int m, int d)
    {
        using namespace std::chrono;
        constexpr int common_year[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
        constexpr int leap_year[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 };
        auto normalize = [](year_month_day ymd) {ymd += months{ 0 }; return year_month_day{ sys_days{ymd} }; };
        auto is_leap = normalize(year(y) / 2 / 29).month() == February;
        auto doy = (is_leap ? leap_year[m - 1] : common_year[m - 1]) + d;
        auto ymd = year(y) / m / d;
        auto dow = weekday(ymd).iso_encoding();
        auto woy = (10 + doy - dow) / 7;
        if (woy == 0)
            return week_of_year(y - 1, 12, 31);
        if (woy == 53)
        {
            if (weekday(year(y) / 12 / 31) != Thursday && weekday(year(y - 1) / 12 / 31) != Wednesday)
                return 1;
        }
        return woy;
    }
}
