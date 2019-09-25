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
#include <cassert>
#include <cstddef>
#include <iterator>

namespace gbr {
    namespace container {
        //*****************************************
        template<class T, size_t N>
        class static_vector
        {
            size_t size_ = 0;
            std::array<T, N> buffer_;

        public:
            //----------------------------
            // typedefs
            //----------------------------
            using value_type = T;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;
            using reference	= value_type&;
            using const_reference = const value_type&;
            using pointer =	value_type*;
            using const_pointer	= const value_type*;
            using iterator = typename std::array<T, N>::iterator;
            using const_iterator = typename std::array<T, N>::const_iterator;
            using reverse_iterator = typename std::array<T, N>::reverse_iterator;
            using const_reverse_iterator = typename std::array<T, N>::const_reverse_iterator;

            //----------------------------
            template<class Archive>
            void serialize(Archive&& a)
            {
                a(size_, buffer_);
            }

            //----------------------------
            static_vector() = default;

            //----------------------------
            explicit static_vector(size_type count, const T& value = T())
                : size_(count)
            {
                assert(count <= N);
                std::uninitialized_fill_n(buffer_.begin(), size_, value);
            }

            //----------------------------
            template<class InputIterator>
            static_vector(InputIterator first, InputIterator last)
            {
                assert(std::distance(first, last) <= N);
                auto i = std::uninitialized_copy(first, last, buffer_.begin());
                size_ = std::distance(buffer_.begin(), i);
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
            iterator        begin() { return buffer_.begin(); }
            //----------------------------
            const_iterator  begin() const { return buffer_.begin(); }
            //----------------------------
            iterator        end() { return buffer_.begin() + size_; }
            //----------------------------
            const_iterator  end() const { return buffer_.begin() + size_; }
            //----------------------------
            reference operator[](size_type n)
            {
                assert(n < size_);
                return buffer_[n];
            }
            //----------------------------
            const_reference operator[](size_type n) const
            {
                assert(n < size_);
                return buffer_[n];
            }
            //----------------------------
            reference at(size_type n)
            {
                return buffer_.at(n);
            }
            //----------------------------
            const_reference at(size_type n) const
            {
                return buffer_.at(n);
            }
            //----------------------------
            size_type size() const { return size_; }
            //----------------------------
            void swap(static_vector& p)
            {
                buffer_.swap(p.buffer_);
                std::swap(size_, p.size_);
            }
            //----------------------------
            void assign(size_type count, const T& value)
            {
                assert(count <= N);
                for (size_type i = 0; i < count && i < size_; ++i)
                    buffer_[i] = value;
                for (size_type i = count; i < size_; ++i)
                    pop_back();
                for (size_type i = size_; i < count; ++i)
                    push_back(value);
            }
            //----------------------------
            template<class InputIterator>
            void assign(InputIterator first, InputIterator last)
            {
                assert(std::distance(first, last) <= N);
                size_type count = 0;
                for (; first != last && count < size_; ++count, ++first)
                    buffer_[count] = *first;
                for (size_type i = count; i < size_; ++i)
                    pop_back();
                for (; first != last; ++first)
                    push_back(*first);
            }
            //----------------------------
            reference       back() { return buffer_[size_ - 1]; }
            //----------------------------
            const_reference back() const { return buffer_[size_ - 1]; }
            //----------------------------
            reference       front() { return buffer_[0]; }
            //----------------------------
            const_reference front() const { return buffer_[0]; }
            //----------------------------
            template<class U>
            void push_back(U&& value)
            {
                if (size_ < N)
                {
                    ::new (static_cast<void*>(std::addressof(buffer_[size_]))) value_type(std::forward<U>(value));
                    ++size_;
                }
                else
                    throw "static_vector::push_back";
            }
            //----------------------------
            template<class ...U>
            void emplace_back(U&&... value)
            {
                if (size_ < N)
                {
                    ::new (static_cast<void*>(std::addressof(buffer_[size_]))) value_type(std::forward<U>(value)...);
                    ++size_;
                }
                else
                    throw "static_vector::push_back";
            }
            //----------------------------
            void pop_back()
            {
                if (size_ > 0)
                {
                    back().~T();
                    --size_;
                }
                else
                    throw "static_vector::pop_back";
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
            template<class InputIterator>
            void insert(iterator position, InputIterator first, InputIterator last)
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
            bool empty() const { return size_ == 0; }
            //----------------------------
            bool full() const { return size_ == N; }
            //----------------------------
            iterator erase(iterator position)
            {
                assert(size_ > 0);
                std::move(position + 1, end(), position);
                pop_back();
                return position;
            }
            //----------------------------
            iterator erase(iterator first, iterator last)
            {
                assert(size_ > 0);
                std::move(last, end(), first);
                for (auto i = first; i != last; ++i)
                    pop_back();
                return first;
            }
            //----------------------------
            void resize(size_type size, const T& value = T())
            {
                if (size <= N)
                {
                    for (auto i = size; i < size_; ++i)
                        pop_back();
                    for (auto i = size_; i < size; ++i)
                        push_back(value);
                }
                else
                    throw "static_vector::resize";
            }
            //----------------------------
            void reserve(size_type size) const 
            {
                assert(size <= N);            
            }
        };
        //*****************************************
    }
} // namespace GB { namespace static_vector
