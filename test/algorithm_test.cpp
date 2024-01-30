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

#include "../util/gbtest.h"
#include "../util/misc.h"
#include "../archive/archive.h"
#include "../algorithm/genetic_optimization.h"
#include <iostream>
#include <thread>

namespace
{
    using namespace gb::yadro::algorithm;
    using namespace gb::yadro::util;
    using namespace gb::yadro::archive;

    GB_TEST(algorithm, genetic_optimization_test)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto x, auto y, auto z, auto v) 
            { return x * x + y * y + std::exp(z)/2 + std::exp(-z)/2 - 1 + ( v + std::sin(v))* (v + std::sin(v)); },
            std::tuple(0u, 10u), std::tuple(-10LL, 10LL), std::tuple(-10.f, 10.f), std::tuple(-10., 10.));

        auto [stat, opt_map] = optimizer.optimize(100ms, 5);
        gbassert(opt_map.size() == 5);
        gbassert(opt_map.begin()->first < 0.01); // may fail on very slow machines

#if defined(GB_DEBUGGING)
        std::cout << stat << "\n";
        for (auto&& opt : opt_map)
        {
            auto [target, xyzv] = opt;
            auto [x, y, z, v] = xyzv;
            std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
        }
#endif
    }

    GB_TEST(algorithm, genetic_optimization_mt_test)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto x, auto y, auto z, auto v)
            {
                auto sum{0.};
                for (auto i = 0; i < 1000; ++i)
                    sum += x * x + y * y + std::exp(z) / 2 + std::exp(-z) / 2 - 1 + (v + std::sin(v)) * (v + std::sin(v));
                return sum;
            },
            std::tuple(0u, 10u), std::tuple(-10LL, 10LL), std::tuple(-10.f, 10.f), std::tuple(-10., 10.));

        {// single thread
            auto [stat, opt_map] = optimizer.optimize(500ms, 5);
            gbassert(opt_map.size() == 5);
            gbassert(opt_map.begin()->first < 1); // may fail on very slow machines

#if defined(GB_DEBUGGING)
            std::cout << "single thread: " << stat << "\n";
            for (auto&& opt : opt_map)
            {
                auto [target, xyzv] = opt;
                auto [x, y, z, v] = xyzv;
                std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
            }
#endif
        }
        {// multithreaded
            gb::yadro::async::threadpool<> tp;
            auto [stat, opt_map] = optimizer.optimize(tp, 100ms, 5);
            gbassert(opt_map.size() == 5);
            gbassert(opt_map.begin()->first < 1); // may fail on very slow machines

#if defined(GB_DEBUGGING)
            std::cout << "multithreaded: " << stat << "\n";
            for (auto&& opt : opt_map)
            {
                auto [target, xyzv] = opt;
                auto [x, y, z, v] = xyzv;
                std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
            }
#endif
        }
    }
}
