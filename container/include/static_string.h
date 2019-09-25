//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
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

#pragma warning( disable: 4996 )

namespace gbr {
    namespace container {

        //-------------------------------------static_string
        template<size_t n, class charT = char>
        class static_string
        {
            std::array<charT, n + 1> buf; // 0-terminated
            size_t len;

            static void test(bool condition)
            {
                if (!condition)
                    throw static_string_error{};
            }
        public:
            template<class Archive>
            void serialize(Archive&& a)
            {
                std::invoke(std::forward<Archive>(a), len, buf);
            }

            struct static_string_error : std::runtime_error
            {
                static_string_error() : runtime_error("static_string_error") {}
            };

            //---------------------
            static_string() : len(0) { buf[0] = 0; }
            //---------------------
            static_string(size_t length, charT fill_char) : len(length)
            {
                test(n >= len);
                std::fill(buf, buf + len, fill_char);
                buf[len] = 0;
            }
            //---------------------
            static_string(const static_string& str) : len(str.len)
            {
                std::copy(str.buf.begin(), str.buf.end(), buf.begin());
                buf[len] = 0;
            }
            //---------------------
            explicit static_string(const std::basic_string<charT>& str) : len(str.length())
            {
                test(n >= len);
                std::copy(str.begin(), str.end(), buf.begin());
                buf[len] = 0;
            }
            //---------------------
            template<class It>
            static_string(It first, It last) : len(std::distance(first, last))
            {
                test(n >= len);
                std::copy(first, last, buf);
                buf[len] = 0;
            }
            //---------------------
            template<size_t n1>
            explicit static_string(const static_string<n1, charT>& str) : len(str.length())
            {
                test(n >= len);
                std::copy(str.begin(), str.end(), buf.begin());
                buf[len] = 0;
            }
            //---------------------
            explicit static_string(const charT* str)
            {
                test(str);
                size_t i = 0;
                for (; i < n && str[i]; ++i)
                    buf[i] = str[i];
                len = i;
                buf[len] = 0;
                test(str[i] == 0);
            }
            //---------------------
            // ----- member functions ----
            //---------------------
            void swap(static_string& str)
            {
                std::swap_ranges(buf.begin(), buf.end(), str.buf.begin());
                std::swap(len, str.len);
            }
            //---------------------
            static_string&  operator = (const static_string& str)
            {
                static_string<n, charT>(str).swap(*this);
                return *this;
            }
            //---------------------
            template<size_t n1>
            static_string&  operator += (const static_string<n1, charT>& str)
            {
                test(len + str.length() < n + 1);
                size_t i = len;
                for (; i < n && i < len + str.length(); ++i)
                    buf[i] = str[i];
                buf[i] = 0;
                len = i;
                return *this;
            }
            //---------------------
            static_string&  operator += (const charT* str)
            {
                test(str);
                size_t i = len;
                size_t j = 0;
                for (; i < n && str[j]; ++i, ++j)
                    buf[i] = str[j];
                len = i;
                buf[len] = 0;
                test(str[j] == 0);
                return *this;
            }
            //---------------------
            static_string&  operator += (const charT ch)
            {
                test(len < n);
                buf[len] = ch;
                ++len;
                buf[len] = 0;
                return *this;
            }
            //---------------------
            template<size_t n1>
            static_string  operator + (const static_string<n1, charT>& str)
            {
                return static_string<n, charT>(*this) += str;
            }
            //---------------------
            static_string  operator + (const charT* str)
            {
                return static_string<n, charT>(*this) += str;
            }
            //---------------------
            static_string  operator + (const charT ch)
            {
                return static_string<n, charT>(*this) += ch;
            }

            //---------------------
            charT&          operator [] (size_t k) { assert(k < len); return buf[k]; }
            //---------------------
            const charT&    operator [] (size_t k) const { assert(k < len); return buf[k]; }
            //---------------------
            const charT*    c_str() const { return std::addressof(buf.front()); }
            //---------------------
            size_t          length() const { return len; }
            //---------------------
            size_t          size() const { return len; }
            //---------------------
            bool            empty() const { return len == 0; }
            //---------------------
            charT*          begin() { return std::addressof(buf.front()); }
            //---------------------
            charT*          end() { return std::addressof(buf[len]); }
            //---------------------
            const charT*    begin() const { return std::addressof(buf.front()); }
            //---------------------
            const charT*    end() const { return std::addressof(buf[len]); }

        };

        //-------------------------------------operator ==
        template<size_t n, class charT>
        bool operator == (const static_string<n, charT>& str1,
            const static_string<n, charT>& str2)
        {
            return str1.length() == str2.length()
                && std::equal(str1.begin(), str1.end(), str2.begin());
        }

        //-------------------------------------operator ==
        template<size_t n, class charT>
        bool operator != (const static_string<n, charT>& str1,
            const static_string<n, charT>& str2)
        {
            return !(str1 == str2);
        }
        //-------------------------------------operator <<
        template<size_t n, class charT>
        std::ostream& operator << (std::ostream& os, const static_string<n, charT>& str)
        {
            return os << str.c_str();
        }


    }
} // namespaces

