//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2024, Gene Bushuyev
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
#include <functional>
#include <mutex>
#include <concepts>
#include <span>
#include <compare>
#include <utility>
#include <chrono>
#include <variant>
#include <string>
#include <sstream>
#include <tuple>
#include <filesystem>
#include <cmath>
#include <format>
#include <vector>
#include <random>
#include "gberror.h"

// miscellaneous utilities

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // move_forward function
    // if supplied parameter is (qualified) lvalue, returns (qualified) lvalue reference
    // if supplied parameter is (qualified) rvalue, returns (qualified) value using move constructor
    template<class T>
    decltype(auto) move_forward(T&& t)
    {
        return static_cast<T>(std::forward<T>(t));
    }

    //-------------------------------------------------------------------------
    // creating overload set through inheritance
    // e.g. auto v = overloaded([](int i, double j){ return i+j;}, [](int, float) { return 0; })(123, 1.);
    template<class...T>
    struct overloaded : T... { using T::operator()...; };

    template<class...T>
    overloaded(T&& ...t) -> overloaded<std::remove_cvref_t<T>...>;

    //-------------------------------------------------------------------------
    template<class...T>
    struct mixin : T...
    {
        template<std::size_t I>
        using type = std::tuple_element<I, std::tuple<T...>>;

        using T::T...;
        using T::operator=...;
    };

    //-------------------------------------------------------------------------
    // compare two floating point numbers
    inline auto almost_equal(std::floating_point auto first, std::floating_point auto second, std::floating_point auto error)
    {
        return std::abs(first - second) <= error;
    }

    //-------------------------------------------------------------------------
    // compare two floating point ranges
    inline auto almost_equal(std::ranges::sized_range auto&& first, std::ranges::sized_range auto&& second, 
        std::floating_point auto error)
    {
        if (std::size(first) == std::size(second))
        {
            auto [i1, i2] = std::mismatch(std::begin(first), std::end(first), std::begin(second), std::end(second),
                [&](auto&& value1, auto&& value2) { return almost_equal(value1, value2, error); });
            return i1 == std::end(first) && i2 == std::end(second);
        }
        return false;
    }

    //-------------------------------------------------------------------------
    // hash functions
    //-------------------------------------------------------------------------
    inline auto make_hash(const char* str) { return std::hash<std::string>{}(str); }
    inline auto make_hash(const wchar_t* str) { return std::hash<std::wstring>{}(str); }
    inline auto make_hash(const auto& v) requires requires{ std::hash<std::remove_cvref_t<decltype(v)>>{}(v); }
    { return std::hash<std::remove_cvref_t<decltype(v)>>{}(v); };
    // TODO: remove after experiment
    inline auto make_hash(unsigned v) { return v; };

    // combine hashes
    inline auto make_hash(const auto& v, const auto&... ts) requires(sizeof ...(ts) != 0);
    // pair, tuple, array
    inline constexpr auto make_hash(auto&& v) requires requires { std::get<0>(v); };
    // ranges
    inline auto make_hash(const std::ranges::common_range auto& r);

    // combine hashes
    inline auto make_hash(const auto& v, const auto&... ts) requires(sizeof ...(ts) != 0)
    {
        auto seed = make_hash(v);
        ((seed ^= make_hash(ts) + 0x9e3779b9 + (seed << 6) + (seed >> 2)), ...);
        return seed;
    }
    
    // ranges
    inline auto make_hash(const std::ranges::common_range auto& r)
    {
        auto seed = make_hash(0);
        for (auto&& v : r)
            seed = make_hash(seed, v);
        return seed;
    }

    // pair, tuple, array
    inline constexpr auto make_hash(auto&& v) requires requires { std::get<0>(v); }
    {
        return std::apply([](auto&&... t) { return make_hash(t...); }, v);
    }

#if defined(clang_p1061)
    // https://gcc.godbolt.org/z/sMdqMY4Yv
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p1061r5.html
    inline constexpr auto make_hash(auto&& t) requires (std::is_class_v<std::remove_cvref_t<decltype(t)>>)
    {
        auto [...a] = std::forward<decltype(t)>(t);
        return make_hash(a...);
    }
#endif

    using make_hash_t = decltype([](auto&& ...v) { return gb::yadro::util::make_hash(std::forward<decltype(v)>(v)...); });
    
    //-------------------------------------------------------------------------
    inline auto time_stamp()
    {
        auto tstamp{ std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) };

        return (std::ostringstream{} << "[" << std::put_time(std::localtime(&tstamp), "%F %T")
            << "] [pid: " << ::_getpid() << ", tid: " << std::this_thread::get_id() << "]").str();
    }

    //-------------------------------------------------------------------------
    // DateTime conversion
    inline auto datetime_to_chrono(double datetime)
    {
        using namespace std::chrono_literals;
        auto days = unsigned(datetime);
        auto hours = unsigned((datetime - days) * 24);
        auto mins = unsigned(((datetime - days) * 24 - hours) * 60);
        auto secs = std::lround((((datetime - days) * 24 - hours) * 60 - mins) * 60);
        return std::chrono::sys_days{ 1899y / 12 / 30 } + std::chrono::days(days)
            + std::chrono::hours(hours)
            + std::chrono::minutes(mins)
            + std::chrono::seconds(secs);
    }

    //-------------------------------------------------------------------------
    // tokenize string based on specified delimiter
    //-------------------------------------------------------------------------
    template<class T>
    inline auto tokenize(const std::basic_string<T>& input, T separator)
    {
        std::vector<std::basic_string<T>> tokens;
        std::basic_istringstream<T> token_stream(input);

        for (std::string token;  std::getline(token_stream, token, separator);)
            tokens.push_back(token);

        return tokens;
    }

    //-------------------------------------------------------------------------
    // transform vector function for multiple equally sized ranges,
    // output range contains the result of transform_fn called on each element
    // of each of other ranges: output[i] = transform_fn(other[i]...)
    inline auto transform(auto&& transform_fn, std::ranges::sized_range auto& output,
        const std::ranges::sized_range auto& ... other)
    {
        static_assert(sizeof ...(other) != 0);
        gbassert(((output.size() == other.size()) && ...));

        for (auto [it, its] = std::tuple(std::begin(output), std::tuple(std::cbegin(other)...));
            it != std::end(output);
            ++it, std::apply([](auto& ... its)
                {
                    ((++its), ...);
                }, its))
        {
            *it = std::apply([&](auto&& ... its)
                {
                    return std::invoke(transform_fn, (*its)...);
                }, its);
        }
    }
    //-------------------------------------------------------------------------
    // resource locked with mutex from construction to destruction
    // serialized access to locked resource is through visit() function
    //-------------------------------------------------------------------------
    template<class T, class Mutex = std::mutex>
    struct locked_resource final
    {
        // construct with owning mutex
        template<class...Args>
        constexpr explicit locked_resource(Args&&... args)
        {
            new(&t) T(std::forward<Args>(args)...);
        }

        // construct with mutex reference
        template<class...Args, class MM = Mutex, std::enable_if_t<std::is_reference_v<MM>, int> = 0>
        constexpr explicit locked_resource(MM& m, Args&&... args) : mtx(m)
        {
            std::scoped_lock _(mtx);
            new(&t) T(std::forward<Args>(args)...);
        }

        ~locked_resource()
        {
            t.~T();
        }

        template<class Function, class...Args>
        constexpr auto visit(Function&& f, Args&&...args)
        {
            return std::scoped_lock(mtx),
                std::invoke(std::forward<Function>(f), t, std::forward<Args>(args)...);
        }

        template<class Function, class...Args>
        constexpr auto visit(Function&& f, Args&&...args) const
        {
            return std::scoped_lock(mtx),
                std::invoke(std::forward<Function>(f), t, std::forward<Args>(args)...);
        }

        constexpr explicit operator bool() const { return std::scoped_lock(mtx), (t ? true : false); }

    private:
        Mutex mtx;
        union { T t; };
    };

    inline auto locked_call(auto fun, auto& ... mtx) requires (std::invocable<decltype(fun)>)
    {
        std::scoped_lock lock(mtx ...);
        return std::invoke(fun);
    }

    //-------------------------------------------------------------------------
    // clean up temporary files at program exit
    struct tmp_file_cleaner_t final
    {
        // no public constructors
        tmp_file_cleaner_t(const tmp_file_cleaner_t&) = delete;

        // clean up files in destructor
        ~tmp_file_cleaner_t()
        {
            std::lock_guard _(_m);
            for (auto&& p : _tmpfiles)
                std::filesystem::remove(p);
        }
        
        // add file paths to be deleted at exit
        static void add(const std::filesystem::path& p)
        {
            static tmp_file_cleaner_t cleaner{ private_tag{} };
            (void)std::lock_guard(cleaner._m), cleaner._tmpfiles.push_back(p);
        }
    private:
        struct private_tag {};
        tmp_file_cleaner_t(private_tag) {}
        std::vector<std::filesystem::path> _tmpfiles;
        std::mutex _m;
    };

    //-------------------------------------------------------------------------
    template<class OnExit>
    struct raii final
    {
        raii(auto on_entry, OnExit on_exit) requires (std::invocable<decltype(on_entry)>)
            : on_exit(on_exit) {
            std::invoke(on_entry);
        }
        raii(OnExit on_exit) : on_exit(on_exit) { }
        ~raii() { std::invoke(on_exit); }
    private:
        OnExit on_exit;
    };

    template<class OnExit, class OnEntry>
    raii(OnEntry, OnExit)->raii<OnExit>;

    //-------------------------------------------------------------------------
    // retainer class exchanges value of the variable with the new_value
    // restores original value on destruction
    template<class T>
    struct retainer final
    {
        template<class U>
        retainer(T& var, U&& new_value) : _var(var), _old_value(std::move(var)) 
        {
            var = std::forward<U>(new_value);
        }
        ~retainer() { _var = std::move(_old_value); }
    private:
        T& _var;
        T _old_value;
    };

    template<class T, class U>
    retainer(T&, U&&) -> retainer<T>;

    //---------------------
    // span 3-way comparison
    template<class T1, std::size_t Extent1, class T2, std::size_t Extent2>
    auto compare(const std::span<T1, Extent1>& s1, const std::span<T2, Extent2>& s2) requires(std::three_way_comparable_with<T1, T2>)
    {
        using ordering_t = std::compare_three_way_result_t<T1, T2>;
        if (s1.size() < s2.size())
            return ordering_t::less;
        else if (s1.size() > s2.size())
            return ordering_t::greater;
        else for (std::size_t i = 0; i < s1.size(); ++i)
        {
            auto cmp = s1[i] <=> s2[i];
            if (std::is_neq(cmp))
                return cmp;
        }
        return ordering_t::equivalent;
    }

    //---------------------
    template<class CharT, class Traits>
    auto compare(const CharT* s1, const CharT* s2, std::size_t size1, std::size_t size2) {
        auto comp = Traits::compare(s1, s2, std::min(size1, size2));
        return comp < 0 ? std::weak_ordering::less
            : comp > 0 ? std::weak_ordering::greater
            : size1 < size2 ? std::weak_ordering::less
            : size1 > size2 ? std::weak_ordering::greater
            : std::weak_ordering::equivalent;
    }

    //-------------------------------------------------------------------------
    constexpr auto strings_equal(const char* s1, const char* s2) {
        return std::char_traits<char>::length(s1) ==
            std::char_traits<char>::length(s2) &&
            std::char_traits<char>::compare(
                s1, s2, std::char_traits<char>::length(s1)) == 0;
    }

    //-----------------------------------------------------------------------------------------------
    // variable parameters
    template<class T>
        requires(std::integral<T> || std::floating_point<T>)
    struct [[nodiscard]] var_t
    {
        explicit var_t(std::vector<T> il) : params(std::move(il)) {}
        var_t(std::initializer_list<T> il) : params(il) {}

        var_t(T first, T last, T increment)
        {
            params.reserve(size_t((last - first) / increment) + 1);
            for (; first <= last; first += increment)
                params.push_back(first);
        }

        auto begin() const { return params.begin(); }
        auto end() const { return params.end(); }
        auto back() const { return params.back(); }
        const auto& get_params() const { return params; }
        auto& append(const var_t& other)&
        {
            params.append_range(other);
            return *this;
        }
    private:
        std::vector<T> params;

        friend auto operator+ (var_t v1, const var_t& v2)
        {
            return v1.append(v2);
        }
    };

    //-----------------------------------------------------------------------------------------------
    // create vector of tuples, containing every combination of var_t parameters
    // example of use:
    // for(auto&& tup : wrap_in_tuple(var1, var2))
    //      std::apply([](auto&& v1, auto&& v2) { ... }, tup);
    template<class T, class ...Ts>
    inline auto wrap_in_tuple(const var_t<T>& var, const var_t<Ts>&... vars)
    {
        std::vector<std::tuple<T, Ts...>> result;
        for (auto p : var.get_params())
            if constexpr (sizeof...(Ts))
                for (auto& ts : wrap_in_tuple(vars...))
                    result.push_back(std::tuple_cat(std::tuple(p), ts));
            else
                result.push_back(std::tuple(p));
        return result;
    }

    //-----------------------------------------------------------------------------------------------
    // specialization for empty parameter list
    inline auto wrap_in_tuple()
    {
        return std::vector<std::tuple<>>{};
    }

    //-----------------------------------------------------------------------------------------------
    // window function specifies behavior outside of min/max values
    auto window_function(auto&& value, std::invocable<decltype(value)> auto&& fun,
        std::convertible_to<decltype(value)> auto&& min_value,
        std::convertible_to<decltype(value)> auto&& max_value)
    {
        return value < min_value || value > max_value ? std::invoke(std::forward<decltype(fun)>(fun), value - min_value)
            : value > max_value ? std::invoke(std::forward<decltype(fun)>(fun), max_value - value) : value;
    }
}
