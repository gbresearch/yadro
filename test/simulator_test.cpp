//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2024, Gene Bushuyev
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

#include "../simulator/simulator.h"
#include "../util/gbtest.h"
#include "../util/misc.h"
#include <sstream>
#include <iostream>
#include <thread>

namespace
{
    using namespace gb::yadro::util;


    //--------------------------------------------------------------------------------------------
    GB_TEST(simulator, coroutine_test, std::launch::async)
    {
        using namespace gb::sim::coroutines;
    }

    GB_TEST(simulator, fiber_test, std::launch::async)
    {
        using namespace gb::sim;
        using namespace gb::sim::fibers;
        std::stringstream ss;
        scheduler_t sch;

        // print signals/events
        auto printer = [&](auto& s, const std::string& name) {
            if constexpr (requires{ s.read(); })
                always([&, name] { ss << sch.current_time() << ": " << name << "=" << s.read() << "\n"; }, s);
            else
                always([&, name] { ss << sch.current_time() << ": " << name << " triggered\n"; }, s);
            };

        // test fiber waiting on event
        event e1;
        printer(e1, "e1");
        sch.forever([&] { ss << get_sim_time() << ": enter fiber #1\n"; wait(e1); ss << get_sim_time() << ": fiber #1 resumed after wait\n"; finish(); });
        sch.schedule(e1, 1);
        sch.run();
        gbassert(ss.str() == "0: enter fiber #1\n1: e1 triggered\n1: fiber #1 resumed after wait\n");
        ss = std::stringstream{};

        // test fiber modigying signal
        signal<int> s1(0, sch), s2(1, sch);
        printer(s1, "s1"); printer(s1.pos_edge(), "s1.pos_edge"); printer(s1.neg_edge(), "s1.neg_edge");
        printer(s2, "s2"); printer(s2.pos_edge(), "s2.pos_edge"); printer(s2.neg_edge(), "s2.neg_edge");
        auto inv = [](auto& in, auto& out)
            {
                wait(in);
                out(1) = in.read() == 0 ? 1 : 0;
            };
        auto f = sch.forever([&] { inv(s1, s2); });
        s1 = 1;
        s1(1) = 0;
        s1(2) = 1;
        sch.run();
        gbassert(ss.str() == R"*(0: s1=1
0: s1.pos_edge triggered
1: s1=0
1: s1.neg_edge triggered
1: s2=0
1: s2.neg_edge triggered
2: s1=1
2: s1.pos_edge triggered
2: s2=1
2: s2.pos_edge triggered
3: s2=0
3: s2.neg_edge triggered
)*");

        // test clock generator fiber
        signal clk(false, sch);
        printer(clk, "clk");
        auto clk_gen = [](auto& clk, auto&& period) mutable { wait(clk); clk(period / 2) = !clk; };
        sch.forever([&] { clk_gen(clk, 2); });
        clk(0) = true;
        ss = std::stringstream{};
        sch.run(10);
        gbassert(ss.str() == R"*(0: clk=1
1: clk=0
2: clk=1
3: clk=0
4: clk=1
5: clk=0
6: clk=1
7: clk=0
8: clk=1
9: clk=0
10: clk=1
)*");
        // test generator
        auto generator = [](auto& clk, auto&& initial_delay, auto&& period) {
            clk(initial_delay) = !clk;
            always([&clk,period] { clk(period / 2) = !clk; }, clk);
            };
        clk.cancel_wait();
        generator(clk, 3, 2);
        ss = std::stringstream{};
        sch.run(10);
        //std::cout << ss.str();
        gbassert(ss.str() == R"*(3: clk=0
4: clk=1
5: clk=0
6: clk=1
7: clk=0
8: clk=1
9: clk=0
10: clk=1
)*");

        // test waiters and callbacks
        signal<bool> in1(false, sch), in2(false, sch), in3(false, sch), in4(false, sch);
        signal and2_out(false, sch), or2_out(false, sch), and3_out(false, sch), or3_out(false, sch), and4_out(false, sch), or4_out(false, sch);
        signal and2r_out(false, sch), or2r_out(false, sch), and3r_out(false, sch), or3r_out(false, sch), and4r_out(false, sch), or4r_out(false, sch);
        printer(in1, "in1"); printer(in2, "in2"); printer(in3, "in3"); printer(in4, "in4");
        printer(and2_out, "and2_out"); printer(and3_out, "and3_out"); printer(and4_out, "and4_out");
        printer(and2r_out, "and2r_out"); printer(and3r_out, "and3r_out"); printer(and4r_out, "and4r_out");
        printer(or2_out, "or2_out"); printer(or3_out, "or3_out"); printer(or4_out, "or4_out");
        printer(or2r_out, "or2r_out"); printer(or3r_out, "or3r_out"); printer(or4r_out, "or4r_out");

        // direct callbacks
        auto and_some = [](auto&& out, auto&... in) { always([&, out = sig_wrapper(decltype(out)(out))] { out = (in && ...); }, in...); };
        and_some(and2r_out(1), in1, in2);
        and_some(and3r_out(2), in1, in2, in3);
        and_some(and4r_out(3), in1, in2, in3, in4);
        auto or_some = [](auto&& out, auto&... in) { always([&, out = sig_wrapper(decltype(out)(out))] { out = (in || ...); }, in...); };
        or_some(or2r_out(1), in1, in2);
        or_some(or3r_out(2), in1, in2, in3);
        or_some(or4r_out(3), in1, in2, in3, in4);

        // fiber functions
        auto and_fn = [](auto&& out, auto& ...in)
            {
                wait_all(in ...);
                out(1) = (in && ...);
            };

        auto or_fn = [](auto&& out, auto& ...in)
            {
                wait_any(in ...);
                out = (in || ...);
            };

        sch.forever([&] { and_fn(and2_out, in1, in2); });
        sch.forever([&] { and_fn(and3_out(1), in1, in2, in3); });
        sch.forever([&] { and_fn(and4_out(2), in1, in2, in3, in4); });
        sch.forever([&] { or_fn(or2_out(1), in1, in2); });
        sch.forever([&] { or_fn(or3_out(1), in1, in2, in3); });
        sch.forever([&] { or_fn(or4_out(1), in1, in2, in3, in4); });

        generator(in1, 1, 4);
        generator(in2, 0, 4);
        generator(in3, 1, 6);
        generator(in4, 1, 8);
        ss = std::stringstream{};
        sch.run(10);
        gbassert(ss.str() == R"*(0: in2=1
1: in1=1
1: in3=1
1: in4=1
1: or2r_out=1
1: or2_out=1
1: or3_out=1
1: or4_out=1
2: or3r_out=1
2: in2=0
2: and2r_out=1
2: and2_out=1
3: or4r_out=1
3: in1=0
3: and3r_out=1
3: and3_out=1
3: and2r_out=0
4: in3=0
4: and4r_out=1
4: and4_out=1
4: and3r_out=0
4: in2=1
4: or2r_out=0
4: and2_out=0
4: or2_out=0
5: in4=0
5: and4r_out=0
5: in1=1
5: or3_out=0
5: or2r_out=1
5: or2_out=1
6: or3r_out=0
6: and3_out=0
6: or3r_out=1
6: in2=0
6: and2r_out=1
6: and2_out=1
6: or3_out=1
7: in3=1
7: in1=0
7: and2r_out=0
8: and4_out=0
8: in2=1
8: or2r_out=0
8: and2_out=0
8: or2_out=0
9: in4=1
9: in1=1
9: or2r_out=1
9: or2_out=1
10: in3=0
10: in2=0
10: and2r_out=1
10: and2_out=1
)*");
    }
}
