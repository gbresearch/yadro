//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2022, Gene Bushuyev
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

#include "../util/gbtest.h"
#include "../util/gblog.h"
#include "../util/gbtimer.h"
#include "../util/misc.h"
#include "../util/gnuplot.h"

#include <sstream>
#include <string>

namespace
{
    using namespace gb::yadro::util;

    GB_TEST(util, logtest)
    {
        std::ostringstream oss;
        logger log(oss);
        log() << "abc" << 1 << 2 << tab{ 10, '.' } << "pi=" << tab{ 15 } << 3.14;
        gbassert(oss.str() == "abc12.....pi=  3.14");
    }

    GB_TEST(util, misc)
    {
        std::int64_t dur{};
        std::uint64_t cnt{};
        {
            accumulating_timer<std::chrono::microseconds> t{
                [&](auto duration, auto count) { dur = duration.count(); cnt = count; }};

            std::mutex m1, m2;
            gbassert(locked_call([] { return 1; }, m1, m2) == 1);

            auto v1{ 0 }, v2{ 0 };
            {
                auto _1{ t.make_scope_timer() };
                raii r{ [&] { v1 = 1; }, [&] { v2 = 2; } };
                gbassert(v1 == 1);
            }
            gbassert(v2 == 2);
        }
        gbassert(dur < 20);
        gbassert(cnt == 1);

        auto v{ 123 };
        {
            auto _{ retainer(v, 321) };
            gbassert(v == 321);
        }
        gbassert(v == 123);

        // test tuples
        auto t1 = std::tuple{ 1, 123.456, std::string("abc"), "xyz" };
        auto t2 = std::tuple{ "...", std::ignore, nullptr};
        gbassert(tuple_to_string(t1, t2) == "{1,123.456,abc,xyz}{...,,nullptr}");
        
        auto [numbers, strings] = tuple_split<2>(t1);
        gbassert(tuple_to_string(numbers, strings) == "{1,123.456}{abc,xyz}");
        
        auto filtered = tuple_foreach(t1, std::ignore, std::ignore,
            [](auto&& v) { return v + "_str"; },
            [](auto&& v) { return v + std::string("_str"); }
        );
        gbassert(tuple_to_string(filtered) == "{,,abc_str,xyz_str}");
        gbassert(tuple_to_string(tuple_remove_ignored(filtered)) == "{abc_str,xyz_str}");
        gbassert(tuple_to_string(tuple_select<1, 3>(t1)) == "{123.456,xyz}");
        
        auto t3 = tuple_transform(t1, overloaded(
            [](int i) { return std::to_string(i); },
            [](double i) { return i; },
            [](const char* s) { return std::string(s); },
            [](auto&& other) { return other; }
        ));
        gbassert(t3 == std::tuple(std::string("1"), 123.456, std::string("abc"), std::string("xyz")));
        
        gbassert(tuple_transform_reduce(t1, // count bytes
            overloaded(
            [](int) { return 4; },
            [](double) { return 8; },
            [](const char* s) { return std::string(s).size(); },
            [](const std::string& s) { return s.size(); }
        ), [](auto&& ...v) { return (0 + ... + v); }) == 18);

        gbassert(std::format("{}", datetime_to_chrono(14000 + 13. / 24 + 25. / 24 / 60 + 15. / 24 / 60 / 60)) == "1938-04-30 13:25:15");
        gbassert(tokenize<char>("abc,xyz,foo,bar", ',') == std::vector<std::string>{ "abc","xyz","foo","bar" });
    }

    GB_TEST(util, gnuplot)
    {
        auto golden = R"*(set multiplot layout 3,2 columnsfirst
set pointsize 5
set grid
set xrange[0:5]
plot "C:/Users/Gene/AppData/Local/Temp/AAD71D026F8A4862A0C7EC1608A00A63.gnuplot" using 1  title "first implicit pane" with steps
set grid
set xrange[0:7]
plot "C:/Users/Gene/AppData/Local/Temp/AAD71D026F8A4862A0C7EC1608A00A63.gnuplot" using 1  notitle with lines
set grid
set xrange[0:5]
plot "C:/Users/Gene/AppData/Local/Temp/AAD71D026F8A4862A0C7EC1608A00A63.gnuplot" using 1  notitle with lines, "C:/Users/Gene/AppData/Local/Temp/AAD71D026F8A4862A0C7EC1608A00A63.gnuplot" using 1  title "v" with steps, "C:/Users/Gene/AppData/Local/Temp/AF2EFCFD6F0D4431B529880D9697EEC3.gnuplot" using 1  notitle with lines
set style fill solid 0.5 border
set grid
set xrange[0:6]
plot  using 1  title "another_dv" with histograms lt rgb "red", sin(x)
set xrange[0:10]
set grid
plot cos(x) title "cos(x)", sin(x)/(x + 1)
unset multiplot)*";
        std::vector<int> v{ 1,2,3,4,5 };
        std::vector<double> d{ 1,20,13,14,25, 18 };

        auto cmd = get_plot_cmd(2, "set pointsize 5"_cmd,
            plot_t(v, "first implicit pane", plotstyle::s_step),
            std::vector<double>{2, 5, 6, 9, 11, 10, 3},
            pane(plot_t(std::vector<int>{10, 20, 30, 40, 50, 60}), plot_t(v, "v", plotstyle::s_step), std::vector<double>{2, 5, 6, 9, 11, 10, 3}),
            "set style fill solid 0.5 border"_cmd, pane(plot_t(d, "another_dv", plotstyle::s_histogram, "red"), "sin(x)"_cmd),
            "set xrange[0:10]"_cmd, "plot cos(x) title \"cos(x)\", sin(x)/(x + 1)"_cmd);

        // remove file names
        auto clean_str = [](const std::string& s)
            {
                std::stringstream ss(s);
                std::ostringstream clean;

                for (std::string str; std::getline(ss, str);)
                {
                    if (str.starts_with("plot"))
                    {
                        std::string quoted;
                        // remove all quoted
                        auto open_quote = false;
                        for (auto c : str)
                        {
                            if (c == '"' && open_quote)
                            {
                                if (!quoted.ends_with(".gnuplot"))
                                    clean << quoted << c;
                                quoted.clear();
                                open_quote = false;
                            }
                            else if (c == '"' && !open_quote)
                            {
                                quoted += c;
                                open_quote = true;
                            }
                            else if (open_quote)
                                quoted += c;
                            else
                                clean << c;
                        }
                    }
                    else
                        clean << str << "\n";
                }
                return clean.str();
            };

        gbassert(clean_str(cmd) == clean_str(golden));
    }
}
