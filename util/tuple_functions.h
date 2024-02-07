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
#include <tuple>
#include <concepts>
#include <sstream>
#include <type_traits>
#include <functional>
#include <utility>

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // tuple functions
    //-------------------------------------------------------------------------

    //-------------------------------------------------------------------------
    // conver multile tuples to string in format {x,x,x}
    // std::ignore values are skipped
    auto tuple_to_string(const auto& ... tuples)
    {
        std::ostringstream os;
        auto write_one = [&](const auto& t)
            {
                auto write_value = [&](const auto& v)
                    {
                        if constexpr (!std::is_same_v< std::remove_cvref_t<decltype(v)>,
                            std::remove_cvref_t<decltype(std::ignore)>>)
                            os << v;
                    };

                std::apply([&](const auto& v1, const auto& ...v) mutable
                    {
                        os << '{';
                        write_value(v1);
                        ((os << ',', write_value(v)), ...);
                        os << '}';
                    }, t);
            };

        if (sizeof...(tuples) != 0)
            (write_one(tuples), ...);

        return os.str();
    }

    //-------------------------------------------------------------------------
    // split tuple by index I
    // returns a tuple of two tuples, 
    // the first tuple contains [0, I - 1] values from the original tuple
    // the second tuple contains [I, N - I] values from the original tuple
    template<std::size_t I, class...T>
    inline auto tuple_split(const std::tuple<T...>& t)
    {
        auto make_tuple_from = []<std::size_t First, std::size_t ...Index>(auto && t, std::index_sequence<First>,
            std::index_sequence<Index...>)
        {
            return std::tuple(std::get<First + Index>(t)...);
        };

        if constexpr (I == 0)
            return std::tuple(std::tuple{}, t);
        else if constexpr (I >= sizeof ...(T))
            return std::tuple(t, std::tuple{});
        else
            return std::tuple(make_tuple_from(t, std::index_sequence<0>{}, std::make_index_sequence<I>{}),
                make_tuple_from(t, std::index_sequence<I>{}, std::make_index_sequence<sizeof...(T) - I>{}));
    }

    //-------------------------------------------------------------------------
    // call specified functions for each element of the tuple/pair/array/subrange
    // returning a tuple of return values from each function call
    // if function returns void then std::ignore will be used in returned tuple
    // std::ignore can be used instead of a function in order to ignore the value
    // note: function arguments evaluation order is unspecified and so are side effects
    //-------------------------------------------------------------------------
    inline auto tuple_foreach(auto&& tup, auto&&... fn)
        requires(std::tuple_size<std::remove_cvref_t<decltype(tup)>>::value == sizeof ...(fn))
    {
        auto call = [](auto&& fn, auto&& val)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(fn)>,
                    std::remove_cvref_t<decltype(std::ignore)>>)
                    return std::ignore;
                else if constexpr (std::is_same_v<std::invoke_result_t<decltype(fn), decltype(val)>, void>)
                {
                    std::invoke(std::forward<decltype(fn)>(fn), std::forward<decltype(val)>(val));
                    return std::ignore;
                }
                else
                    return std::invoke(std::forward<decltype(fn)>(fn), std::forward<decltype(val)>(val));
            };

        return std::apply([&](auto&&...args)
            {
                return std::tuple(call(std::forward<decltype(fn)>(fn), std::forward<decltype(args)>(args))...);
            }, std::forward<decltype(tup)>(tup));
    }

    //-------------------------------------------------------------------------
    // create a new tuple with the values from tuple corresponding to indexes
    template<std::size_t... Indexes>
    inline auto tuple_select(auto&& tup)
    {
        return std::tuple(std::get<Indexes>(tup)...);
    }

    //-------------------------------------------------------------------------
    // create a new tuple with all std::ignore values removed
    inline auto tuple_remove_ignored(auto&& tup)
    {
        auto make_t = [](auto&& v)
            {
                if constexpr (std::is_same_v< std::remove_cvref_t<decltype(v)>,
                    std::remove_cvref_t<decltype(std::ignore)>>)
                    return std::tuple<>{};
                else
                    return std::tuple{ std::forward<decltype(v)>(v) };
            };
        return std::apply([&](auto&& ...v)
            {
                return std::tuple_cat(make_t(std::forward<decltype(v)>(v))...);
            }, std::forward<decltype(tup)>(tup));
    }

    //-------------------------------------------------------------------------
    // transform multiple tuples to another tuple where transform_fn is called on each element of the tuples in lockstep
    // requires all tuples to be the same size
    auto tuple_transform(auto&& transform_fn, auto&& t, auto&& ...ts)
        requires ((sizeof ...(ts) == 0 ||
        ((std::tuple_size<std::remove_cvref_t<decltype(t)>>{} == std::tuple_size<std::remove_cvref_t<decltype(ts)>>{})
        && ...)) && std::invocable<decltype(transform_fn), decltype(std::get<0>(t)), decltype(std::get<0>(ts))...>)
    {
        auto get_n = [&]<std::size_t N>(std::index_sequence<N>)
        {
            return transform_fn(std::get<N>(std::forward<decltype(t)>(t)), std::get<N>(std::forward<decltype(ts)>(ts))...);
        };

        auto get_tup = [&]<std::size_t ...N>(std::index_sequence<N...>)
        {
            return std::tuple(get_n(std::index_sequence<N>{})...);
        };
        
        constexpr auto size = std::tuple_size<std::remove_cvref_t<decltype(t)>>{};

        return get_tup(std::make_index_sequence<size>{});
    }

    //-------------------------------------------------------------------------
    // apply reduce_fn to a tuple which is transformed using transfrom_fn
    inline auto tuple_transform_reduce(auto&& transform_fn, auto&& reduce_fn, auto&& t) 
        requires requires { std::get<0>(t); }
    {
        return std::apply(std::forward<decltype(reduce_fn)>(reduce_fn), tuple_transform(
            std::forward<decltype(transform_fn)>(transform_fn), std::forward<decltype(t)>(t)));
    }

    //-------------------------------------------------------------------------
    // returns min value of all elements of the specified tuples (must be comparable)
    inline auto tuple_min(auto&& ...t)
    {
        return std::min({ std::apply([](auto&& ...val) { return std::min({ val... }); }, t)... });
    }

    //-------------------------------------------------------------------------
    // returns max value of all elements of the specified tuples (must be comparable)
    inline auto tuple_max(auto&& ...t)
    {
        return std::max({ std::apply([](auto&& ...val) { return std::max({ val... }); }, t)... });
    }
}
