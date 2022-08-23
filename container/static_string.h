//-----------------------------------------------------------------------------
//  Gene Bushuyev (c) 2006-2022. All Rights Reserved
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cassert>
#include <string>
#include <stdexcept>
#include <array>
#include <functional>
#include <utility>
#include <compare>
#include <iterator>
#include <concepts>
#include <span>
#include "../util/gberror.h"
#include "../util/misc.h"
#include "../archive/archive.h"

#pragma warning( disable: 4996 )

namespace gb::yadro::container
{
    template<class A, class B>
    concept plus_assignable = requires(A&& a, B&& b)
    {
        { std::forward<A>(a) += std::forward<B>(b) } -> std::convertible_to<A>;
    };

    template<class T>
    concept sequence = requires(T && t)
    {
        std::begin(std::forward<T>(t));
        std::end(std::forward<T>(t));
    };

    template<class T>
    concept c_str_c = requires(T && t)
    {
        typename std::remove_cvref_t<T>::value_type;
        {t.c_str()} -> std::convertible_to<std::add_pointer_t<std::add_const_t<typename std::remove_cvref_t<T>::value_type>>>;
    };

    //-------------------------------------static_string
    template<size_t N, class CharT = char, class Traits = std::char_traits<CharT>>
    struct static_string
    {
        using size_type = std::make_unsigned_t<CharT>;
        using value_type = CharT;
        using traits_type = Traits;
        static constexpr auto max_size = std::numeric_limits<size_type>::max();
        static_assert(N < max_size);

        template<class Archive>
        void serialize(Archive&& a)
        {
            a(_size);
            a(std::span(_buf.begin(), _buf.begin() + _size));
        }

        struct static_string_error : std::runtime_error
        {
            static_string_error() : runtime_error("static_string_error") {}
        };

        //---------------------
        static_string() : _size(0) { _buf[0] = 0; }

        //---------------------
        static_string(size_type length, CharT fill_char) : _size(length)
        {
            util::gbassert(N >= _size);
            std::fill_n(_buf.begin(), _size, fill_char);
            _buf[_size] = 0;
        }

        //---------------------
        static_string(const static_string& str) : _size(str._size)
        {
            std::copy(str._buf.begin(), str._buf.end(), _buf.begin());
            _buf[_size] = 0;
        }

        //---------------------
        template<std::forward_iterator It1, std::forward_iterator It2>
        static_string(It1 first, It2 last) requires(std::equality_comparable_with<It1, It2>)
        {
            for (_size = 0; _size < N && first != last && *first != 0; ++first, ++_size)
                _buf[_size] = *first;
            _buf[_size] = 0;
        }

        //---------------------
        static_string(auto&& other)  requires(sequence<decltype(other)>)
            : static_string(std::begin(std::forward<decltype(other)>(other)), std::end(std::forward<decltype(other)>(other)))
        {
        }

        //---------------------
        explicit static_string(const CharT* str)
        {
            util::gbassert(str);
            size_type i = 0;
            for (; i < N && str[i]; ++i)
                _buf[i] = str[i];
            util::gbassert(i < N);
            _size = i;
            _buf[_size] = 0;
            util::gbassert(str[i] == 0);
        }

        //---------------------
        // member functions
        //---------------------
        void swap(static_string& str)
        {
            std::swap_ranges(_buf.begin(), _buf.end(), str._buf.begin());
            std::swap(_size, str._size);
        }

        //---------------------
        static_string& operator = (const static_string& str)
        {
            static_string<N, CharT>(str).swap(*this);
            return *this;
        }

        //---------------------
        auto& operator= (const auto& s) requires(std::convertible_to<std::decay_t<decltype(s)>, static_string>)
        {
            static_string(s).swap(*this);
            return *this;
        }

        //---------------------
        static_string& operator = (const CharT* str)
        {
            static_string<N, CharT>(str).swap(*this);
            return *this;
        }

        //---------------------
        static_string& operator += (const CharT* str)
        {
            util::gbassert(str);
            size_type j = 0;
            for (; _size < N && str[j]; ++_size, ++j)
                _buf[_size] = str[j];
            _buf[_size] = 0;
            util::gbassert(str[j] == 0);
            return *this;
        }

        //---------------------
        static_string& operator += (CharT ch)
        {
            util::gbassert(_size < N);
            _buf[_size] = ch;
            ++_size;
            _buf[_size] = 0;
            return *this;
        }

        //---------------------
        auto& operator+= (const auto& s) requires(c_str_c<decltype(s)>)
        {
            return *this += s.c_str();
        }

        //---------------------
        friend auto operator+ (const static_string& s, const auto& other) requires(plus_assignable<static_string, decltype(other)>)
        {
            return static_string(s) += other;
        }

        //---------------------
        CharT& operator [] (size_type k) { util::gbassert(k <= _size); return _buf[k]; }
        //---------------------
        const CharT& operator [] (size_type k) const { util::gbassert(k <= _size); return _buf[k]; }
        //---------------------
        const CharT* c_str() const { return std::addressof(_buf.front()); }
        //---------------------
        void clear() { _size = 0; _buf[0] = 0; }
        //---------------------
        size_type length() const { return _size; }
        //---------------------
        size_type size() const { return _size; }
        //---------------------
        explicit operator bool() const { return _size != 0; }
        //---------------------
        CharT* begin() { return std::addressof(_buf.front()); }
        //---------------------
        CharT* end() { return std::addressof(_buf[_size]); }
        //---------------------
        const CharT* begin() const { return std::addressof(_buf.front()); }
        //---------------------
        const CharT* end() const { return std::addressof(_buf[_size]); }

        //---------------------
        template<std::size_t M>
        friend auto operator<=> (const static_string& s1, const static_string<M, CharT, Traits>& s2) //requires(std::three_way_comparable_with<const CharT*, const CharT*>)
        {
            return util::compare<CharT, Traits>(s1.c_str(), s2.c_str(), s1.size(), s2.size());
        }
        //---------------------
        template<std::size_t N>
        friend auto operator<=> (const static_string& s1, const CharT s2[N])
        {
            return util::compare<CharT, Traits>(s1.c_str(), s2, s1.size(), N);
        }
        //---------------------
        template<std::size_t N>
        friend auto operator<=> (const CharT s2[N], const static_string& s1)
        {
            return util::compare<CharT, Traits>(s2, s1.c_str(), N, s1.size());
        }
        //---------------------
        friend auto operator<=> (const static_string& s1, const CharT* s2)
        {
            return util::compare<CharT, Traits>(s1.c_str(), s2, s1.size(), std::strlen(s2));
        }
        //---------------------
        friend auto operator== (const static_string& s1, const auto& s2) { return std::is_eq(s1 <=> s2); }
        //---------------------
        friend auto operator!= (const static_string& s1, const auto& s2) { return std::is_neq(s1 <=> s2); }

    private:
        std::array<CharT, N + 1> _buf; // 0-terminated
        size_type _size;
        static_assert(N < std::numeric_limits<size_type>::max());
    };


    //-------------------------------------operator <<
    template<size_t N, class CharT>
    std::ostream& operator << (std::ostream& os, const static_string<N, CharT>& str)
    {
        std::copy(std::begin(str), std::end(str), std::ostreambuf_iterator<CharT>(os));
        return os;
    }

} // namespaces

