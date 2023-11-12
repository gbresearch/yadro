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

#pragma once

#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>
#include <future>

#include "gblog.h"
#include "gbtimer.h"
#include "gberror.h"
#include "../async/threadpool.h"

namespace gb::yadro::util
{
    struct tester;

    struct test_base
    {
        bool _result{ false };
        bool _enabled{ true };
        std::launch _policy;
        const char* _test_name{};

        explicit operator bool() { return _result; }

        virtual void run() const = 0;
        test_base(const char* test_name, const char* suite, std::launch policy = std::launch::deferred);
    };

    //-----------------------------------------------------------------------------------------------------------------
    struct tester
    {
        gb::async::threadpool<> _pool;
        std::unordered_map<std::string, std::vector<test_base*>> _tests;
        mutable logger _log;

        static tester& get()
        {
            static tester m;
            return m;
        }

        static bool run() { return get()._run(); }

        static void set_policy(std::launch policy)
        {
            for (auto& rec : get()._tests) for (auto& test : rec.second) test->_policy = policy;
        }
        
        static void disable_suites(const char* name, auto&& ... names)
        {
            get()._disable_suite(name);
            if constexpr (sizeof ...(names) != 0)
                disable_suites(names...);
        }
        
        static void disable_test(const char* suite, const char* test)
        {
            get()._disable_test(suite, test);
        }

        static void set_logger(auto&& ... streams)
        {
            get()._log.add_streams(std::forward<decltype(streams)>(streams)...);
        }

        static auto& get_logger()
        {
            return get()._log;
        }

        static void set_verbose(bool verbose)
        {
            get()._verbose = verbose;
        }

    private:
        bool _verbose{};

        bool _run()
        {
            std::vector<std::future<void>> futures;
            auto start_time = std::chrono::system_clock::now();

            for (auto& rec : _tests)
            {
                for (auto& test : rec.second)
                {
                    gbassert( test );
                    if (test->_enabled)
                    {
                        auto test_run = [&] {
                            try
                            {
                                auto t = std::chrono::system_clock::now();
                                test->run();
                                test->_result = true;
                                if (_verbose)
                                {
                                    auto ts = time_stamp() + " ";
                                    _log() << ts << rec.first << "." << test->_test_name << ":" << tab(ts.size() + 50) << "PASSED (" <<
                                        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - t).count()
                                        << " ms)" << std::endl;
                                }
                                else
                                    _log() << rec.first << "." << test->_test_name << ":" << tab(50) << "PASSED\n";
                            }
                            catch (std::exception& ex)
                            {
                                _log() << rec.first << "." << test->_test_name << ":" << tab(20) << "FAILED\n" << ex.what() << "\n";
                            }
                            catch (...)
                            {
                                _log() << rec.first << "." << test->_test_name << ":" << tab(20) << "FAILED, unknown exception\n";
                            }
                        };

                        if (test->_policy == std::launch::async)
                            futures.push_back(_pool(test_run));
                        else
                            test_run();
                    }
                    else
                    {
                        _log() << rec.first << "." << test->_test_name << ":" << tab(50) << "DISABLED\n";
                    }
                }
            }

            for (auto&& f : futures)
                f.get();
            if (_verbose)
            {
                _log() << "run time: " <<
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start_time).count()
                    << " ms\n";
            }
            return _statistics();
        }

        bool _statistics() const
        {
            std::size_t passed(0), failed(0), disabled(0);

            for (auto& rec : _tests)
            {
                for (auto& test : rec.second)
                {
                    if (!test->_enabled) ++disabled;
                    else if (test->_result) ++passed;
                    else ++failed;
                }
            }
            _log() << "tests passed: " << passed << ", failed: " << failed << ", disabled: " << disabled << "\n";
            return failed == 0;
        }

        void _disable_suite(const char* name)
        {
            auto [b,e] = _tests.equal_range(name);
            for (; b != e; ++b)
                for (auto&& test : b->second)
                    test->_enabled = false;
        }
        void _disable_test(const char* name, const char* test_name)
        {
            auto [b, e] = _tests.equal_range(name);
            for (; b != e; ++b)
                for (auto&& test : b->second)
                    if(test->_test_name == test_name)
                        test->_enabled = false;
        }
    };
}

//---------------------------------------------------------------------------------------------------------------------
inline gb::yadro::util::test_base::test_base(const char* test_name, const char* suite, std::launch policy)
    : _test_name(test_name), _policy(policy)
{
    gb::yadro::util::tester::get()._tests[suite].push_back(this);
}


//---------------------------------------------------------------------------------------------------------------------
#define GB_TEST(s, x, ...) \
        struct x : gb::yadro::util::test_base { \
        x()  : gb::yadro::util::test_base(#x, #s, __VA_ARGS__) {} \
        void run() const;\
        } x;\
        void x::run() const
