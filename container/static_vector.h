//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once

#include <array>
#include <algorithm>
#include <memory>
#include <cstddef>
#include <iterator>
#include <span>
#include "../util/gberror.h"
#include "../util/misc.h"
#include "../archive/archive.h"

namespace gb::yadro::container
{
    //*****************************************
    template<class T, size_t N>
    class static_vector
    {
        size_t _size = 0;
        std::array<T, N> _buffer;

    public:
        //----------------------------
        // typedefs
        //----------------------------
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = typename std::array<T, N>::iterator;
        using const_iterator = typename std::array<T, N>::const_iterator;
        using reverse_iterator = typename std::array<T, N>::reverse_iterator;
        using const_reverse_iterator = typename std::array<T, N>::const_reverse_iterator;

        //----------------------------
        template<class Archive>
        void serialize(Archive&& a)
        {
            a(_size);
            a(std::span(begin(), end()));
        }

        //----------------------------
        static_vector() = default;

        //----------------------------
        explicit static_vector(size_type count, const T& value = T())
            : _size(count)
        {
            util::gbassert(count <= N);
            std::uninitialized_fill_n(_buffer.begin(), _size, value);
        }

        //----------------------------
        template<std::forward_iterator It1, std::forward_iterator It2>
        static_vector(It1 first, It2 last)  requires(std::equality_comparable_with<It1, It2>)
        {
            util::gbassert(std::distance(first, last) <= N);
            auto i = std::uninitialized_copy(first, last, _buffer.begin());
            _size = std::distance(_buffer.begin(), i);
        }

        //----------------------------
        template<class T1, size_t N1>
        explicit static_vector(const static_vector<T1, N1>& p) : static_vector(p.begin(), p.end())
        {
        }

        //----------------------------
        template<class T1, size_t N1>
        static_vector& operator = (const static_vector<T1, N1>& p)
        {
            assign(p.begin(), p.end());
            return *this;
        }
        //----------------------------
        iterator        begin() { return _buffer.begin(); }
        //----------------------------
        const_iterator  begin() const { return _buffer.begin(); }
        //----------------------------
        iterator        end() { return _buffer.begin() + _size; }
        //----------------------------
        const_iterator  end() const { return _buffer.begin() + _size; }
        //----------------------------
        reference operator[](size_type n)
        {
            util::gbassert(n < _size);
            return _buffer[n];
        }
        //----------------------------
        const_reference operator[](size_type n) const
        {
            util::gbassert(n < _size);
            return _buffer[n];
        }
        //----------------------------
        reference at(size_type n)
        {
            return _buffer.at(n);
        }
        //----------------------------
        const_reference at(size_type n) const
        {
            return _buffer.at(n);
        }
        //----------------------------
        size_type size() const { return _size; }
        //----------------------------
        void swap(static_vector& p)
        {
            _buffer.swap(p._buffer);
            std::swap(_size, p._size);
        }
        //----------------------------
        void assign(size_type count, const T& value)
        {
            util::gbassert(count <= N);
            size_type i = 0;
            for (; i < count && i < _size; ++i)
                _buffer[i] = value;
            while (i < _size)
                pop_back();
            for (; i < count; ++i)
                push_back(value);
        }
        //----------------------------
        template<std::forward_iterator Iterator>
        void assign(Iterator first, Iterator last)
        {
            util::gbassert(std::distance(first, last) <= N);
            size_type count = 0;
            for (; first != last && count < _size; ++count, ++first)
                _buffer[count] = *first;
            while (count < _size)
                pop_back();
            for (; first != last; ++first)
                push_back(*first);
        }
        //----------------------------
        reference       back() { return _buffer[_size - 1]; }
        //----------------------------
        const_reference back() const { return _buffer[_size - 1]; }
        //----------------------------
        reference       front() { return _buffer[0]; }
        //----------------------------
        const_reference front() const { return _buffer[0]; }
        //----------------------------
        template<class U>
        void push_back(U&& value)
        {
            util::gbassert(_size < N);
            ::new (static_cast<void*>(std::addressof(_buffer[_size]))) value_type(std::forward<U>(value));
            ++_size;
        }
        //----------------------------
        template<class ...U>
        void emplace_back(U&&... value)
        {
            util::gbassert(_size < N);
            ::new (static_cast<void*>(std::addressof(_buffer[_size]))) value_type(std::forward<U>(value)...);
            ++_size;
        }
        //----------------------------
        void pop_back()
        {
            util::gbassert(_size > 0);
            back().~T();
            --_size;
        }
        //----------------------------
        template<class U>
        iterator insert(iterator position, U&& value)
        {
            push_back(std::forward<U>(value));
            for (auto i = end() - 1; i != position; --i)
                std::swap(*i, *(i - 1));
            return position;
        }
        //----------------------------
        iterator insert(iterator position, size_type count, const T& value)
        {
            static_vector<T, N> tmp(begin(), position);
            for (; count; --count)
                tmp.push_back(value);
            for (auto i = position; i != end(); ++i)
                tmp.push_back(*i);

            *this = std::move(tmp);
            return position;
        }
        //----------------------------
        template<std::forward_iterator It>
        auto insert(iterator position, It first, It last)
        {
            static_vector<T, N> tmp(begin(), position);
            for (; first != last; ++first)
                tmp.push_back(*first);
            for (auto i = position; i != end(); ++i)
                tmp.push_back(*i);

            *this = std::move(tmp);
            return position;
        }
        //----------------------------
        void clear() { while (!empty()) pop_back(); }
        //----------------------------
        size_type capacity() const { return N; }
        //----------------------------
        bool empty() const { return _size == 0; }
        //----------------------------
        bool full() const { return _size == N; }
        //----------------------------
        iterator erase(iterator position)
        {
            util::gbassert(_size > 0);
            std::move(position + 1, end(), position);
            pop_back();
            return position;
        }
        //----------------------------
        iterator erase(iterator first, iterator last)
        {
            util::gbassert(_size > 0);
            std::move(last, end(), first);
            for (auto i = first; i != last; ++i)
                pop_back();
            return first;
        }
        //----------------------------
        void resize(size_type size, const T& value = T())
        {
            util::gbassert(size <= N);
            while (size < _size)
                pop_back();
            while (size > _size)
                push_back(value);
        }

        //---------------------
        template<class Other>
        friend auto operator<=> (const static_vector& v1, const Other& v2) requires(std::three_way_comparable_with<T, typename Other::value_type>)
            && requires(const Other& v)
        {
            v.begin();
            v.size();
        }
        {
            return util::compare(std::span(v1.begin(), v1.size()), std::span(v2.begin(), v2.size()));
        }

        //---------------------
        template<class Other>
        friend auto operator== (const static_vector& v1, const Other& v2) { return std::is_eq(v1 <=> v2); }

        //---------------------
        template<class Other>
        friend auto operator!= (const static_vector& v1, const Other& v2) { return std::is_neq(v1 <=> v2); }
    };
} // namespaces
