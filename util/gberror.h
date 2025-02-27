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

#include <exception>
#include <stdexcept>
#include <concepts>
#include <functional>
#include <string>
#include <sstream>
#include <source_location>
#include <stacktrace>

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // to_string function to convert multiple parameters to a string
    const auto to_string = [](auto&&... args)
    {
        std::stringstream ss;
        if constexpr (sizeof ...(args) != 0) // avoid -Werror=unused-value
            (ss << ... << args);
        return ss.str();
    };

    //-------------------------------------------------------------------------
    // to_wstring function to convert multiple parameters to UTF-16 string
    const auto to_wstring = [](auto&&... args)
        {
            std::wstringstream ss;
            if constexpr (sizeof ...(args) != 0) // avoid -Werror=unused-value
                (ss << ... << args);
            return ss.str();
        };

    //-------------------------------------------------------------------------
    // exception class with optional payload
    template<class Data = void>
    struct exception_t;

    // exception class specialization w/o payload
    template<>
    struct exception_t<void> : std::exception
    {
        exception_t(std::string error_str,
            const std::source_location& loc = std::source_location::current(),
            std::stacktrace trace = std::stacktrace::current())
            : _error_str{ std::move(error_str) },
            _loc{ loc }, _trace{ trace }
        {}

        const char* what() const noexcept { return _error_str.c_str(); }
        auto&& what_str() const noexcept { return _error_str; }
        auto&& location() const noexcept { return _loc; }
        auto location_str(bool include_function_name = false) const {
            return include_function_name ? to_string(_loc.file_name(), '(',
                _loc.line(), ":", _loc.column(), ") \"", _loc.function_name(), "\"")
                : to_string(_loc.file_name(), '(',
                    _loc.line(), ":", _loc.column(), ")");
        }
        auto&& stacktrace() const noexcept { return _trace; }
        auto stacktrace_str() const { return std::to_string(_trace); }

        auto message(bool include_function_name = false, bool include_stacktrace = false) const
        {
            return include_stacktrace ? _error_str + "\n" + location_str(include_function_name) + "\n" + stacktrace_str() + "\n"
                : _error_str + "\n" + location_str(include_function_name) + "\n";
        }

        void serialize(this auto&& self, auto&& archive)
        {
            archive(self._error_str); // only error string is serialized
        }

    private:
        std::string _error_str;
        std::source_location _loc;
        std::stacktrace _trace;
    };

    // exception class specialization w/payload
    template<class Data>
    struct exception_t : exception_t<void>
    {
        exception_t(std::string error_str, Data data,
            const std::source_location& loc = std::source_location::current(),
            std::stacktrace trace = std::stacktrace::current())
            : exception_t<void>(std::move(error_str), loc, trace),
            _data{ std::move(data) }
        {}

        auto&& data(this auto&& self) { return std::forward<decltype(self)>(self)._data; }

        void serialize(this auto&& self, auto&& archive)
        {
            self.exception_t<void>::serialize(archive);
            archive(self._data);
        }

    private:
        Data _data;
    };

    exception_t(std::string error_str)->exception_t<void>;

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

    //-------------------------------------------------------------------------
    // predefined error types
    //-------------------------------------------------------------------------
    using failed_assertion = error_t<0, std::logic_error>;
    using unreachable_error = error_t<1, std::logic_error>;
    using generic_error = error_t<1000>;

    //-------------------------------------------------------------------------
    template<class ErrorType = generic_error>
    [[noreturn]] inline void throw_error(const std::string& msg = "failed condition", std::source_location location = std::source_location::current())
    {
        throw ErrorType(msg, " (", location.file_name(), ':', location.line(), ')');
    }

    //-------------------------------------------------------------------------
    template<class ErrorType = generic_error>
    inline void gbassert(const auto& cond, const std::string& msg, std::source_location location = std::source_location::current())
        requires(std::invocable<decltype(cond)> || std::convertible_to<decltype(!cond), bool>)
    {
        if constexpr (std::invocable<decltype(cond)>)
        {
            if (!std::invoke(cond))
                throw ErrorType(msg, " (", location.file_name(), ':', location.line(), ')');
        }
        else
        {
            if (!cond)
                throw ErrorType(msg, " (", location.file_name(), ':', location.line(), ')');
        }
    }

    //-------------------------------------------------------------------------
    template<class ErrorType = failed_assertion>
    inline void gbassert(const auto& cond, std::source_location location = std::source_location::current())
        requires(std::invocable<decltype(cond)> || std::convertible_to<decltype(!cond), bool>)
    {
        gbassert<ErrorType>(std::forward<decltype(cond)>(cond), "assertion failed", location);
    }

    //-------------------------------------------------------------------------
    template<class ExceptionType>
    inline void must_throw(auto&& fun, std::source_location location = std::source_location::current())
    {
        auto thrown = false;
        try { std::invoke(fun); }
        catch (const ExceptionType&) { thrown = true; }
        catch (...) {}
        gbassert(thrown, location);
    }
    
    //-------------------------------------------------------------------------
    inline void must_throw(auto&& fun, std::source_location location = std::source_location::current())
    {
        auto thrown = false;
        try { std::invoke(fun); }
        catch (...) { thrown = true; }
        gbassert(thrown, location);
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
