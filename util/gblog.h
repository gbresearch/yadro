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
#include <thread>
#include <mutex>
#include <string>
#include <chrono>
#include <sstream>
#include <functional>
#include <memory>
#include <ostream>
#include <fstream>
#include <concepts>
#include <process.h> // for getpid
#include <cassert>
#include <map>
#include <set>
#include <iostream>
#include <type_traits>
#include "misc.h"

// log utilities
namespace gb::yadro::util
{
    //-----------------------------------------------------------------
    // write in tab positions filling blanks with spaces
    struct tab
    {
        std::streampos position;
        char fill_char = ' ';

        friend auto& operator<< (std::ostream& os, const tab& p)
        {
            auto pos = os.tellp();
            assert(pos != -1);
            if (pos < p.position)
                os << std::string(static_cast<std::size_t>(p.position - pos), p.fill_char);
            else
                os << p.fill_char;
            return os;
        }
    };

    //-----------------------------------------------------------------
    // thread-safe logger
    // 
    // example:
    // 
    // logger log(std::cout, "file.txt"); // logger with default category, cout and file "file.txt" streams
    // log() << a << b << pad_to(10) << c; // writes to default category
    // log.add_streams("another.file"); // add file stream "another.file" to default category
    // log() << abc ; // writes to default category
    //
    // another example, create a log hierarchy:
    // inline gb::yadro::util::logger log;
    // inline const auto debug_log = log >> R"*(tae_debug.log)*";
    // inline const auto info_log = log >> R"*(tae_info.log)*";
    // inline const auto default_log = log >> std::cout >> debug_log >> info_log >> R"*(tae.log)*";
    // inline const auto cout_log = log >> std::cout >> nullptr; // binding to nullptr removes the category, used for debugging
    // default_log << a << b << pad_to(10) << c;
    // cout_log << abc; // writes nothing
    //-----------------------------------------------------------------

    struct logger
    {
        logger() = default;

        // logger constructor, adding streams in default(0) category
        explicit logger(auto&& ...streams) try
        // can't move outside, because of VC++ bug
        // error C2600: cannot define a compiler-generated special member function (must be declared in the class first)
        // https://developercommunity.visualstudio.com/t/C-compiler-bug:-error-C2600:-logger::/10509123
        {
            (*this)(std::forward<decltype(streams)>(streams)...);
        }
        catch (std::exception& e)
        {
            std::cerr << "failed to initialize logger: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "failed to initialize logger: unknown error" << std::endl;
        }

        auto& write(auto&& ... args) const;
        auto& writeln(auto&& ... args) const;

        void remove_category(auto&& category);

        void flush();
        auto operator>> (auto&& v) { return cat_log{ counter++, *this } >> std::forward<decltype(v)>(v); }

        auto operator() () { return cat_log{ 0, *this }; } // using default binder

        auto operator() (auto&& ...streams) requires (sizeof...(streams) != 0)
        {
            return (cat_log{ counter++, *this } >> ... >> std::forward<decltype(streams)>(streams));
        }

    private:
        std::map<std::string, std::unique_ptr<std::ofstream>> _name_map; // {file_name, stream}
        std::multimap<int, std::ostream*> _log_map; // { category, stream }
        int counter{0};
        mutable std::recursive_mutex _m;

        auto& write_cat(int cat, auto&& ... msg) const
        {
            std::lock_guard _(_m);
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(cat); beg != end; ++beg)
            {
                auto output = beg->second;
                if (!outputs.contains(output)) // logs may countain the same streams, avoid repeating
                {
                    outputs.insert(output);
                    ((*output) << ... << std::forward<decltype(msg)>(msg));
                }
            }
            return *this;
        }

        auto& write_cat(int cat, std::ios_base& (*func)(std::ios_base&)) const
        {
            std::lock_guard _(_m);
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(cat); beg != end; ++beg)
            {
                auto output = beg->second;
                if (!outputs.contains(output)) // logs may countain the same streams, avoid repeating
                {
                    outputs.insert(output);
                    std::invoke(func, *output);
                }
            }
            return *this;
        }

        auto& write_cat(int cat, std::basic_ios<char, std::char_traits<char>>& (*func)(std::basic_ios<char, std::char_traits<char>>&)) const
        {
            std::lock_guard _(_m);
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(cat); beg != end; ++beg)
            {
                auto output = beg->second;
                if (!outputs.contains(output)) // logs may countain the same streams, avoid repeating
                {
                    outputs.insert(output);
                    std::invoke(func, *output);
                }
            }
            return *this;
        }

        auto& write_cat(int cat, std::basic_ostream<char, std::char_traits<char>>& (*func)(std::basic_ostream<char, std::char_traits<char>>&)) const
        {
            std::lock_guard _(_m);
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(cat); beg != end; ++beg)
            {
                auto output = beg->second;
                if (!outputs.contains(output)) // logs may countain the same streams, avoid repeating
                {
                    outputs.insert(output);
                    std::invoke(func, *output);
                }
            }
            return *this;
        }

        void add_stream(int cat, const std::string& file_name)
        {
            if (auto it = _name_map.find(file_name); it != _name_map.end())
            {
                _log_map.insert({ cat, it->second.get() });
            }
            else
            {
                auto ofs = std::make_unique<std::ofstream>(file_name);
                if (!*ofs)
                    throw std::runtime_error("failed to open log file: " + file_name);

                _log_map.insert({ cat, ofs.get() });
                _name_map[file_name] = std::move(ofs);
            }
        }

        void add_stream(int cat, std::ostream& os)
        {
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(cat); beg != end; ++beg)
                outputs.insert(beg->second);
            
            if(!outputs.contains(&os))
                _log_map.insert({ cat, &os });
        }

        void add_stream(int from_cat, int to_cat) // merge category
        {
            std::set<std::ostream*> outputs;

            for (auto [beg, end] = _log_map.equal_range(to_cat); beg != end; ++beg)
                outputs.insert(beg->second);

            for (auto [beg, end] = _log_map.equal_range(from_cat); beg != end; ++beg)
            {
                if (!outputs.contains(beg->second))
                    _log_map.insert({ to_cat, beg->second });
            }
        }

        struct cat_log // category logger
        {
            const int id;
            logger& log;

            template<class char_t>
            auto& operator>> (const char_t* name) const
            {
                log.add_stream(id, name);
                return *this;
            }
            template<class char_t, class traits_t, class alloc_t>
            auto& operator>> (const std::basic_string< char_t, traits_t, alloc_t>& name) const
            {
                log.add_stream(id, name);
                return *this;
            }
            template<class char_t, class traits_t>
            auto& operator>> (std::basic_ostream<char_t, traits_t>& os) const
            {
                log.add_stream(id, os);
                return *this;
            }

            auto& operator>> (const cat_log& other) const
            {
                log.add_stream(other.id, id);
                return *this;
            }

            auto& operator>> (std::nullptr_t) const
            {
                log.remove_category(id);
                return *this;
            }

            auto& write(auto&& ... msg) const
            {
                log.write_cat(id, std::forward<decltype(msg)>(msg)...);
                return *this;
            }

            auto& writeln(auto&& ... msg) const
            {
                std::lock_guard _(log._m);
                log.write_cat(id, std::forward<decltype(msg)>(msg)...);
                log.write_cat(0, std::endl);
                return *this;
            }

            auto& operator<< (auto&& v) const
            {
                return write(std::forward<decltype(v)>(v));
            }
            auto& operator<< (std::ios_base& (*func)(std::ios_base&)) const
            {
                log.write_cat(id, func);
                return *this;
            }
            auto& operator<< (std::basic_ios<char, std::char_traits<char>>& (*func)(std::basic_ios<char, std::char_traits<char>>&)) const
            {
                log.write_cat(id, func);
                return *this;
            }
            auto& operator<< (std::basic_ostream<char, std::char_traits<char>>& (*func)(std::basic_ostream<char, std::char_traits<char>>&)) const
            {
                log.write_cat(id, func);
                return *this;
            }
        };
    };

    inline auto& logger::write(auto&& ... args) const
    {
        return write_cat(0, std::forward<decltype(args)>(args)...);
    }

    inline auto& logger::writeln(auto&& ... args) const
    {
        std::lock_guard _(_m);
        write(std::forward<decltype(args)>(args)...);
        return write_cat(0, std::endl);
    }

    inline void logger::remove_category(auto&& category)
    {
        std::lock_guard _(_m);
        static_assert(std::is_convertible_v<decltype(category), int> || std::is_convertible_v<decltype(category), cat_log>);

        if constexpr(std::is_convertible_v<decltype(category), int>)
            _log_map.erase(category);
        else
            _log_map.erase(category.id);
    }
    
    inline void logger::flush()
    {
        std::lock_guard _(_m);
        for (auto&& stream : _name_map)
            stream.second->flush();
        for (auto&& stream : _log_map)
            stream.second->flush();
    }
}
