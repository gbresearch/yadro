#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <type_traits>

namespace gb::yadro::util
{
    //---------------------------------------------------------------------
    /**
    * Accumulating timer
    * 1. Create timer:
    *        static auto timer = make_accumulating_timer("timer name");
    *    or:
    *        static accumulating_timer timer(
    *          [](auto duration, auto count) {
    *            std::cout << "time: " << duration.count() << " ms, count: " << count << '\n';
    *          });
    * 2. Start/stop timer:
    *        {
    *          auto scoped_timer{ timer.make_scope_timer() };
    *          <some code>
    *        }
    *    or:
    *        auto scoped_timer{ timer.make_scope_timer() };
    *        <some code>
    *        scoped_timer.stop();
    */
    
    namespace detail
    {
        // nonlocking_mutex used in single threaded context
        struct nonlocking_mutex
        {
            void lock() const {}
            void unlock() const {}
        };
    }

    template<
        class time_unit,
        class clock = std::chrono::high_resolution_clock,
        class mutex = detail::nonlocking_mutex
    >
    struct accumulating_timer
    {

        template<class F>
        accumulating_timer(F fn)
            : _fn{ fn }
        {
        }

        auto& operator+= (auto d)
        {
            std::scoped_lock lock(_m);
            _duration += d;
            ++_count;
            return *this;
        }

        ~accumulating_timer() {
            _fn(std::chrono::duration_cast<time_unit>(_duration), _count);
        }

        const auto& get_duration() const { return _duration; }
        const auto get_count() const { return _count; }

        auto make_scope_timer() { return scope_timer(this); }

    private:
        std::function<void(time_unit, std::size_t)> _fn;
        typename clock::duration _duration{};
        std::size_t _count{};
        mutex _m;

        struct scope_timer
        {
            scope_timer(accumulating_timer<time_unit, clock, mutex>* t) :
                _atimer{ t },
                _start{ clock::now() }
            {}

            scope_timer(scope_timer&& other) :
                _atimer(other._atimer),
                _start(std::move(other._start))
            {
                other._atimer = nullptr;
                other._start = {};
            }

            ~scope_timer()
            {
                stop();
            }

            void stop()
            {
                if (_atimer && _start != time_point{})
                    *_atimer += clock::now() - _start;
                _atimer = nullptr;
            }

            void pause()
            {
                if (_atimer && _start != time_point{})
                    *_atimer += clock::now() - _start;
                _start = {};
            }

            void start()
            {
                _start = clock::now();
            }

        private:
            accumulating_timer<time_unit, clock, mutex>* _atimer;
            using time_point = typename clock::time_point;
            time_point _start;
        };
    };

    template<class duration>
    constexpr auto get_duration_suffix()
    {
        return duration::period::num != 1 ? ""
            : duration::period::den == 1'000'000'000 ? "nanosec"
            : duration::period::den == 1'000'000 ? "microsec"
            : duration::period::den == 1'000 ? "millisec"
            : duration::period::den == 1 ? "sec"
            : "";
    }

    template<class time_unit,
        class clock = std::chrono::high_resolution_clock,
        class mutex = detail::nonlocking_mutex>
    auto make_accumulating_timer(std::string name)
    {
        return accumulating_timer<time_unit, clock, mutex>([=](auto duration, auto count)
            {
                const auto& duration_s = std::to_string(duration.count()) + ' ' + get_duration_suffix<time_unit>();
                printf(":TIMER: %s time: %s, count: %zu\n", name.c_str(), duration_s.c_str(), count);
            });
    }

    // dependent timers for sub-blocks
    template<class time_unit, class clock, class mutex>
    auto make_slave_timer(std::string name, const accumulating_timer<time_unit, clock, mutex>& master_timer)
    {
        return accumulating_timer<time_unit, clock, mutex>(
            [timer_name = std::move(name), master = &master_timer](auto duration, auto count)
        {
            const auto& duration_s = std::to_string(duration.count()) + ' ' + get_duration_suffix<time_unit>();

            if (master->get_duration().count()) {
                auto percentage = 100.0 * duration.count() / std::chrono::duration_cast<time_unit>(master->get_duration()).count();
                printf(":SLAVE TIMER: %s time: %s (%f %%), count: %zu\n",
                    timer_name.c_str(), duration_s.c_str(), percentage, count);
            }
            else {
                printf(":SLAVE TIMER: %s time: %s, count: %zu%s\n",
                    timer_name.c_str(), duration_s.c_str(), count);
            }
        });
    }
}