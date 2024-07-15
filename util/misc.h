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
#include <string>
#include <sstream>
#include <tuple>
#include <filesystem>
#include <cmath>
#include <format>
#include <vector>
#include <random>
#include "gberror.h"

// miscellaneous utilities (dumping ground)

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // lambda function traits, deducing return type and parameters
    template<class Fn> struct lambda_traits;

    template< class R, class G, class ... A >
    struct lambda_traits<R(G::*)(A...) const>
    {
        using Ret = R;
        using Args = std::tuple<A...>;
        using PureArgs = std::tuple<std::remove_cvref_t<A>...>;
    };

    template<class Fn>
    struct lambda_traits : lambda_traits<decltype(&Fn::operator())>
    {
    };

    template<class Fn>
    using lambda_args = typename lambda_traits<std::remove_cvref_t<Fn>>::Args;

    template<class Fn>
    using lambda_pure_args = typename lambda_traits<std::remove_cvref_t<Fn>>::PureArgs;

    template<class Fn>
    using lambda_ret = typename lambda_traits<std::remove_cvref_t<Fn>>::Ret;

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
    // mutexer<mutex> inherits from mutex and provides empty copy ctor/assignment
    // each object of the class has its own mutex
    template<class Mutex>
    struct mutexer : Mutex
    {
        mutexer() = default;
        mutexer(const mutexer&) {}
        auto& operator= (const mutexer&) { return *this; }
    };

    //-------------------------------------------------------------------------
    // resource locked with mutex from construction to destruction
    // serialized access to locked resource is through visit() function
    //-------------------------------------------------------------------------
    template<class T, class Mutex = mutexer<std::mutex>>
    struct locked_resource final
    {
        static_assert(not std::is_reference_v<T>);
        
        // construct with owning mutex
        template<class...Args>
        requires(not std::convertible_to<Args, std::add_lvalue_reference_t<Mutex>> || ...)
        constexpr explicit locked_resource(Args&&... args)
        {
            std::unique_lock _(mtx);
            new(&t) T(std::forward<Args>(args)...);
        }

        ~locked_resource()
        {
            t.~T();
        }

        template<class Function, class...Args>
        constexpr auto visit(this auto&& self, Function&& f, Args&&...args)
        {
            return (void)std::unique_lock(self.mtx),
                std::invoke(std::forward<Function>(f), std::forward<decltype(self)>(self).t, std::forward<Args>(args)...);
        }

        constexpr explicit operator bool() const { return (void)std::unique_lock(mtx), (t ? true : false); }

    private:
        mutable Mutex mtx;
        union { T t; };
    };
    
    inline auto locked_call(auto fun, auto& ... mtx) requires (std::invocable<decltype(fun)>)
    {
        return (void)std::scoped_lock(mtx ...), std::invoke(fun);
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
