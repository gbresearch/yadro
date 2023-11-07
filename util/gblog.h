//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2022, Gene Bushuyev
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
#include <iostream>

// log utilities
namespace gb::yadro::util
{
    //-----------------------------------------------------------------
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
    struct log_record
    {

        log_record(auto&& ... msgs) : time_stamp(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())),
            thread_id(std::this_thread::get_id()),
            pid(::getpid())
        {
            write(std::forward<decltype(msgs)>(msgs)...);
        }

        void print(auto& output, bool verbose) const
        {
            if (verbose)
                output << "[" << std::put_time(std::localtime(&time_stamp), "%F %T") << "] [pid: " << pid << ", tid: " << thread_id
                << "]\t";
            output << _msg << "\n";
        }

        auto write(auto&& ... msgs)
        {
            std::ostringstream os;
            (os << ... << std::forward<decltype(msgs)>(msgs));
            _msg += os.str();
        }

    private:
        std::time_t time_stamp;
        std::thread::id thread_id;
        int pid;
        std::string _msg;
    };

    template<class T1, class T2>
    concept not_same_as = !std::same_as<T1, T2>;

    template<class T>
    concept c_binder = requires { {std::declval<T>().id}->std::convertible_to<int>; std::declval<T>() >> std::cout; };
    template<class T>
    concept c_not_binder = !c_binder<T>;

    //-----------------------------------------------------------------
    // thread-safe logger
    // 
    // example:
    // 
    // logger log(0_log >> std::cout >> "file.txt" >> std::err, 1_log >> "debug.log");
    // log.writeln(a,b,c,pad_to(n),...); // writes to 0_log, i.e. std::cout, file "file.txt", and std::err
    // log.writeln(1_log, "debug stream"); // writes to 1_log, i.e. file "debug.log"
    // log.add_streams(1_log >> "another.file"); // add file stream "another.file" to 1_log
    // log.add_stream("another_file.log"); // add file stream "another.file" to 0_log
    //
    // another example, create a log hierarchy:
    // inline const auto debug_log = 2_log >> "program-debug.log"; // debug logs go to only "program-debug.log" file
    // inline const auto info_log = 1_log >> "program-info.log" >> debug_log; // info logs go to "program-info.log" and debug logs
    // inline const auto default_log = 0_log >> std::cout >> "program.log" >> info_log; // default logs go to all streams
    // logger log1(default_log, debug_log, info_log); // enable all logs
    // write to specific logs
    // log.writeln(default_log, "default log");
    // log.writeln(info_log, "info log");
    // log.writeln(debug_log, "debug log");
    //-----------------------------------------------------------------
    class logger
    {
        template<class ...T> struct stream_binder;
    public:

        logger() = default;

        logger(auto&& ...binders) try
            // can't move outside, because of VC++ bug
            // error C2600: cannot define a compiler-generated special member function (must be declared in the class first)
            // https://developercommunity.visualstudio.com/t/C-compiler-bug:-error-C2600:-logger::/10509123
        {
            add_streams(std::forward<decltype(binders)>(binders)...);
        }
        catch (std::exception& e)
        {
            std::cerr << "failed to initialize logger: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "failed to initialize logger: unknown error" << std::endl;
        }

        auto& writeln(const stream_binder<>& binder, auto&& ... msg) const;

        auto& writeln(not_same_as<stream_binder<>> auto&& ... msgs) const;

        auto& add_streams(c_binder auto&& ... binders);

        auto& add_streams(c_not_binder auto&& ... streams);

        void flush();

        void set_verbose(bool v) { _verbose = v; }
        bool get_verbose() const { return _verbose; }

    private:
        bool _verbose{ false };
        std::map<std::string, std::unique_ptr<std::ofstream>> _name_map; // {file_name, stream}
        std::multimap<int, std::ostream*> _log_map; // { category, stream }
        mutable std::mutex _m;

        template<class ...T>
        struct stream_binder
        {
            const int id;
            std::tuple<T...> streams;

            auto operator>> (const std::string& name) const
            {
                return stream_binder<T..., std::string>{id, std::tuple_cat(streams, std::tuple(name))};
            }
            auto operator>> (std::ostream& os) const
            {
                return stream_binder<T..., std::ostream*>{id, std::tuple_cat(streams, std::tuple(&os))};
            }
            template<class ...U>
            auto operator>> (const stream_binder<U...>& other) const
            {
                return stream_binder<T..., U...>{id, std::tuple_cat(streams, other.streams)};
            }
        };

        friend consteval auto operator""_log(unsigned long long id);

        template<class...T>
        auto add_stream_binder(const stream_binder<T...>& binder)
        {
            auto add = [this]<std::size_t ...I>(auto && binder, std::index_sequence<I...>)
            {
                (add_stream(binder.id, std::get<I>(binder.streams)), ...);
            };
            add(binder, std::index_sequence_for<T...>{});
        }

        auto add_stream(int cat, const std::string& file_name)
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

        auto add_stream(int cat, std::ostream* os)
        {
            _log_map.insert({ cat, os });
        }
    };

    inline consteval auto operator""_log(unsigned long long id) { return logger::stream_binder<>{ (int)id }; }


    inline auto& logger::writeln(const stream_binder<>& binder, auto&& ... msg) const
    {
        log_record rec{ std::forward<decltype(msg)>(msg)... };

        std::lock_guard _(_m);
        for (auto [beg, end] = _log_map.equal_range(binder.id); beg != end; ++beg)
            rec.print(*beg->second, _verbose);

        return *this;
    }

    inline auto& logger::writeln(not_same_as<stream_binder<>> auto&& ... msgs) const
    {
        return writeln(stream_binder<>{0}, msgs...);
    }

    inline auto& logger::add_streams(c_binder auto&& ... binders)
    {
        std::lock_guard _(_m);
        (add_stream_binder(binders), ...);
        return *this;
    }

    inline auto& logger::add_streams(c_not_binder auto&& ... streams)
    {
        return add_streams((stream_binder<>{0} >> std::forward<decltype(streams)>(streams)) ...);
    }

    inline void logger::flush()
    {
        for (auto&& stream : _name_map)
            stream.second->flush();
        for (auto&& stream : _log_map)
            stream.second->flush();
    }
}
