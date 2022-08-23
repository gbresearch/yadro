//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once

#include <type_traits>
#include <vector>
#include <string>
#include <array>
#include <valarray>
#include <variant>

#include "../util/traits.h"

namespace gb::yadro::archive
{
#if 0
    //---------------------------------------------------------------------
    // facilities available in c++20

    struct nonesuch {
        nonesuch() = delete;
        ~nonesuch() = delete;
        nonesuch(const nonesuch&) = delete;
        void operator=(const nonesuch&) = delete;
    };

    namespace detail
    {
        template<class Default, class AlwaysVoid, template<class...> class Op, class... Args>
        struct detector
        {
            using value_t = std::false_type;
            using type = Default;
        };


        template<class Default, template<class...> class Op, class... Args>
        struct detector<Default, std::void_t<Op<Args...>>, Op, Args...>
        {
            using value_t = std::true_type;
            using type = Op<Args...>;
        };
    }

    //---------------------------------------------------------------------
    // detector aliases
    template<template<class...> class Op, class... Args>
    using is_detected = typename detail::detector<nonesuch, void, Op, Args...>::value_t;

    template<template<class...> class Op, class... Args>
    using detected_t = typename detail::detector<nonesuch, void, Op, Args...>::type;

    template <class Default, template<class...> class Op, class... Args>
    using detected_or = detail::detector<Default, void, Op, Args...>;

    //---------------------------------------------------------------------
    // additional utilities
    template< template<class...> class Op, class... Args >
    constexpr bool is_detected_v = is_detected<Op, Args...>::value;

    template< class Default, template<class...> class Op, class... Args >
    using detected_or_t = typename detected_or<Default, Op, Args...>::type;

    template <class Expected, template<class...> class Op, class... Args>
    using is_detected_exact = std::is_same<Expected, detected_t<Op, Args...>>;

    template <class Expected, template<class...> class Op, class... Args>
    constexpr bool is_detected_exact_v = is_detected_exact<Expected, Op, Args...>::value;

    template <class To, template<class...> class Op, class... Args>
    using is_detected_convertible = std::is_convertible<detected_t<Op, Args...>, To>;

    template <class To, template<class...> class Op, class... Args>
    constexpr bool is_detected_convertible_v = is_detected_convertible<To, Op, Args...>::value;

    //---------------------------------------------------------------------
    template< class T >
    struct remove_cvref
    {
        using type = std::remove_cv_t<std::remove_reference_t<T>>;
    };

    template< class T >
    using remove_cvref_t = typename remove_cvref<T>::type;
#endif
    //---------------------------------------------------------------------
    // private details
    //---------------------------------------------------------------------
    namespace detail
    {

        // function tests

        template<class A, class T>
        using serialize_mem_fn = decltype(std::declval<T>().template serialize(std::declval<A>()));

        template<class A, class T>
        using serialize_fn = decltype(serialize(std::declval<A>(), std::declval<T>()));

        template<class S>
        using read_fn = decltype(std::declval<S>().read((typename std::remove_cvref_t<S>::char_type*)0, (std::streamsize)0));

        template<class S>
        using write_fn = decltype(std::declval<S>().write((const typename std::remove_cvref_t<S>::char_type*)0, (std::streamsize)0));

        template<class T>
        using sequence_fn = decltype(std::begin(std::declval<T>()), std::end(std::declval<T>()),
            std::size(std::declval<T>()));

        template<class T>
        using resize_fn = decltype(std::declval<T>().resize(0));

        template<class T>
        using tuple_fn = decltype(std::tuple_size<T>{});

        template<class T>
        using queue_fn = decltype(std::declval<T>().front(), std::empty(std::declval<T>()),
            std::declval<T>().push(std::declval<typename std::remove_cvref_t<T>::value_type>()),
            std::declval<T>().pop(), std::size(std::declval<T>()));

        template<class T>
        using stack_fn = decltype(std::declval<T>().top(), std::empty(std::declval<T>()),
            std::declval<T>().push(std::declval<typename std::remove_cvref_t<T>::value_type>()),
            std::declval<T>().pop(), std::size(std::declval<T>()));

        template<class T>
        using ordered_associative_fn = decltype(std::begin(std::declval<T>()), std::end(std::declval<T>()),
            std::size(std::declval<T>()), std::declval<T>().clear(),
            std::declval<T>().insert(std::declval<typename std::remove_cvref_t<T>::value_type>()));

        template<class T>
        using unordered_associative_fn = decltype(std::begin(std::declval<T>()), std::end(std::declval<T>()),
            std::size(std::declval<T>()), std::declval<T>().bucket_count(), std::declval<T>().rehash(),
            std::declval<T>().reserve(0));

        template<class To, class From>
        using cast_fn = decltype(static_cast<To>(std::declval<From>()));

        template<class T>
        using optional_fn = decltype(std::declval<T>().has_value(),
            std::declval<T>().emplace(std::declval<T>().value()));

        template<class T>
        using variant_fn = decltype(std::declval<T>().index(), std::declval<T>().valueless_by_exception(),
            std::declval<T>().template emplace<0>(std::get<0>(std::declval<T>())));
    }

    using util::is_detected_v;

    // has member serialize(Archive) function
    template<class A, class T>
    constexpr auto is_mem_serializable_v = is_detected_v<detail::serialize_mem_fn, A, T>
        || is_detected_v<detail::serialize_mem_fn, std::add_lvalue_reference_t<A>, T>;

    // has free serialize(Archive, T) function
    template<class A, class T>
    constexpr auto is_free_serializable_v = is_detected_v<detail::serialize_fn, A, T> // A&&, T&&
        || is_detected_v<detail::serialize_fn, std::add_lvalue_reference_t<A>, T> // A&, T&&
        || is_detected_v<detail::serialize_fn, A, std::add_lvalue_reference_t<T>> // A&&, T&
        || is_detected_v<detail::serialize_fn, std::add_lvalue_reference_t<A>, std::add_lvalue_reference_t<T>>; // A&, T&

    // serializable types are: trivial or have member serialilize or free serialize functions
    template<class A, class T>
    constexpr bool is_serializable_v = std::is_trivial_v<T> || is_mem_serializable_v<A, T> || is_free_serializable_v<A, T>;

    template<class S>
    constexpr bool is_readable_v = is_detected_v<detail::read_fn, S>;

    template<class S>
    constexpr bool is_writable_v = is_detected_v<detail::write_fn, S>;

    template<class A>
    constexpr bool is_iarchive_v = is_readable_v<typename std::remove_cvref_t<A>::stream_type>;

    template<class A>
    constexpr bool is_oarchive_v = is_writable_v<typename std::remove_cvref_t<A>::stream_type>;

    template<class A>
    constexpr bool is_archive_v = is_iarchive_v<A> || is_oarchive_v<A>;

    template<class T>
    constexpr bool is_resizable_v = is_detected_v<detail::resize_fn, T>;

    template<class To, class From>
    constexpr bool is_castable_v = is_detected_v<detail::cast_fn, To, From>;

    //-----------------------------------------------------------------------------------------
    // fixed size arrays
    template<class T>
    struct is_fixed_array : std::false_type {};

    template<class T, std::size_t N>
    struct is_fixed_array<T[N]> : std::true_type {};

    template<class T, std::size_t N>
    struct is_fixed_array<std::array<T, N>> : std::true_type {};

    template<class T>
    constexpr bool is_fixed_array_v = is_fixed_array<T>::value;

    //-----------------------------------------------------------------------------------------
    // contiguous sequences of trivial types can be serialized as a single write/read
    //-----------------------------------------------------------------------------------------
    template<class T>
    struct is_trivial_sequence : std::false_type {};

    template<class T, class A>
    struct is_trivial_sequence<std::vector<T, A>> : std::bool_constant<std::is_trivial_v<T>> {};

    template<class T, class Traits, class A>
    struct is_trivial_sequence <std::basic_string<T, Traits, A>> : std::bool_constant<std::is_trivial_v<T>> {};

    template<class T>
    struct is_trivial_sequence <std::valarray<T>> : std::bool_constant<std::is_trivial_v<T>> {};

    template<class T>
    constexpr bool is_trivial_sequence_v = is_trivial_sequence<std::remove_cvref_t<T>>::value;

    //-----------------------------------------------------------------------------------------
    // tuple/pair
    template<class T>
    constexpr bool is_tuple_v = is_detected_v<detail::tuple_fn, T> && !is_fixed_array_v<T>;

    //-----------------------------------------------------------------------------------------
    // non-trivial common sequences, resizable or fixed
    template<class T>
    constexpr bool is_common_sequence_v = is_detected_v<detail::sequence_fn, T>
        && !is_trivial_sequence_v<T> && !std::is_trivial_v<std::remove_cvref_t<T>>
        && (is_resizable_v<std::remove_cvref_t<T>> || is_fixed_array_v<std::remove_cvref_t<T>>);

    //-----------------------------------------------------------------------------------------
    // queue
    template<class T>
    constexpr bool is_queue_v = is_detected_v<detail::queue_fn, T>
        && !is_detected_v<detail::resize_fn, T>;

    //-----------------------------------------------------------------------------------------
    // stack
    template<class T>
    constexpr bool is_stack_v = is_detected_v<detail::stack_fn, T>
        && !is_detected_v<detail::resize_fn, T>;

    //-----------------------------------------------------------------------------------------
    // ordered associative containers
    template<class T>
    constexpr bool is_ordered_associative_v = is_detected_v<detail::ordered_associative_fn, T>
        && !is_detected_v<detail::resize_fn, T>;

    //-----------------------------------------------------------------------------------------
    // unordered associative containers
    template<class T>
    constexpr bool is_unordered_associative_v = is_detected_v<detail::unordered_associative_fn, T>;

    //-----------------------------------------------------------------------------------------
    // optional
    template<class T>
    constexpr bool is_optional_v = is_detected_v < detail::optional_fn, T>;

    //-----------------------------------------------------------------------------------------
    // variant
    template<class T>
    constexpr bool is_variant_v = is_detected_v < detail::variant_fn, T>;
}
