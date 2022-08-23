//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
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


#include "taskcontainer.h"

namespace gb::async
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
        threadpool(unsigned max_threads, std::function<void()> on_empty)
            : _max_threads(max_threads), _on_empty(on_empty)
        {}

        //-----------------------------------------------------------------
        using sleep_duration_t = decltype(std::chrono::milliseconds(0));
        threadpool(unsigned max_threads, sleep_duration_t sleep_duration)
            : threadpool(max_threads, [=] { std::this_thread::sleep_for(sleep_duration); })
        {}

        //-----------------------------------------------------------------
        explicit threadpool(unsigned max_threads = std::thread::hardware_concurrency())
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
        void detach()
        {
            std::lock_guard<std::mutex> _(_m_thread);
            for (auto&& t : _threads)
                t.detach();
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
        auto operator()(F&& f, Args&&... args)&
        {
            _finish = false;
            auto t = std::make_unique<detail::function_wrapper<std::decay_t<F>, std::decay_t<Args>...>>(std::forward<F>(f), std::forward<Args>(args)...);
            auto ftr = t->get_future();
            std::lock_guard<std::mutex> _(_m_task);
            _tasks.enqueue(std::move(t));
            inc_threads();
            return ftr;
        }

    private:
        using task_t = std::unique_ptr<detail::callable>;
        TaskContainer<task_t> _tasks;
        std::vector<std::thread> _threads;
        std::mutex _m_thread;
        std::mutex _m_task;
        std::atomic_bool _finish{ false };
        unsigned _max_threads;
        std::function<void()> _on_empty;

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
