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

#include "event.h"
#include "scheduler.h"

namespace gb::sim
{
    //---------------------------------------------------------------------------------------------
    inline auto always(auto&& call_back, auto&& first_event, auto&& ... events)
    {
        (events.bind(call_back), ...); // copies of callback will be stored
        first_event.bind(decltype(call_back)(call_back)); // forward to the first event
    }
    //---------------------------------------------------------------------------------------------
    inline auto once(auto&& call_back, auto&& first_event, auto&& ... events)
    {
        (events.bind_once(call_back), ...); // copies of callback will be stored
        first_event.bind_once(decltype(call_back)(call_back)); // forward to the first event
    }

    //---------------------------------------------------------------------------------------------
    // sig_wrapper wraps either an lvalue reference signal or an rvalue delayed_writer
    template<class T>
    struct sig_wrapper
    {
        sig_wrapper(T&& t) : _t(std::forward<T>(t)) {}
        operator const T& () const { return _t; }
        decltype(auto) read() const { return _t.read(); }
        auto& operator= (auto&& value) const { _t = decltype(value)(value); return *this; }
    private:
        T _t;
    };

    template<class T>
    sig_wrapper(T&&) -> sig_wrapper<T>;

    // common implementation for coroutines and fibers
    namespace detail
    {
        template<class T, class EventType>
        struct const_signal : EventType
        {
            const_signal() requires(std::default_initializable) : _value{} {}
            const_signal(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
            auto&& read() const { return _value; }
        private:
            T _value;
        };

        //---------------------------------------------------------------------------------------------
        // signal_base only used as a base class, wraps type T, triggers event when Compare is true
        template<class T, class Compare, class EventType>
        struct signal_base : EventType
        {
            using type = T;

            signal_base() requires(std::default_initializable) : _value{} {}
            signal_base(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
            auto&& read() const { return _value; }
            operator const T& () const { return read(); }

        protected:
            void trigger() { EventType::trigger(); } // hide access to event::trigger
            auto set_value(std::convertible_to<T> auto&& value)
            {
                if (Compare{}(_value, value))
                {
                    _value = decltype(value)(value);
                    trigger();
                }
            }
        private:
            T _value;
        };

        //---------------------------------------------------------------------------------------------
        // wire wraps type T, triggers event when Compare is true, writing values immediately without scheduling
        template<class T, class Compare, class EventType>
        struct wire : signal_base<T, Compare, EventType>
        {
            using base = signal_base<T, Compare, EventType>;
            using base::base;

            auto& operator= (auto&& value) { return write(decltype(value)(value)); }

            auto& write(std::convertible_to<T> auto&& value)
            {
                this->set_value(decltype(value)(value));
                return *this;
            }
        };

        //---------------------------------------------------------------------------------------------
        // signal wraps type T, triggers event when Compare is true, schedules writing values with specified delay
        template<class T, class Compare, class EventType, class Scheduler>
        struct signal : signal_base<T, Compare, EventType>
        {
            using base = signal_base<T, Compare, EventType>;

            signal() = default;
            signal(auto&& initial, Scheduler& scheduler) : base(decltype(initial)(initial)), _scheduler(scheduler) {}

            auto& operator= (auto&& value) { return write(decltype(value)(value), 0); }

            auto operator() (sim_time_t delay) { return delayed_writer(*this, delay); }

            auto& write(std::convertible_to<T> auto&& value, auto&& delay)
            {
                _scheduler.schedule([value = decltype(value)(value), this] { this->set_value(value); }, delay);
                return *this;
            }

            auto current_time() const { return _scheduler.current_time(); }

        private:
            Scheduler& _scheduler;

            struct delayed_writer
            {
                delayed_writer(signal& s, sim_time_t delay) : s(s), delay(delay) {}
                auto& operator= (auto&& value) const { return s.write(decltype(value)(value), delay); }
                operator const T& () const { return s.read(); }
                decltype(auto) read() const { return s.read(); }
                auto operator() (sim_time_t delay) { return delayed_writer(s, delay + this->delay); }
            private:
                signal& s;
                sim_time_t delay;
            };
        };

        //---------------------------------------------------------------------------------------------
        // conditional event is always an rvalue
        template<class Signal, class Compare>
        struct conditional_event
        {
            conditional_event(conditional_event&& other) : _s(other._s), _old_value(std::move(_old_value)) {}
            conditional_event(Signal& s, Compare comp = {}) : _s(s), _old_value(s.read()) {}
            void bind(auto fun) { _s.bind(always_callback(std::move(fun))); }
            void bind_once(auto fun) { _s.bind_once(wait_callback(std::move(fun))); }
            void bind_cancellable(void* p, auto&& f) { _s.bind_cancellable(p, decltype(f)(f)); }
            void cancel_wait(void* p) { _s.cancel_wait(p); }
            void cancel_wait() { _s.cancel_wait(); }
            void visit(auto&& fn) { std::invoke(decltype(fn)(fn), _s); }

        private:
            Signal& _s;
            typename Signal::type _old_value{};

            // wait_callback bind callback for as long as the temporary conditional_event is alive
            auto wait_callback(auto fun)
            {
                return [this, fun]
                    {
                        auto&& vref = _s.read();
                        auto comp = Compare{}(vref, _old_value);
                        _old_value = decltype(vref)(vref);

                        if (comp)
                            std::invoke(fun);
                        else
                            this->bind_once(fun);
                    };
            }

            // always_callback binds callback that must persist after this temporary conditional_event destroyed
            auto always_callback(auto fun)
            {
                return [&s = _s, old_value = _old_value, fun] () mutable
                    {
                        auto&& vref = s.read();
                        auto comp = Compare{}(vref, old_value);
                        old_value = decltype(vref)(vref);

                        if (comp)
                            std::invoke(fun);
                    };
            }
        };

    }
}

namespace gb::sim::coroutines
{
    //---------------------------------------------------------------------------------------------
    // constant signal that doesn't change value doesn't trigger events
    template<class T>
    struct const_signal : gb::sim::detail::const_signal<T, empty_event>
    {
        using base = gb::sim::detail::const_signal<T, empty_event>;
        using base::base;
    };

    template<class T>
    const_signal(T) -> const_signal<T>;

    template<class T, class Compare = std::not_equal_to<>>
    struct wire : gb::sim::detail::wire<T, Compare, event>
    {
        using base = gb::sim::detail::signal_base<T, Compare, event>;
        using base::base;
    };

    template<class T>
    wire(T) -> wire<T, std::not_equal_to<>>;

    template<class T, class Compare = std::not_equal_to<>>
    struct signal : gb::sim::detail::signal<T, Compare, event, scheduler_t>
    {
        using base = gb::sim::detail::signal<T, Compare, event, scheduler_t>;
        using base::base;
        using base::operator=;
    };

    template<class T>
    signal(T, scheduler_t&) -> signal<T, std::not_equal_to<>>;

    //---------------------------------------------------------------------------------------------
    template<class Signal, class Compare>
    struct conditional_event : gb::sim::detail::conditional_event<Signal, Compare>
    {
        using base = gb::sim::detail::conditional_event<Signal, Compare>;
        using base::base;

        void await_suspend(auto h) { base::bind_once([h] { h.resume(); }); }
        void await_resume() {}
        auto await_ready() { return false; }
    };

    template<class Signal>
    using pos_edge = conditional_event<Signal, std::greater<>>;

    template<class Signal>
    using neg_edge = conditional_event<Signal, std::less<>>;
}

namespace gb::sim::fibers
{
    //---------------------------------------------------------------------------------------------
    // constant signal that doesn't change value doesn't trigger events
    template<class T>
    struct const_signal : gb::sim::detail::const_signal<T, empty_event>
    {
        using base = gb::sim::detail::const_signal<T, empty_event>;
        using base::base;
    };

    template<class T>
    const_signal(T) -> const_signal<T>;

    //---------------------------------------------------------------------------------------------
    template<class T, class Compare = std::not_equal_to<>>
    struct wire : gb::sim::detail::wire<T, Compare, event>
    {
        using base = gb::sim::detail::signal_base<T, Compare, event>;
        using base::base;
    };

    template<class T>
    wire(T) -> wire<T, std::not_equal_to<>>;

    //---------------------------------------------------------------------------------------------
    template<class T, class Compare = std::not_equal_to<>>
    struct signal : gb::sim::detail::signal<T, Compare, event, scheduler_t>
    {
        using base = gb::sim::detail::signal<T, Compare, event, scheduler_t>;
        using base::base;
        using base::operator=;
    };

    template<class T>
    signal(T, scheduler_t&) -> signal<T, std::not_equal_to<>>;

    //---------------------------------------------------------------------------------------------
    template<class Signal, class Compare>
    using conditional_event = gb::sim::detail::conditional_event<Signal, Compare>;

    template<class Signal>
    using pos_edge = conditional_event<Signal, std::greater<>>;

    template<class Signal>
    using neg_edge = conditional_event<Signal, std::less<>>;

}
