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
#include <variant>

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    // tuple functions
    //-------------------------------------------------------------------------
    
    // concept for tuple-like types
    template<class T>
    concept tuple_like = requires
    {
        std::get<0>(std::declval<T>());
        std::tuple_size_v<std::remove_cvref_t<T>>;
        typename std::tuple_element<0, std::remove_cvref_t<T>>;
    };

    //-------------------------------------------------------------------------
    // make a flat tuple, unwrapping tuple-like types
    constexpr auto make_flat_tuple(auto&& arg, auto&& ...args)
    {
        if constexpr (sizeof...(args) == 0)
        {   // single argument
            if constexpr (!tuple_like<decltype(arg)>)
                return std::tuple{ arg };
            else
                return std::apply([](auto&&...t) { return std::tuple_cat(make_flat_tuple(t)...); }, arg);
        }
        else
            return std::tuple_cat(make_flat_tuple(arg), make_flat_tuple(args...));
    }
    
    //-------------------------------------------------------------------------
    // convert multiple tuples to string in format {x,x,x}
    // std::ignore values are skipped
    inline auto tuple_to_string(const tuple_like auto& ... tuples)
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
    // get tuple value by run-time index, returning a variant of the same types
    constexpr auto tuple_to_variant(tuple_like auto&& t, std::size_t index)
    {
        using variant_type = decltype(std::apply([](auto&&... v) {
            return std::variant<std::remove_cvref_t<decltype(v)>...>{};
            }, t));

        constexpr auto get_n = []<auto N>(this auto && self, auto && t, std::size_t index, std::index_sequence<N>)
            -> variant_type
        {
            constexpr auto size = std::tuple_size_v<std::remove_cvref_t<decltype(t)>>;

            if constexpr (N >= size)
                throw util::exception_t{ "bad tuple index", index };
            else
            {
                if (N == index)
                    return variant_type(std::in_place_index<N>, std::get<N>(t));
                else
                    return self(t, index, std::index_sequence<N + 1>{});
            }
        };

        return get_n(std::forward<decltype(t)>(t), index, std::index_sequence<0>{});
    }

    //-------------------------------------------------------------------------
    // convert tuple to variant assigning value of the first satisfied predicate
    constexpr auto tuple_to_variant(tuple_like auto&& t, auto&& predicate)
        requires (not std::convertible_to<decltype(predicate), std::size_t>)
    {
        using variant_type = decltype(std::apply([](auto&&... v) {
            return std::variant<std::remove_cvref_t<decltype(v)>...>{};
            }, t));

        constexpr auto get_n = []<auto N>(this auto && self, auto && t, auto && predicate, std::index_sequence<N>)
            -> variant_type
        {
            constexpr auto size = std::tuple_size_v<std::remove_cvref_t<decltype(t)>>;

            if constexpr (N >= size)
                throw util::exception_t{ "bad tuple index" };
            else
            {
                if constexpr (requires{{predicate(std::get<N>(t))}->std::convertible_to<bool>; })
                    if (predicate(std::get<N>(t)))
                        return variant_type(std::in_place_index<N>, std::get<N>(t));
                return self(t, predicate, std::index_sequence<N + 1>{});
            }
        };

        return get_n(std::forward<decltype(t)>(t), predicate, std::index_sequence<0>{});
    }

    //-------------------------------------------------------------------------
    // visit tuple value by predicate
    constexpr void tuple_visit(tuple_like auto&& t, auto&& predicate, auto&& visitor_function)
    {
        auto get_n = [&]<auto N>(this auto && self, std::index_sequence<N>)
        {
            constexpr auto size = std::tuple_size_v<std::remove_cvref_t<decltype(t)>>;

            if constexpr (N >= size)
                throw util::exception_t{ "bad tuple index" };
            else
            {
                if constexpr (requires{{predicate(std::get<N>(t))}->std::convertible_to<bool>; }
                    && requires{visitor_function(std::get<N>(t)); })
                    if (predicate(std::get<N>(t)))
                    {
                        visitor_function(std::get<N>(t));
                        return;
                    }
                self(std::index_sequence<N + 1>{});
            }
        };

        get_n(std::index_sequence<0>{});
    }

    //-------------------------------------------------------------------------
    // transformation of variant
    struct void_type {};
    struct wrong_arg_type {};

    namespace detail
    {
        template<class Fn, class... Args>
        constexpr auto invoke_fn(Fn fn, Args&&...args)
        {
            if constexpr (not std::is_invocable_v<Fn, Args...>)
                return wrong_arg_type{};
            else if constexpr (std::is_void_v<std::invoke_result_t<Fn, Args...>>)
                return std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...),
                void_type{};
            else
                return std::invoke(fn, std::forward<Args>(args)...);
        }

        template<class Fn, class... Args>
        using ret_t = decltype(invoke_fn(std::declval<Fn>(), std::declval<Args>()...));

        template<auto N, class Fn, class...T>
        auto transform_helper(Fn fn, const std::variant<T...>& v)
            -> std::variant<ret_t<Fn, T>...>
        {
            using variant_type = std::variant<ret_t<Fn, T>...>;

            if constexpr (N == sizeof...(T))
                throw util::exception_t{ "transform variant error", N };
            else if (N == v.index())
                return variant_type(std::in_place_index<N>, invoke_fn(fn, std::get<N>(v)));
            else
                return transform_helper<N + 1>(fn, v);
        }
    }

    //-------------------------------------------------------------------------
    // transform variant to another variant applying function to vaiant argument
    // discussion: https://stackoverflow.com/questions/78716234/how-to-transform-stdvariant-by-applying-specified-function
    template<class Fn, class...T>
    auto transform(Fn fn, const std::variant<T...>& v)
    {
        return detail::transform_helper<0>(fn, v);
    }

    //-------------------------------------------------------------------------
    template<std::size_t ...I>
    concept is_sorted_idx = std::ranges::is_sorted(std::array<std::size_t, sizeof...(I)>{I...});

    template<std::size_t N, std::size_t...I>
    consteval auto offset_sequence(std::index_sequence<I...>) { return std::index_sequence<(N + I)...>{}; }

    template<std::size_t ...I, std::size_t ...J>
    consteval auto make_sequence(std::index_sequence<I...>, std::index_sequence<J...>)
    {
        return std::tuple{ offset_sequence<I>(std::make_index_sequence<(J - I)>{})... };
    }

    template<std::size_t Last, std::size_t First, std::size_t ...I>
    requires(is_sorted_idx<First, I...>)
    consteval auto make_sequence(std::index_sequence<First, I...>)
    {
        return make_sequence(std::index_sequence<First, I...>{}, std::index_sequence<I..., Last>{});
    }

    // MSVC 19.latest compiler fails with consteval
    template<size_t...I>
    constexpr auto get_from_sequence(auto&& t, std::index_sequence<I...>)
    {
        return std::tuple{ std::get<I>(t)... };
    }

    //-------------------------------------------------------------------------
    // split tuple by indexes (must be sorted)
    // returns a tuple of multiple tuples cut by supplied indexes, counting from 0
    // for example: tuple_split<2,3,5>(tuple{1,2,3,4,5,6,7}) --> {{1,2},{3},{4,5},{6,7}}
    // MSVC 19.latest compiler fails with consteval

    template<size_t...I>
    requires(is_sorted_idx<I...>)
    constexpr auto tuple_split(tuple_like auto&& t)
    {
        constexpr auto size = std::tuple_size_v<std::remove_cvref_t<decltype(t)>>;
        static_assert(((I > 0) && ...));
        static_assert(((I < size) && ...));
        return std::apply([&](auto&&...seq)
            {
                return std::tuple{ get_from_sequence(std::forward<decltype(t)>(t), seq)... };
            }, make_sequence<size>(std::index_sequence<0, I...>{}));
    }
    
    //-------------------------------------------------------------------------
    // making a sub-tuple from tuple-like type in index range [From, To)

    template<std::size_t From, std::size_t To>
    constexpr auto subtuple(tuple_like auto&& t)
    {
        using tuple_type = std::remove_cvref_t<decltype(t)>;
        static_assert(From >= 0 && From < To && To <= std::tuple_size_v<tuple_type>);
        return get_from_sequence(std::forward<decltype(t)>(t),
            offset_sequence<From>(std::make_index_sequence<(To - From)>{}));
    }

    //-------------------------------------------------------------------------
    // making a sub-tuple from tuple-like type in index range [From, Tuple-Size)
    template<std::size_t From>
    constexpr auto subtuple(tuple_like auto&& t)
    {
        using tuple_type = std::remove_cvref_t<decltype(t)>;
        return subtuple<From, std::tuple_size_v<tuple_type>>(std::forward<decltype(t)>(t));
    }

    //-------------------------------------------------------------------------
    // call specified functions for each element of the tuple/pair/array/subrange
    // returning a tuple of return values from each function call
    // if function returns void then std::ignore will be used in returned tuple
    // std::ignore can be used instead of a function in order to ignore the value
    // note: function arguments evaluation order is unspecified and so are side effects
    //-------------------------------------------------------------------------
    constexpr auto tuple_foreach(tuple_like auto&& tup, auto&&... fn)
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
    constexpr auto tuple_select(tuple_like auto&& tup)
    {
        return std::tuple(std::get<Indexes>(tup)...);
    }

    //-------------------------------------------------------------------------
    // create a new tuple with all std::ignore values removed
    constexpr auto tuple_remove_ignored(tuple_like auto&& tup)
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
    constexpr auto tuple_transform(auto&& transform_fn, tuple_like auto&& t, tuple_like auto&& ...ts)
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
    constexpr auto tuple_transform_reduce(auto&& transform_fn, auto&& reduce_fn, tuple_like auto&& t)
    {
        return std::apply(std::forward<decltype(reduce_fn)>(reduce_fn), tuple_transform(
            std::forward<decltype(transform_fn)>(transform_fn), std::forward<decltype(t)>(t)));
    }

    //-------------------------------------------------------------------------
    // returns min value of all elements of the specified tuples (must be comparable)
    constexpr auto tuple_min(tuple_like auto&& ...t)
    {
        return std::min({ std::apply([](auto&& ...val) { return std::min({ val... }); }, t)... });
    }

    //-------------------------------------------------------------------------
    // returns max value of all elements of the specified tuples (must be comparable)
    constexpr auto tuple_max(tuple_like auto&& ...t)
    {
        return std::max({ std::apply([](auto&& ...val) { return std::max({ val... }); }, t)... });
    }

#if defined(clang_p1061)
    //-------------------------------------------------------------------------
    // return member count of a class
    constexpr auto class_member_count(auto&& t)
    {
        auto&& [...x] = std::forward<decltype(t)>(t);
        return sizeof...(x);
    }
    
    //-------------------------------------------------------------------------
    // create a tuple from the members of class
    constexpr auto class_to_tuple(auto&& t)
    {
        auto&& [...x] = std::forward<decltype(t)>(t);
        return std::tuple{ std::forward<decltype(x)>(x)...};
    }
#endif

    //-------------------------------------------------------------------------
    namespace detail
    {
        template<class T> concept aggregate_type = std::is_aggregate_v<T>;
        struct filler_t
        {
            template<class T> requires(not aggregate_type<T>) constexpr operator T& ();
            template<class T> requires(not aggregate_type<T>) constexpr operator T && ();
        };
    }

    //-------------------------------------------------------------------------
    // return member count of an aggregate
    template<detail::aggregate_type T>
    constexpr auto aggregate_member_count(auto&& ...filler)
    {
        if constexpr (requires{ T{ filler... }; })
            return aggregate_member_count<T>(detail::filler_t{}, filler...);
        else
        {
            static_assert(sizeof...(filler), "unsupported type");
            return sizeof...(filler) - 1;
        }
    }

    //-------------------------------------------------------------------------
    // create a tuple from aggregate
    template<detail::aggregate_type T, auto N = aggregate_member_count<T>()>
    constexpr auto aggregate_to_tuple(T&& t)
    {
        static_assert(N > 0 && N < 11);

        if constexpr(N == 1)
        {
            auto&& [x] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x)>(x));
        }
        else if constexpr (N == 2)
        {
            auto&& [x1, x2] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2));
        }
        else if constexpr (N == 3)
        {
            auto&& [x1, x2, x3] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3));
        }
        else if constexpr (N == 4)
        {
            auto&& [x1, x2, x3, x4] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4));
        }
        else if constexpr (N == 5)
        {
            auto&& [x1, x2, x3, x4, x5] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4), 
                std::forward<decltype(x5)>(x5));
        }
        else if constexpr (N == 6)
        {
            auto&& [x1, x2, x3, x4, x5, x6] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4),
                std::forward<decltype(x5)>(x5), std::forward<decltype(x6)>(x6));
        }
        else if constexpr (N == 7)
        {
            auto&& [x1, x2, x3, x4, x5, x6, x7] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4),
                std::forward<decltype(x5)>(x5), std::forward<decltype(x6)>(x6), std::forward<decltype(x7)>(x7));
        }
        else if constexpr (N == 8)
        {
            auto&& [x1, x2, x3, x4, x5, x6, x7, x8] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4),
                std::forward<decltype(x5)>(x5), std::forward<decltype(x6)>(x6), std::forward<decltype(x7)>(x7), std::forward<decltype(x8)>(x8));
        }
        else if constexpr (N == 9)
        {
            auto&& [x1, x2, x3, x4, x5, x6, x7, x8, x9] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4),
                std::forward<decltype(x5)>(x5), std::forward<decltype(x6)>(x6), std::forward<decltype(x7)>(x7), std::forward<decltype(x8)>(x8),
                std::forward<decltype(x9)>(x9));
        }
        else if constexpr (N == 10)
        {
            auto&& [x1, x2, x3, x4, x5, x6, x7, x8, x9, x10] = std::forward<T>(t);
            return std::tuple(std::forward<decltype(x1)>(x1), std::forward<decltype(x2)>(x2), std::forward<decltype(x3)>(x3), std::forward<decltype(x4)>(x4),
                std::forward<decltype(x5)>(x5), std::forward<decltype(x6)>(x6), std::forward<decltype(x7)>(x7), std::forward<decltype(x8)>(x8),
                std::forward<decltype(x9)>(x9), std::forward<decltype(x10)>(x10));
        }
    }
}
