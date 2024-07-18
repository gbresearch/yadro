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

#include "../util/gbutil.h"

#include <sstream>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <vector>
#include <deque>
#include <future>

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

    GB_TEST(util, tuples)
    {
        // test tuples
        std::unordered_set<std::tuple<unsigned, unsigned>, make_hash_t> s;
        s.insert({ 1,2 });
        s.emplace(std::tuple{ 2,2 });
        gbassert(s.contains(std::tuple{ 1,2 }));
        gbassert(s.contains(std::tuple{ 2,2 }));
        gbassert(!s.contains(std::tuple{ 1,3 }));
        auto [ts1, ts2] = tuple_split<3>(std::tuple(1, 2, 3, 4, 5));
        gbassert(ts1 == std::tuple(1, 2, 3));
        gbassert(ts2 == std::tuple(4, 5));
        auto [sp1, sp2, sp3] = tuple_split<1, 2>(std::tuple(1, 2, 3, 4, 5));
        gbassert(sp1 == std::tuple(1));
        gbassert(sp2 == std::tuple(2));
        gbassert(sp3 == std::tuple(3, 4, 5));
        auto [p1, p2, p3, p4] = tuple_split<1, 2, 3>(std::tuple(1, 2, 3, 4, 5));
        gbassert(p1 == std::tuple(1));
        gbassert(p2 == std::tuple(2));
        gbassert(p3 == std::tuple(3));
        gbassert(p4 == std::tuple(4, 5));
        auto t1 = std::tuple{ 1, 123.456, std::string("abc"), "xyz" };
        auto t2 = std::tuple{ "...", std::ignore, nullptr };
        gbassert(tuple_to_string(t1, t2) == "{1,123.456,abc,xyz}{...,,nullptr}");

        // test tuple_split
        auto [numbers, strings] = tuple_split<2>(t1);
        gbassert(tuple_to_string(numbers, strings) == "{1,123.456}{abc,xyz}");

        // test subtuple
        gbassert(tuple_to_string(subtuple<1, 3>(t1), subtuple<2>(t1)) == "{123.456,abc}{abc,xyz}");

        // test tuple_foreach
        auto filtered = tuple_foreach(t1, std::ignore, std::ignore,
            [](auto&& v) { return v + "_str"; },
            [](auto&& v) { return v + std::string("_str"); }
        );
        gbassert(tuple_to_string(filtered) == "{,,abc_str,xyz_str}");
        gbassert(tuple_to_string(tuple_remove_ignored(filtered)) == "{abc_str,xyz_str}");
        gbassert(tuple_to_string(tuple_select<1, 3>(t1)) == "{123.456,xyz}");

        // test tuple_transform
        auto t3 = tuple_transform(overloaded(
            [](int i) { return std::to_string(i); },
            [](double i) { return i; },
            [](const char* s) { return std::string(s); },
            [](auto&& other) { return other; }
        ), t1);

        gbassert(t3 == std::tuple(std::string("1"), 123.456, std::string("abc"), std::string("xyz")));

        gbassert(tuple_transform([](auto ... v)
            {   // return tuple of max values
                return std::max({ v... });
            }, std::tuple(1, 2, 3), std::tuple(3, 1, 4), std::tuple(2, 0, 8)) == std::tuple(3, 2, 8));

        gbassert(tuple_transform_reduce(// count bytes
            overloaded(
                [](int) { return 4; },
                [](double) { return 8; },
                [](const char* s) { return std::string(s).size(); },
                [](const std::string& s) { return s.size(); }
            ), [](auto&& ...v) { return (0 + ... + v); }, t1) == 18);

        static_assert(tuple_min(std::tuple(1, 2, 3), std::tuple(-1, -2, -3)) == -3);
        static_assert(tuple_max(std::tuple(1, 2, 3), std::tuple(-1, -2, -3)) == 3);

        // test conversion aggregates to tuples
        struct A { int a{ -1 }; unsigned b{ 2 }; double c{ 3.3 }; };
        static_assert(aggregate_to_tuple(A{}) == std::tuple{ -1, 2, 3.3 });
        struct B : A { std::size_t s{ 4 }; };
        static_assert(aggregate_member_count<B>() == 4);
        // aggregate_to_tuple(B{}) doesn't work, gcc/clang - compilation failure, MSVC - incorrect result

        {
            // test tuple flattening
            static_assert(make_flat_tuple(std::tuple{ 'a', std::tuple{'b', std::tuple{std::array{1,2,3},4}} }) == std::tuple{ 'a','b',1,2,3,4 });

            // conversion tuple to variant
            static_assert(tuple_to_variant(std::tuple{ 1,2. }, 0) ==
                std::variant<int, double>(std::in_place_index<0>, 1));
            static_assert(tuple_to_variant(std::tuple{ 1,2. }, 1) ==
                std::variant<int, double>(std::in_place_index<1>, 2.));
            static_assert(tuple_to_variant(std::array{ 1,2 }, 0) ==
                std::variant<int, int>(std::in_place_index<0>, 1));
            static_assert(std::visit([](auto v) { return (double)v; }, tuple_to_variant(std::tuple{ 1,2. }, 1)) == 2.);
            static_assert(std::visit([](auto v) { return v; }, tuple_to_variant(std::array{ 1,2 }, 0)) == 1);

            try {
                auto t = tuple_to_variant(std::array{ 1,2 }, 4);
            }
            catch (exception_t<>& e) { gbassert(e.what_str() == "bad tuple index"); }

            static_assert(std::get<0>(tuple_to_variant(std::tuple{ 1, 2.2, "string" }, 0)) == 1);
            static_assert(std::get<1>(tuple_to_variant(std::tuple{ 1, 2.2, "string" }, 1)) == 2.2);
            static_assert(std::get<2>(tuple_to_variant(std::tuple{ 1, 2.2, "string" }, 2)) == std::string("string"));
        }
        {
            // test variant transformation
            auto fun = overloaded(
                [](double v) { return v; },
                [](int v) { /* doesn't return */; },
                [](const std::string& v) { return v; });

            auto print = overloaded(
                [](void_type) { return std::string("void_type"); },
                [](wrong_arg_type) { return std::string("wrong_arg_type"); },
                [](std::string v) { return v; },
                [](auto&& v) { return std::to_string(v); });

            using variant_type = std::variant<int, int, long, double, std::string, std::tuple<int, int>>;

            gbassert(std::visit(print, transform(fun, variant_type{ "pi = " })) == "pi = ");
            gbassert(std::visit(print, transform(fun, variant_type{ 3.1415 })) == "3.141500");
            gbassert(std::visit(print, transform(fun, variant_type{ std::in_place_index<0>, 1 })) == "void_type");
            gbassert(std::visit(print, transform(fun, variant_type{ std::tuple(1,2) })) == "wrong_arg_type");
        }
    }

    GB_TEST(util, misc)
    {
        // misc utilities
        std::int64_t dur{};
        std::uint64_t cnt{};
        {
            accumulating_timer<std::chrono::milliseconds> t{
                [&](auto duration, auto count) { dur = duration.count(); cnt = count; } };

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
        gbassert(dur < 1);
        gbassert(cnt == 1);

        auto v{ 123 };
        {
            auto _{ retainer(v, 321) };
            gbassert(v == 321);
        }
        gbassert(v == 123);

        gbassert(std::format("{}", datetime_to_chrono(14000 + 13. / 24 + 25. / 24 / 60 + 15. / 24 / 60 / 60)) == "1938-04-30 13:25:15");
        gbassert(tokenize<char>("abc,xyz,foo,bar", ',') == std::vector<std::string>{ "abc", "xyz", "foo", "bar" });
        gbassert(!almost_equal(1.55, 1.54, 0.001));
        gbassert(almost_equal(1.55, 1.54, 0.011));
        gbassert(!almost_equal(std::vector{ 1.1, 1.2, 2.2 }, std::deque{ 1.11, 1.19, 2.3 }, 0.001));
        gbassert(almost_equal(std::vector{ 1.1, 1.2, 2.2 }, std::deque{ 1.11, 1.19, 2.3 }, 0.11));
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

    GB_TEST(util, misc_locked)
    {
        locked_resource<int> lint{ 123 };
        locked_resource<std::string> lstr{ 3, 'x' };
        lstr.visit([](auto&& s) { gbassert(s == "xxx"); });
        lstr.visit([](auto&& s, auto&& arg) { s = std::string("hello") + arg; }, " world");
        lstr.visit([](auto&& s) { gbassert(s == "hello world"); });
    }

    GB_TEST(util, string_util)
    {
        // test bas64
        std::vector v{ 0,1,2,3,4,5,6,7,8,9 };
        gbassert(base64_encode(v) == "AAAAAAEAAAACAAAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAA==");
        auto vv{ base64_decode(base64_encode(v)) };
        gbassert(std::ranges::equal(std::as_bytes(std::span(v)), std::as_bytes(std::span(vv))));
        gbassert(base64_encode(base64_decode(base64_encode(v))) == "AAAAAAEAAAACAAAAAwAAAAQAAAAFAAAABgAAAAcAAAAIAAAACQAAAA==");
        gbassert(base64_encode(2024) == "6AcAAA==");
        gbassert(base64_encode(std::array{ 2024u, 7u, 8u }) == "6AcAAAcAAAAIAAAA");

        // test MD5
        gbassert(md5string("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
        gbassert(md5string("The quick brown fox jumps over the lazy dog.") == "e4d909c290d0fb1ca068ffaddf22cbd0");
        gbassert(md5string("") == "d41d8cd98f00b204e9800998ecf8427e");
        gbassert(md5string("2024/07/08") == "f241350f66d24efb8720e345b462c5fc");
        gbassert(md5string(std::array{ 2024u, 7u, 8u }) == "73bf0bee1976d89dee2f6a293eca3312");
        gbassert(md5digest("2024/07/08") == md5digest("2024/07/08"));
        gbassert(md5digest("2024/07/08") != md5digest("2024/7/8"));
        gbassert(md5string("2024/07/08", "2024/7/8") == "500faf436e1f15bbac22a95b1e896b02");
        gbassert(base64_encode(md5digest("2024/07/08")) == "8kE1D2bSTvuHIONFtGLF/A==");
    }

    GB_TEST(util, win_pipe)
    {
        using namespace std::chrono_literals;
#if defined(GBWINDOWS)
        {   // single server-client test
            auto f = std::async(std::launch::async, [] {
                winpipe_server_t server(L"\\\\.\\pipe\\yadro\\pipe");
                server.run(
                    [](int) {},
                    std::function([&](int i) { return reinterpret_cast<std::uint64_t>(server.get_handle()); }),
                    [](std::vector<int> v) { return v; }, // echo vector
                    [](int i1, int i2, int i3) { return std::array{ i1, i2, i3 }; }
                );
                });

            winpipe_client_t client(L"\\\\.\\pipe\\yadro\\pipe", 5);
            gbassert(client.request<void>(0, 1));
            gbassert(client.request<std::uint64_t>(1, 0));
            gbassert(client.request<std::array<int, 3>>(3, 1, 2, 3).value() == std::array{ 1,2,3 });
            std::vector<int> vec(1'000'000, 0);
            vec[0] = 1; vec[1] = 2; vec[3] = 3; vec[4] = 4; vec[5] = 5; vec.back() = 12345;
            auto response = client.request<std::vector<int>>(2, vec);
            gbassert(response.value() == vec);
            // client.disconnect(); is implicit in client destructor
        }
        {   // test multi-instance server
            auto f = std::async(std::launch::async, []
                {
                    start_server(L"\\\\.\\pipe\\yadro\\pipe", nullptr,
                        [](int) {},
                        [](const std::vector<int>& v) { return v; }, // echo vector
                        [](int i1, int i2, int i3) { return std::array{ i1, i2, i3 }; },
                        []{},
                        [] { return 1; }
                        );
                });
            {   // first client
                winpipe_client_t client(L"\\\\.\\pipe\\yadro\\pipe", 5);
                gbassert(client.request<void>(0, 1));
                gbassert(client.request<std::array<int, 3>>(2, 1, 2, 3).value() == std::array{ 1,2,3 });
                gbassert(client.request<void>(3));
                gbassert(client.request<int>(4) == 1);
                client.disconnect();
            }
            {   // second client
                winpipe_client_t client(L"\\\\.\\pipe\\yadro\\pipe", 5);
                gbassert(client.request<void>(0, 1));
                gbassert(client.request<std::array<int, 3>>(2, 1, 2, 3).value() == std::array{ 1,2,3 });
                client.disconnect();
            }
            {   // two clients, overlaping requests
                winpipe_client_t client(L"\\\\.\\pipe\\yadro\\pipe", "first client", 5);
                gbassert(client.request<void>(0, 1));
                winpipe_client_t client1(L"\\\\.\\pipe\\yadro\\pipe", "second client", 5);
                gbassert(client.request<std::array<int, 3>>(2, 1, 2, 3).value() == std::array{ 1,2,3 });
                client.disconnect();
                gbassert(client1.request<void>(0, 1));
                gbassert(client1.request<std::array<int, 3>>(2, 1, 2, 3).value() == std::array{ 1,2,3 });
            }
            
            shutdown_server(L"\\\\.\\pipe\\yadro\\pipe", 5);
        }
#endif
    }
}
