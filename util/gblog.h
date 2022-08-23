#pragma once
#include <vector>
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

// log utilities
namespace gb::yadro::util
{
    struct logger;

    //-----------------------------------------------------------------
    struct tab
    {
        std::streampos position;
        char fill_char = ' ';

        friend auto& operator<< (std::ostream& os, const tab& p)
        {
            auto pos = os.tellp();
            if (pos < p.position)
                os << std::string(p.position - pos, p.fill_char);
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
        friend struct logger;

        std::time_t time_stamp;
        std::thread::id thread_id;
        int pid;
        std::string _msg;
    };

    //-----------------------------------------------------------------
    // usage: logger.write(a,b,c,pad_to(n),...);
    //-----------------------------------------------------------------
    struct logger
    {
        logger(auto&& ... streams)
        {
            add_streams(std::forward<decltype(streams)>(streams)...);
        }

        auto writeln(auto&& ... msg) const
        {
            log_record rec{ std::forward<decltype(msg)>(msg)... };

            if (_file_streams.empty() && _streams.empty())
                throw std::runtime_error("logger: no associated streams");

            if (!_file_streams.empty())
            {
                std::lock_guard _(_m1);
                for (auto& fs : _file_streams)
                    rec.print(*fs, _verbose);
            }
            if (!_streams.empty())
            {
                std::lock_guard _(_m2);
                for (auto& fs : _streams)
                    rec.print(*fs, _verbose);
            }
        }

        void add_streams(auto&& ... streams) try
        {
            (add_stream(std::forward<decltype(streams)>(streams)), ...);
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "failed to initialize streams" << std::endl;
        }

        void set_verbose(bool v) { _verbose = v; }
        bool get_verbose() const { return _verbose; }

    private:
        void add_stream(const std::string& file_name)
        {
            auto ofs = std::make_unique<std::ofstream>(file_name);
            if (!ofs)
                throw std::runtime_error("failed to open: " + file_name);
            _file_streams.push_back(std::move(ofs));
        }
        void add_stream(std::ostream& os)
        {
            _streams.push_back(&os);
        }
        bool _verbose{ false };
        std::vector<std::unique_ptr<std::ofstream>> _file_streams;
        std::vector<std::ostream*> _streams;
        mutable std::mutex _m1;
        mutable std::mutex _m2;
    };
}
