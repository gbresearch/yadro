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

#include <functional>
#include <queue>
#include <vector>
#include <set>
#include <utility>
#include <concepts>
#include <tuple>
#include <chrono>

#include "task.h"
#include "event.h"
#include "fiber.h"
#include "../util/misc.h"

//-------------------------------------------------------------------------------------------------
namespace gb::sim
{
    using sim_time_t = std::uint64_t;

    struct scheduler_base_t
    {
        void schedule(std::convertible_to<std::function<void()>> auto&& call_back, sim_time_t delay = 0)
        {
            emplace(current_time() + delay, [call_back = decltype(call_back)(call_back)] { std::invoke(call_back); });
        }

        void schedule(auto&& e, sim_time_t delay = 0)
        {
            emplace(current_time() + delay, [fwd_e = gb::yadro::util::fwd_wrapper(decltype(e)(e))] { fwd_e.get().trigger(); });
        }

        // run scheduler until max_time is reached, forever by default
        auto run(sim_time_t max_time = (sim_time_t)(-1))
        {
            while (!_pq.empty() && _current_time < max_time)
            {
                _current_time = get_current_time();
                advance();
            }
            reset();
        }

        template<class Rep, class Period>
        auto run(std::chrono::duration<Rep, Period> d)
        {
            for (auto end_time = std::chrono::steady_clock::now() + d; 
                !_pq.empty() && std::chrono::steady_clock::now() < end_time;)
            {
                _current_time = get_current_time();
                advance();
            }
            reset();
        }

        void reset()
        {
            _pq = pqueue{}; _current_time = 0; _counter = 0;
        }

        auto current_time() const -> sim_time_t { return _current_time; }

        auto empty() const { return _pq.empty(); }

    private:
        using pq_rec = std::tuple<std::tuple<sim_time_t, std::uint64_t>, std::function<void()>>;
        using pqueue = std::priority_queue < pq_rec, std::vector<pq_rec>,
            decltype([](auto&& p1, auto&& p2) { return std::get<0>(p1) > std::get<0>(p2); }) > ;

        pqueue _pq;
        sim_time_t _current_time{};
        std::uint64_t _counter{}; // insertion counter to stabilize priority queue

        void emplace(sim_time_t t, auto&& call_back)
        {
            _pq.emplace(std::tuple{t, ++_counter }, decltype(call_back)(call_back));
        }

        sim_time_t get_current_time() const
        {
            return std::get<0>(std::get<0>(_pq.top()));
        }

        void advance()
        {
            for (; !_pq.empty() && _current_time == get_current_time(); _pq.pop())
            {
                std::invoke(std::get<1>(_pq.top()));
            }
        }

    };
}

namespace gb::sim::coroutines
{
    using namespace gb::sim;
    
    //---------------------------------------------------------------------------------------------
    using scheduler_t = scheduler_base_t;
}

//-------------------------------------------------------------------------------------------------
namespace gb::sim::fibers
{
    using namespace gb::sim;

    //---------------------------------------------------------------------------------------------
    struct scheduler_t : scheduler_base_t
    {
        scheduler_t();

        ~scheduler_t();

        auto forever(std::convertible_to<std::function<void()>> auto&& call_back)
        {
            _fibers.push_back(std::make_unique<fiber>(*this, decltype(call_back)(call_back)));
            return _fibers.back().get();
        }

        auto once(std::convertible_to<std::function<void()>> auto&& call_back)
        {
            _called_once.emplace(std::make_unique<fiber>(*this,
                [this, cb = gb::yadro::util::fwd_wrapper{ call_back }]
                {
                    std::invoke(cb.get());
                    finish();
                    
                    // schedule to delete this fiber on the next zero-cycle
                    schedule([this, fiber_to_destroy = this_fiber()] {
                        std::unique_ptr<fiber> tmp_unique(fiber_to_destroy);
                        _called_once.erase(tmp_unique);
                        tmp_unique.release();
                        });
                }));
        }

        void reset()
        {
            scheduler_base_t::reset();
            _fibers.clear();;
        }

    private:
        friend fiber;
        void* _main_fiber;
        std::vector<std::unique_ptr<fiber>> _fibers;    // forever fibers
        std::set<std::unique_ptr<fiber>> _called_once;  // once fibers
    };
}