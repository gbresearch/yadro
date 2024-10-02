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

namespace gb::sim::coroutines
{
    //---------------------------------------------------------------------------------------------
    template<class T>
    struct const_signal
    {
        const_signal() = default;
        const_signal(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
        auto&& read() const { return _value; }
        auto& changed() { return _e; }
        auto& pos_edge() { return _e; }
        auto& neg_edge() { return _e; }
    private:
        T _value{};
        [[no_unique_address]] empty_event _e;
    };

    template<class T>
    const_signal(T) -> const_signal<T>;

    //---------------------------------------------------------------------------------------------
    template<class T, class Compare>
    struct signal_base : event
    {
        signal_base() = default;
        signal_base(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
        signal_base(std::convertible_to<T> auto&& initial, Compare compare) : _value(decltype(initial)(initial)), _compare(std::move(compare)) {}
        auto&& read() const { return _value; }
        operator const T& () const { return read(); }

        //auto& changed() { return _changed; }
        auto& pos_edge() { return _pos_edge; }
        auto& neg_edge() { return _neg_edge; }
    protected:
        void trigger() { event::trigger(); } // hide access to event::trigger
        auto set_value(std::convertible_to<T> auto&& value) 
        { 
            if (Compare{}(_value, value))
            {
                _value = decltype(value)(value);
                trigger();
                pos_edge().trigger();
            }
            else if (Compare{}(_value, _value))
            {
                _value = decltype(value)(value);
                trigger();
                neg_edge().trigger();
            }
        }
    private:
        T _value{};
        [[no_unique_address]] Compare _compare;
        event _pos_edge;
        event _neg_edge;
    };

    //---------------------------------------------------------------------------------------------
    template<class T, class Compare = std::less<>>
    struct wire : signal_base<T, Compare>
    {
        using base = signal_base<T, Compare>;
        using base::base;

        auto& operator= (auto&& value) { return write(decltype(value)(value)); }

        auto& write(std::convertible_to<T> auto&& value)
        {
            this->set_value(decltype(value)(value));
            return *this;
        }
    };

    template<class T, class Compare>
    wire(T, Compare) -> wire<T, Compare>;

    template<class T>
    wire(T) -> wire<T, std::less<>>;

    //---------------------------------------------------------------------------------------------
    template<class T, class Scheduler, class Compare = std::less<>>
    struct signal : signal_base<T, Compare>
    {
        using base = signal_base<T, Compare>;

        signal() = default;
        signal(auto&& initial, auto& scheduler) : base(decltype(initial)(initial)), _scheduler(scheduler) {}

        auto& operator= (auto&& value) { return write(decltype(value)(value), 0); }

        auto& write(std::convertible_to<T> auto&& value, auto&& delay)
        {
            _scheduler.schedule([value = decltype(value)(value), this] { this->set_value(value); }, delay);
            return *this;
        }

    private:
        Scheduler& _scheduler;
    };

    template<class T, class Scheduler, class Compare>
    signal(T, Scheduler&, Compare) -> signal<T, Scheduler, Compare>;

    template<class T, class Scheduler>
    signal(T, Scheduler&) -> signal<T, Scheduler, std::less<>>;

    template<class T>
    signal(T) -> signal<T, void, std::less<>>;

    //---------------------------------------------------------------------------------------------
    inline auto sensitive(auto&& call_back, auto&& ... events)
    {
        (events.bind(decltype(call_back)(call_back)), ...);
    }
}

namespace gb::sim::fibers
{
    //---------------------------------------------------------------------------------------------
    // constant signal that doesn't change value doesn't trigger events
    template<class T>
    struct const_signal : empty_event
    {
        const_signal() requires(std::default_initializable) : _value{} {}
        const_signal(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
        auto&& read() const { return _value; }
    private:
        T _value;
    };

    template<class T>
    const_signal(T) -> const_signal<T>;

    //---------------------------------------------------------------------------------------------
    // signal_base only used as a base class, wraps type T, triggers event when Compare is true
    template<class T, class Compare>
    struct signal_base : event
    {
        using type = T;

        signal_base() requires(std::default_initializable) : _value{} {}
        signal_base(std::convertible_to<T> auto&& initial) : _value(decltype(initial)(initial)) {}
        auto&& read() const { return _value; }
        operator const T& () const { return read(); }

    protected:
        void trigger() { event::trigger(); } // hide access to event::trigger
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
    template<class T, class Compare = std::not_equal_to<>>
    struct wire : signal_base<T, Compare>
    {
        using base = signal_base<T, Compare>;
        using base::base;

        auto& operator= (auto&& value) { return write(decltype(value)(value)); }

        auto& write(std::convertible_to<T> auto&& value)
        {
            this->set_value(decltype(value)(value));
            return *this;
        }
    };

    template<class T>
    wire(T) -> wire<T, std::not_equal_to<>>;

    //---------------------------------------------------------------------------------------------
    // signal wraps type T, triggers event when Compare is true, schedules writing values with specified delay
    template<class T, class Compare = std::not_equal_to<>>
    struct signal : signal_base<T, Compare>
    {
        using base = signal_base<T, Compare>;

        signal() = default;
        signal(auto&& initial, scheduler_t& scheduler) : base(decltype(initial)(initial)), _scheduler(scheduler) {}

        auto& operator= (auto&& value) { return write(decltype(value)(value), 0); }

        auto operator() (sim_time_t delay) { return delayed_writer(*this, delay); }

        auto& write(std::convertible_to<T> auto&& value, auto&& delay)
        {
            _scheduler.schedule([value = decltype(value)(value), this] { this->set_value(value); }, delay);
            return *this;
        }

        auto current_time() const { return _scheduler.current_time(); }

    private:
        scheduler_t& _scheduler;

        struct delayed_writer
        {
            delayed_writer(signal& s, sim_time_t delay) : s(s), delay(delay) {}
            auto& operator= (auto&& value) const { return s.write(decltype(value)(value), delay); }
            operator const T&() const { return s.read(); }
            decltype(auto) read() const { return s.read(); }
            auto operator() (sim_time_t delay) { return delayed_writer(s, delay + this->delay); }
        private:
            signal& s;
            sim_time_t delay;
        };
    };

    template<class T>
    signal(T, scheduler_t&) -> signal<T, std::not_equal_to<>>;

    //---------------------------------------------------------------------------------------------
    // conditional event is always an rvalue
    template<class Signal, class Compare>
    struct conditional_event
    {
        conditional_event(conditional_event&& other) : _s(other._s), _old_value(std::move(_old_value)){}
        conditional_event(Signal& s, Compare comp = {}) : _s(s), _old_value(s.read()) {}
        void bind(auto fun) { _s.bind(always_callback(std::move(fun))); }
        void bind_once(auto fun) { _s.bind_once(wait_callback(std::move(fun))); }
        void bind_cancellable(auto fun) { _s.bind_cancellable(wait_callback(std::move(fun))); }
        void cancel_wait(void* p) { _s.cancel_wait(p); }
        void cancel_wait() { _s.cancel_wait(); }

    private:
        Signal& _s;
        typename Signal::type _old_value{};

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

        auto always_callback(auto fun)
        {
            return [this, fun]
                {
                    auto&& vref = _s.read();
                    auto comp = Compare{}(vref, _old_value);
                    _old_value = decltype(vref)(vref);

                    if (comp)
                        std::invoke(fun);
                };
        }
    };

    template<class Signal>
    using pos_edge = conditional_event<Signal, std::greater<>>;

    template<class Signal>
    using neg_edge = conditional_event<Signal, std::less<>>;

    //---------------------------------------------------------------------------------------------
    // sig_wrapper wraps either an lvalue reference signal or an rvalue delayed_writer
    template<class T>
    struct sig_wrapper
    {
        sig_wrapper(T&& t) : _t(std::forward<T>(t)) {}
        operator const T&() const { return _t; }
        decltype(auto) read() const { return _t.read(); }
        auto& operator= (auto&& value) const { _t = decltype(value)(value); return *this; }
    private:
        T _t;
    };

    template<class T>
    sig_wrapper(T&&) -> sig_wrapper<T>;
}

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
}