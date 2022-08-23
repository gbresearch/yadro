//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once

#include <vector>
#include <queue>
#include <stack>
#include <functional>
#include <future>


namespace gb::async
{
    //---------------------------------------------------------------------
    template<class T, class Container>
    class task_container;

    //---------------------------------------------------------------------
    // specialization for std::queue
    //---------------------------------------------------------------------
    template<class T>
    class task_container<T, std::queue<T>>
    {
        std::queue<T> _tasks;
    public:
        using value_type = T;
        auto size() const { return _tasks.size(); }
        auto empty() const { return _tasks.empty(); }
        void clear() { _tasks = std::queue<T>{}; }
        value_type dequeue()
        {
            auto top = std::move(_tasks.front());
            _tasks.pop();
            return top;
        }
        template<class V>
        void enqueue(V&& v)
        {
            _tasks.emplace(std::forward<V>(v));
        }
    };

    template<class T> using task_queue = task_container<T, std::queue<T>>;

    //---------------------------------------------------------------------
    // specialization for std::priority_queue
    //---------------------------------------------------------------------
    template<class T, class Compare>
    class task_container<T, std::priority_queue<T, std::vector<T>, Compare>>
    {
        std::priority_queue<T, std::vector<T>, Compare> _tasks;
    public:
        using value_type = T;
        auto size() const { return _tasks.size(); }
        auto empty() const { return _tasks.empty(); }
        void clear() { _tasks = std::priority_queue<T, std::vector<T>, Compare>{}; }
        value_type dequeue()
        {
            auto top = std::move(_tasks.top());
            _tasks.pop();
            return top;
        }
        template<class V>
        void enqueue(V&& v)
        {
            _tasks.emplace(std::forward<V>(v));
        }
    };

    template<class T, class Compare> using task_priority_queue = task_container<T, std::priority_queue<T, std::vector<T>, Compare>>;

    //---------------------------------------------------------------------
    // specialization for std::stack
    //---------------------------------------------------------------------
    template<class T>
    class task_container<T, std::stack<T>>
    {
        std::stack<T> _tasks;
    public:
        using value_type = T;
        auto size() const { return _tasks.size(); }
        auto empty() const { return _tasks.empty(); }
        void clear() { _tasks = std::stack<T>{}; }
        value_type dequeue()
        {
            auto top = std::move(_tasks.top());
            _tasks.pop();
            return top;
        }
        template<class V>
        void enqueue(V&& v)
        {
            _tasks.emplace(std::forward<V>(v));
        }
    };

    template<class T> using task_stack = task_container<T, std::stack<T>>;

    //---------------------------------------------------------------------
    // specialization for std::vector
    //---------------------------------------------------------------------
    template<class T>
    class task_container<T, std::vector<T>>
    {
        std::vector<T> _tasks;
    public:
        using value_type = T;
        auto size() const { return _tasks.size(); }
        auto empty() const { return _tasks.empty(); }
        void clear() { _tasks.clear(); }
        value_type dequeue()
        {
            auto top = std::move(_tasks.back());
            _tasks.pop_back();
            return top;
        }
        template<class V>
        void enqueue(V&& v)
        {
            _tasks.emplace_back(std::forward<V>(v));
        }
    };

    template<class T> using task_vector = task_container<T, std::vector<T>>;
}
