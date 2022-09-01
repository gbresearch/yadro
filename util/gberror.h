#pragma once

#include <exception>
#include <stdexcept>
#include <concepts>
#include <functional>
#include <string>
#include <source_location>
#include "misc.h"

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // a generic error type
    template<unsigned ErrNo, class ErrorBase = std::runtime_error>
    struct error_t : public ErrorBase
    {
        template <class... Args>
        explicit error_t(Args&&... args)
            : ErrorBase(to_string("[E", ErrNo, "] ", std::forward<Args>(args)...))
        {
        }
    };

    using failed_assertion = error_t<0, std::logic_error>;
    using unreachable_error = error_t<1, std::logic_error>;
    using generic_error = error_t<1000>;

    void gbassert(auto v, const std::string& msg = "", std::source_location location = std::source_location::current())
        requires(std::invocable<decltype(v)> || std::convertible_to<decltype(v), bool>)
    {
        if constexpr (std::invocable<decltype(v)>)
        {
            if (!std::invoke(v))
                throw failed_assertion("assertion failed (", location.file_name(), ':', location.line(), ')');
        }
        else
        {
            if (!v)
                throw failed_assertion("assertion failed (", location.file_name(), ':', location.line(), ')');
        }
    }

    //-------------------------------------------------------------------------
    // logically unreachable code, throw exception if executed
    [[noreturn]] inline void unreachable(std::source_location location = std::source_location::current())
    {
        throw unreachable_error("Unreachable code (", location.file_name(), ':', location.line(), ')');
    };


#ifdef POSIX
    template<std::size_t N>
    struct stack_snapshot
    {
        stack_snapshot() = default;

        stack_snapshot(const stack_snapshot& other) : _frame_count(other._frame_count)
        {
            std::copy_n(other._callstack, _frame_count, _callstack);
        }

        stack_snapshot& capture()
        {
            _frame_count = backtrace(_callstack, N);
            return *this;
        }

        template<class String>
        auto to_string(int skip = 1) const
        {
            String result;

            if (_frame_count <= skip)
                return result;

            auto symbols = backtrace_symbols(_callstack, _frame_count);

            for (int i = skip; i < _frame_count; i++)
            {
                result += "[";
                std::array<char, 10> index;

                if (auto [ptr, ec] = std::to_chars(index.data(), index.data() + index.size(), i - skip);
                    ec == std::errc())
                {
                    result += String(index.data(), ptr);
                }
                result += "]: ";

                Dl_info info;
                if (dladdr(_callstack[i], &info) && info.dli_sname)
                {
                    if (info.dli_sname[0] == '_')
                    {
                        int status;
                        auto demangled = abi::__cxa_demangle(info.dli_sname, NULL, 0, &status);

                        result += status == 0 ? demangled :
                            info.dli_sname == 0 ? symbols[i] : info.dli_sname;

                        free(demangled);
                    }
                    else
                        result += info.dli_sname == 0 ? symbols[i] : info.dli_sname;
                }
                else
                {
                    result += symbols[i];
                }
                result += "\n";
            }

            free(symbols);
            return result;
        }

        auto operator== (const stack_snapshot& other) const
        {
            return _frame_count == other._frame_count
                && std::equal(_callstack, _callstack + _frame_count, other._callstack);
        }

        auto is_truncated() const { return _frame_count == N; }
        auto is_empty() const { return _frame_count == 0; }
        auto get_frame_count() const { return _frame_count; }

        std::size_t hash() const
        {
            std::size_t h = _frame_count;
            for (auto i = 0; i < _frame_count; ++i)
                h ^= std::size_t(_callstack[i]);
            return h;
        }

        struct hash_t
        {
            bool operator()(const stack_snapshot& s) const { return s.hash(); }
        };

    private:
        void* _callstack[N]{};
        int _frame_count{};
    };

    //-------------------------------------------------------------------------
    inline auto stack_trace() { return stack_snapshot<20>{}.capture().to_string<std::string>(); }
#endif
}
