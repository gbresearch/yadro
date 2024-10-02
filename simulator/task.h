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
#include <coroutine>
#include <exception>

namespace gb::sim::coroutines
{
    //---------------------------------------------------------------------------------------------
    template<class Coroutine, class initial_suspend_t, class final_suspend_t>
    struct promise_base {
        auto get_return_object() noexcept { return Coroutine{}; }
        static initial_suspend_t initial_suspend() noexcept { return {}; }
        static final_suspend_t final_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept { _current_exception = std::current_exception(); }
        auto get_exception() const { return _current_exception; }
    private:
        std::exception_ptr _current_exception{};
    };

    //---------------------------------------------------------------------------------------------
    template<class T = void>
    struct task;

    template<>
    struct task<void> {
        struct promise_type : promise_base<task, std::suspend_never, std::suspend_never> {
            void return_void() {}
        };
    };

    template<class T>
    struct task {
        task(auto&& v) : value(v) {}
        struct promise_type : promise_base<task, std::suspend_never, std::suspend_never> {
            auto get_return_object() noexcept { return task{ value }; }
            void return_value(auto&& v) { value = decltype(v)(v); }
            T value;
        };
        operator T() const { return value; }
    private:
        T value;
    };
}