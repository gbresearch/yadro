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
#include "../algorithm/regression_analysis.h"
#include <iostream>
#include <thread>

namespace
{
    using namespace gb::yadro::algorithm;
    using namespace gb::yadro::util;
    using namespace gb::yadro::archive;

    //--------------------------------------------------------------------------------------------
    // test optimization with tuples of fundamental types
    GB_TEST(algorithm, genetic_optimization_test, std::launch::async)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto x, auto y, auto z, auto v) 
            { return x * x + y * y + std::exp(z) / 2 + std::exp(-z) / 2 - 1 + (v + std::sin(v)) * (v + std::sin(v)); },
            std::less<>{},
            std::tuple(0u, 10u), std::tuple(-10LL, 10LL), std::tuple(-10.f, 10.f), std::tuple(-10., 10.));

        auto [stat, opt_map] = optimizer.optimize(100ms, 5);
        
        // only testing in optimized build, debug build can be too slow and tests would fail randomly
#if defined(NDEBUG)
        gbassert(opt_map.size() == 5);
        gbassert(opt_map.begin()->first < 0.01); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
        std::cout << "\n" << stat << "\n";
        for (auto&& opt : opt_map)
        {
            auto&& [target, xyzv] = opt;
            auto [x, y, z, v] = xyzv;
            std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
        }
#endif
    }

    //--------------------------------------------------------------------------------------------
    // test optimization with tuples of fundamental types, providing acceptable_target
    GB_TEST(algorithm, genetic_optimization_test1, std::launch::async)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto x, auto y, auto z, auto v)
            { return x * x + y * y + std::exp(z) / 2 + std::exp(-z) / 2 - 1 + (v + std::sin(v)) * (v + std::sin(v)); },
            std::less<>{},
            std::tuple(0u, 10u), std::tuple(-10LL, 10LL), std::tuple(-10.f, 10.f), std::tuple(-10., 10.));

        auto [stat, opt_map] = optimizer.optimize(0.01, 100ms, 5);

        // only testing in optimized build, debug build can be too slow and tests would fail randomly
#if defined(NDEBUG)
        gbassert(opt_map.size() == 5);
        gbassert(opt_map.begin()->first < 0.01); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
        std::cout << "\n" << stat << "\n";
        for (auto&& opt : opt_map)
        {
            auto&& [target, xyzv] = opt;
            auto [x, y, z, v] = xyzv;
            std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
        }
#endif
    }

    //--------------------------------------------------------------------------------------------
    // test optimization with tuples of ranges
    GB_TEST(algorithm, genetic_opt_range_test, std::launch::async)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto&& r)
            { return r[0] * r[0] + r[1] * r[1] + std::exp(r[2]) / 2 + std::exp(-r[2]) / 2 - 1 + (r[3] + std::sin(r[3])) * (r[3] + std::sin(r[3])); },
            std::less<>{},
            std::tuple{ std::vector{0., -10., -10., -10.}, std::vector{10., 10., 10., 10.} });

        optimizer.set_opt_parameters(0.6, 0.5, 0.4);
        auto [stat, opt_map] = optimizer.optimize(100ms, 5);

        // only testing in optimized build, debug build can be too slow and tests would fail randomly
#if defined(NDEBUG)
        gbassert(opt_map.size() == 5);
        gbassert(opt_map.begin()->first < 0.01); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
        std::cout << "\n" << stat << "\n";
        for (auto&& opt : opt_map)
        {
            auto&& [target, xyzv] = opt;
            auto&& [v] = xyzv;
            std::cout << "target: " << target << ", " << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << "\n";
        }
#endif
    }

    //--------------------------------------------------------------------------------------------
    GB_TEST(algorithm, genetic_optimization_mt_test, std::launch::async)
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

        optimizer.add_solution(1, 0, 1e-2f, 1e-3);

        {// single thread
            //optimizer.optimize(10ms, 5);
            auto [stat, opt_map] = optimizer.optimize(200ms, 5);

#if defined(NDEBUG)
            gbassert(opt_map.size() == 5);
            gbassert(opt_map.begin()->first < 0.1); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
            std::cout << "single thread:\n" << stat << "\n";
            for (auto&& opt : opt_map)
            {
                auto [target, xyzv] = opt;
                auto [x, y, z, v] = xyzv;
                std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
            }
#endif
        }
        {// multithreaded
            optimizer.clear();
            gb::yadro::async::threadpool<> tp;
            auto [stat, opt_map] = optimizer.optimize(tp, 100ms, 5);
#if defined(NDEBUG)
            gbassert(opt_map.size() == 5);
            gbassert(opt_map.begin()->first < 1); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
            std::cout << "multithreaded:\n" << stat << "\n";
            for (auto&& opt : opt_map)
            {
                auto [target, xyzv] = opt;
                auto [x, y, z, v] = xyzv;
                std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
            }
#endif
        }
        {// multithreaded with acceptable_target
            optimizer.clear();
            gb::yadro::async::threadpool<> tp;
            auto [stat, opt_map] = optimizer.optimize(tp, 0.01, 100ms, 5);
#if defined(NDEBUG)
            gbassert(opt_map.size() == 5);
            gbassert(opt_map.begin()->first < 1); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
            std::cout << "multithreaded with acceptable_target:\n" << stat << "\n";
            for (auto&& opt : opt_map)
            {
                auto [target, xyzv] = opt;
                auto [x, y, z, v] = xyzv;
                std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
            }
#endif
        }
    }

    //--------------------------------------------------------------------------------------------
    GB_TEST(algorithm, genetic_opt_serialization_test, std::launch::async)
    {
        using namespace std::chrono_literals;

        genetic_optimization_t optimizer([](auto x, auto y, auto z, auto v)
            { return x * x + y * y + std::exp(z) / 2 + std::exp(-z) / 2 - 1 + (v + std::sin(v)) * (v + std::sin(v)); },
            std::tuple(0u, 10u), std::tuple(-10LL, 10LL), std::tuple(-10.f, 10.f), std::tuple(-10., 10.));
        
        optimizer.optimize(10ms, 5);
        // serialize to memory archive
        gb::yadro::archive::omem_archive<> oma;
        oma(optimizer);
        optimizer.clear();
        // deserialize from memory archive
        gb::yadro::archive::imem_archive ima(std::move(oma));
        ima(optimizer);
        auto [stat, opt_map] = optimizer.optimize(1ms, 5);
#if defined(NDEBUG)
        gbassert(opt_map.size() == 5);
        gbassert(opt_map.begin()->first < 0.01); // may fail on very slow machines
#endif

#if defined(GB_DEBUGGING)
        std::cout << "\n" << stat << "\n";
        for (auto&& opt : opt_map)
        {
            auto [target, xyzv] = opt;
            auto [x, y, z, v] = xyzv;
            std::cout << "target: " << target << ", " << x << ", " << y << ", " << z << ", " << v << "\n";
        }
#endif
    }

    //--------------------------------------------------------------------------------------------
    GB_TEST(algorithm, regression_test, std::launch::async)
    {
        using namespace std::chrono_literals;

        // test residuals
        auto data = std::vector<std::tuple<double, double>>{ {1., 0.}, { 2.,1. } };
        gbassert(residuals([](auto x) { return x; }, data) == std::vector{1., 1.});
        gbassert(residuals([](auto x) { return 1 + x; }, data, std::minus<>{}) == std::vector{ 0., 0. });

        {
            // least-squares optimization
            auto opt = least_squares_optimizer([](auto a, auto b) { return [=](auto x) { return a + b * x; }; },
                data, std::tuple{ -2., 2. }, std::tuple{ -3., 3. });

            auto [stat, opt_map] = opt.optimize(50ms, 4);

#if defined(GB_DEBUGGING)
            std::cout << stat << "\n";

            for (auto&& opt : opt_map)
            {
                auto [target, ab] = opt;
                std::cout << "target: " << target << ", " << tuple_to_string(ab) << "\n";
            }
#endif
            gbassert(opt_map.size() == 4);
            auto [target, a, b] = make_flat_tuple(*opt_map.begin());
            gbassert(almost_equal(target, 0., 0.01));
            gbassert(almost_equal(a, 1., 0.1));
            gbassert(almost_equal(b, 1., 0.1));
        }

        // least absolute value optimization
        {
            auto opt = least_abs_optimizer([](auto a, auto b) { return [=](auto x) { return a + b * x; }; },
                data, std::tuple{ -2., 2. }, std::tuple{ -3., 3. });

            auto [stat, opt_map] = opt.optimize(50ms, 4);

#if defined(GB_DEBUGGING)
            std::cout << stat << "\n";

            for (auto&& opt : opt_map)
            {
                auto [target, ab] = opt;
                std::cout << "target: " << target << ", " << tuple_to_string(ab) << "\n";
            }
#endif
            gbassert(opt_map.size() == 4);
            auto [target, a, b] = make_flat_tuple(*opt_map.begin());
            gbassert(almost_equal(target, 0., 0.1));
            gbassert(almost_equal(a, 1., 0.1));
            gbassert(almost_equal(b, 1., 0.1));
        }
    }
}
