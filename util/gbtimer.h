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
    template <class time_unit>
    class basic_generic_timer {
    public:
        using clock = std::chrono::high_resolution_clock;

        basic_generic_timer() {
            start();
        }

        void start() {
            _start = clock::now();
        }

        void stop() {
            _duration += std::chrono::duration_cast<time_unit>(clock::now() - _start);
        }

        void reset() {
            _duration = _duration.zero();
        }

        int get_time() {
            return _duration.count();
        }

    protected:
        clock::time_point _start;
        time_unit _duration;
    };

    using basic_timer = basic_generic_timer<std::chrono::microseconds>;

    using floating_point_milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;

    //---------------------------------------------------------------------
    /**
    * Accumulating timer
    * 1. Create timer:
    *        static auto timer = make_accumulating_timer("timer name");
    *    or:
    *        static accumulating_timer timer(
    *          [](auto duration, auto count, const auto& extra_data) {
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

    // nonlocking_mutex used in single threaded context
    struct nonlocking_mutex {};

    template<
        class display_time_unit = floating_point_milliseconds,
        class clock = std::chrono::high_resolution_clock,
        class mutex = nonlocking_mutex
    >
    struct accumulating_timer
    {
        using time_unit = decltype(clock::now() - clock::now());
        using extra_data_t = std::atomic_uint64_t; // allows to count bytes up to 18 exabytes (18 mln terabytes), so no overflow check for now
        using extra_data_map_t = std::map<std::string, extra_data_t>;

        template<class F>
        accumulating_timer(F fn, std::initializer_list<std::string> extra_fields = {})
            : _fn{ fn }
        {
            for (auto&& field_name : extra_fields) {
                _extra_data.emplace(field_name, 0);
            }
        }

        template<class add_time_unit, class M = mutex>
        auto operator+= (add_time_unit d) ->std::enable_if_t<std::is_same_v<M, nonlocking_mutex>, accumulating_timer&>
        {
            _duration += d; // max duration is at least 292 years, so no overflow check for now
            ++_count;
            return *this;
        }

        template<class add_time_unit, class M = mutex>
        auto operator+= (add_time_unit d) ->std::enable_if_t<!std::is_same_v<M, nonlocking_mutex>, accumulating_timer&>
        {
            std::scoped_lock lock(_m);
            _duration += d; // max duration is at least 292 years, so no overflow check for now
            ++_count;
            return *this;
        }

        ~accumulating_timer() {
            _fn(std::chrono::duration_cast<display_time_unit>(_duration), _count, _extra_data);
        }

        const auto& get_duration() const { return _duration; }
        const auto get_count() const { return _count; }

        auto make_scope_timer() { return scope_timer(this); }

        extra_data_t& operator[] (const std::string& name) {
            return _extra_data.at(name);
        }

    private:
        std::function<void(display_time_unit, std::size_t, const extra_data_map_t&)> _fn;
        time_unit _duration{};
        std::size_t _count{};
        extra_data_map_t _extra_data;
        mutex _m;

        struct scope_timer
        {
            scope_timer(accumulating_timer<display_time_unit, clock, mutex>* t) :
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
            accumulating_timer<display_time_unit, clock, mutex>* _atimer;
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

    template<class display_time_unit = floating_point_milliseconds,
        class clock = std::chrono::high_resolution_clock,
        class mutex = nonlocking_mutex>
    auto make_accumulating_timer(std::string name, std::initializer_list<std::string> extra_fields = {})
    {
        return accumulating_timer<display_time_unit, clock, mutex>([=](auto duration, auto count, const auto& extra_data)
            {
                std::string extra;
                for (auto&& d : extra_data) {
                    extra += ", " + d.first + ": " + std::to_string(d.second);
                }

                const auto& duration_s = std::to_string(duration.count()) + ' ' + get_duration_suffix<display_time_unit>();
                printf(":TIMER: %s time: %s, count: %zu%s\n", name.c_str(), duration_s.c_str(), count, extra.c_str());
            }, extra_fields);
    }

    template<class display_time_unit, class clock, class mutex>
    auto make_slave_timer(std::string name, const accumulating_timer<display_time_unit, clock, mutex>& master_timer)
    {
        return accumulating_timer<display_time_unit, clock, mutex>(
            [timer_name = std::move(name), master = &master_timer](auto duration, auto count, const auto& extra_data)
        {
            std::string extra;
            for (auto&& d : extra_data) {
                extra += ", " + d.first + ": " + std::to_string(d.second);
            }

            const auto& duration_s = std::to_string(duration.count()) + ' ' + get_duration_suffix<display_time_unit>();

            if (master->get_duration().count()) {
                auto percentage = 100.0 * duration.count() / std::chrono::duration_cast<display_time_unit>(master->get_duration()).count();
                printf(":SLAVE TIMER: %s time: %s (%f %%), count: %zu%s\n",
                    timer_name.c_str(), duration_s.c_str(), percentage, count, extra.c_str());
            }
            else {
                printf(":SLAVE TIMER: %s time: %s, count: %zu%s\n",
                    timer_name.c_str(), duration_s.c_str(), count, extra.c_str());
            }
        });
    }
}