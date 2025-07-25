//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2023, Gene Bushuyev
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

#include <type_traits>
#include <thread>
#include <atomic>
#include <mutex>
#include <future>

#include <vector>
#include <tuple>
#include <utility>
#include <functional>
#include <memory>
#include <future>
#include <type_traits>
#include <exception>
#include <chrono>

#include "taskcontainer.h"

namespace gb::yadro::async
{
    namespace detail
    {
        //---------------------------------------------------------------------
        struct callable { virtual ~callable() = default; virtual void call() = 0; };

        //---------------------------------------------------------------------
        // function_wrapper: a wrapper class for function object and arguments
        template<class F, class...Args>
        struct function_wrapper final : public callable
        {
            template<class Fun, class ... A>
            function_wrapper(Fun&& f, A&&... args) : f(std::forward<Fun>(f)), args(std::forward<A>(args)...) {}

            ~function_wrapper() override = default;

            auto get_future() { return p.get_future(); }

            virtual void call() override
            {
                try { set_value(); }
                catch (...) { p.set_exception(std::current_exception()); }
            }
        private:
            F f;
            std::tuple<Args...> args;
            using R = decltype(std::invoke(std::forward<F>(std::declval<F>()), std::declval<Args>()...));
            std::promise<R> p;

            decltype(auto) invoke() { return std::apply(std::forward<F>(f), args); }

            template<class T = R>
            std::enable_if_t<std::is_void_v<T>>
                set_value() { invoke(); p.set_value(); }

            template<class T = R>
            std::enable_if_t<!std::is_void_v<T>>
                set_value() { p.set_value(invoke()); }
        };
    }

    //---------------------------------------------------------------------
    // threadpool class: execute pooled tasks (tasks should not use locks)
    template<template<class T> class TaskContainer = task_queue>
    struct threadpool final
    {
        //-----------------------------------------------------------------
        threadpool(threadpool&&) = delete;
        //-----------------------------------------------------------------
        threadpool(std::size_t max_threads, std::function<void()> on_empty)
            : _max_threads(max_threads), _on_empty(on_empty)
        {}

        //-----------------------------------------------------------------
        using sleep_duration_t = decltype(std::chrono::milliseconds(0));
        threadpool(std::size_t max_threads, sleep_duration_t sleep_duration)
            : threadpool(max_threads, [=] { std::this_thread::sleep_for(sleep_duration); })
        {}

        //-----------------------------------------------------------------
        explicit threadpool(std::size_t max_threads = std::thread::hardware_concurrency())
            : threadpool(max_threads, [] { std::this_thread::yield(); })
        {}

        //-----------------------------------------------------------------
        explicit threadpool(std::function<void()> on_empty)
            : threadpool(std::thread::hardware_concurrency(), on_empty)
        {}


        //-----------------------------------------------------------------
        ~threadpool() { clear(); }

        //-----------------------------------------------------------------
        void join()
        {
            std::lock_guard<std::mutex> _(_m_thread);
            for (auto&& t : _threads)
                t.join();
        }

        //-----------------------------------------------------------------
        void clear()
        {
            _finish = true;
            join();
            _tasks.clear();
            _threads.clear();
        }

        //-----------------------------------------------------------------
        // enqueue the task and return a future
        template<class F, class...Args>
        [[nodiscard]] auto operator()(F&& f, Args&&... args)&
        {
            _finish = false;
            auto t = std::make_unique<detail::function_wrapper<std::decay_t<F>, std::decay_t<Args>...>>(std::forward<F>(f), std::forward<Args>(args)...);
            auto ftr = t->get_future();
            {
                std::lock_guard<std::mutex> _(_m_task);
                _tasks.enqueue(std::move(t));
            }
            inc_threads();
            return ftr;
        }

        //-----------------------------------------------------------------
        // enque task which waits for futures to get ready
        auto then(auto&& task, auto&&... futures)
            requires requires { (futures.get(), ...); }
        {
            {
                std::unique_lock _(_m_con);

                if (!_continuations)
                    _continuations = std::make_unique<threadpool<>>(std::max(_max_threads / 2, std::size_t(1)));
            }
            return (*_continuations)([](auto&& task, auto&&... futures)
                {
                    if constexpr (std::is_same_v<std::invoke_result_t<decltype(task), decltype(futures.get())...>, void>)
                        std::invoke(std::forward<decltype(task)>(task), futures.get()...);
                    else
                        return std::invoke(std::forward<decltype(task)>(task), futures.get()...);
                },
                std::forward<decltype(task)>(task), std::forward<decltype(futures)>(futures)...);
        }
        
        //-----------------------------------------------------------------
        auto max_thread_count() const { return _max_threads; }

    private:
        using task_t = std::unique_ptr<detail::callable>;
        TaskContainer<task_t> _tasks;
        std::vector<std::thread> _threads;
        std::mutex _m_thread;
        std::mutex _m_task;
        std::atomic_bool _finish{ false };
        std::size_t _max_threads;
        std::function<void()> _on_empty;
        std::unique_ptr < threadpool<>> _continuations; // secondary threadpool for continuations
        std::mutex _m_con; // protects continuations

        //-----------------------------------------------------------------
        auto dequeue()
        {
            std::lock_guard<std::mutex> _(_m_task);
            if (!_tasks.empty())
            {
                return _tasks.dequeue();
            }
            else
                return std::unique_ptr<detail::callable>{};
        }
        //-----------------------------------------------------------------
        void execute_loop()
        {
            while (!_finish)
            {
                if (auto tsk = dequeue())
                {
                    tsk->call();
                }
                else
                {
                    _on_empty();
                }
            }
        }
        //-----------------------------------------------------------------
        void inc_threads()
        {
            std::lock_guard<std::mutex> _(_m_thread);
            if (_threads.size() < _max_threads)
            {
                _threads.emplace_back([this]
                    {
                        execute_loop();
                    });
            }
        }

    };
}
