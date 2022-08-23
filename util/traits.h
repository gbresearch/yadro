#pragma once

#include <type_traits>
#include <vector>
#include <string>
#include <array>
#include <valarray>
#include <variant>

namespace gb::yadro::util
{
    //---------------------------------------------------------------------
    // facilities from <experimental/type_traits>

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

    ////---------------------------------------------------------------------
    //// available in c++20
    //template< class T >
    //struct remove_cvref
    //{
    //    using type = std::remove_cv_t<std::remove_reference_t<T>>;
    //};

    //template< class T >
    //using remove_cvref_t = typename remove_cvref<T>::type;

    ////-----------------------------------------------------------------------------------------
    //// fixed size arrays
    //template<class T>
    //struct is_fixed_array : std::false_type {};

    //template<class T, std::size_t N>
    //struct is_fixed_array<T[N]> : std::true_type {};

    //template<class T, std::size_t N>
    //struct is_fixed_array<std::array<T, N>> : std::true_type {};

    //template<class T>
    //constexpr bool is_fixed_array_v = is_fixed_array<T>::value;

    ////-----------------------------------------------------------------------------------------
    //// contiguous sequences of trivial types can be serialized as a single write/read
    ////-----------------------------------------------------------------------------------------
    //template<class T>
    //struct is_trivial_sequence : std::false_type {};

    //template<class T, class A>
    //struct is_trivial_sequence<std::vector<T, A>> : std::bool_constant<std::is_trivial_v<T>> {};

    //template<class T, class Traits, class A>
    //struct is_trivial_sequence <std::basic_string<T, Traits, A>> : std::bool_constant<std::is_trivial_v<T>> {};

    //template<class T>
    //struct is_trivial_sequence <std::valarray<T>> : std::bool_constant<std::is_trivial_v<T>> {};

    //template<class T>
    //constexpr bool is_trivial_sequence_v = is_trivial_sequence<remove_cvref_t<T>>::value;

    ////-----------------------------------------------------------------------------------------
    //// tuple/pair
    //template<class T>
    //constexpr bool is_tuple_v = is_detected_v<detail::tuple_fn, T> && !is_fixed_array_v<T>;

    ////-----------------------------------------------------------------------------------------
    //// non-trivial common sequences, resizable or fixed
    //template<class T>
    //constexpr bool is_common_sequence_v = is_detected_v<detail::sequence_fn, T>
    //    && !is_trivial_sequence_v<T> && !std::is_trivial_v<remove_cvref_t<T>>
    //    && (is_resizable_v<remove_cvref_t<T>> || is_fixed_array_v<remove_cvref_t<T>>);

    ////-----------------------------------------------------------------------------------------
    //// queue
    //template<class T>
    //constexpr bool is_queue_v = is_detected_v<detail::queue_fn, T>
    //    && !is_detected_v<detail::resize_fn, T>;

    ////-----------------------------------------------------------------------------------------
    //// stack
    //template<class T>
    //constexpr bool is_stack_v = is_detected_v<detail::stack_fn, T>
    //    && !is_detected_v<detail::resize_fn, T>;

    ////-----------------------------------------------------------------------------------------
    //// ordered associative containers
    //template<class T>
    //constexpr bool is_ordered_associative_v = is_detected_v<detail::ordered_associative_fn, T>
    //    && !is_detected_v<detail::resize_fn, T>;

    ////-----------------------------------------------------------------------------------------
    //// unordered associative containers
    //template<class T>
    //constexpr bool is_unordered_associative_v = is_detected_v<detail::unordered_associative_fn, T>;

    ////-----------------------------------------------------------------------------------------
    //// optional
    //template<class T>
    //constexpr bool is_optional_v = is_detected_v < detail::optional_fn, T>;

    ////-----------------------------------------------------------------------------------------
    //// variant
    //template<class T>
    //constexpr bool is_variant_v = is_detected_v < detail::variant_fn, T>;

}
