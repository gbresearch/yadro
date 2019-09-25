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
#include <utility>
#include <ios>
#include <iterator>
#include <string>
#include "archive_traits.h"

namespace gbr {
    namespace archive {

        //---------------------------------------------------------------------
        // archive defined for in- and out-streams, but not for io-streams
        template<class Stream>
        class archive
        {
            Stream s;
        public:
            using stream_type = remove_cvref_t<Stream>;
            using char_type = typename stream_type::char_type;
            static_assert(is_readable_v<stream_type> && !is_writable_v<stream_type>
                || !is_readable_v<stream_type> && is_writable_v<stream_type>,
                "stream must be readable or writable, but not both");

            //-----------------------------------
            explicit archive(Stream&& s) : s(std::forward<Stream>(s))
            {
            }

            //-----------------------------------
            // read an array T
            template<class T>
            auto read(T& t, std::size_t count = 1)
            {
                static_assert(is_readable_v<stream_type>);
                static_assert(std::is_trivial_v<T>);

                s.rdbuf()->sgetn(static_cast<char_type*>(static_cast<void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
            }

            //-----------------------------------
            // write an array of T
            template<class T>
            auto write(const T& t, std::size_t count = 1)
            {
                static_assert(is_writable_v<stream_type>);
                static_assert(std::is_trivial_v<T>);

                s.rdbuf()->sputn(static_cast<const char_type*>(static_cast<const void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
            }

            //-----------------------------------
            template<class T>
            auto operator()(T&& t)
            {
                using pure_type = remove_cvref_t<T>;

                if constexpr (is_readable_v<stream_type>)
                {   // read
                    static_assert(!std::is_const_v<std::remove_reference_t<T>>);
                    static_assert(is_serializable_v<archive, pure_type>, "type isn't read-serializable");

                    if constexpr (std::is_trivial_v<pure_type>)
                        read(t);
                    else if constexpr (is_mem_serializable_v<archive, pure_type>)
                        t.serialize(*this);
                    else if constexpr (is_free_serializable_v<archive, pure_type>)
                        serialize(*this, t);
                }
                else
                {   // write
                    using const_pure_type = std::add_const_t<pure_type>;
                    using const_ref_type = std::add_lvalue_reference_t<const_pure_type>;

                    static_assert(is_serializable_v<archive, const_pure_type>
                        || is_serializable_v<archive, pure_type>, "type isn't write-serializable");

                    if constexpr (std::is_trivial_v<pure_type>)
                        write(t);
                    else if constexpr (is_mem_serializable_v<archive, const_pure_type>)
                        const_cast<const_ref_type>(t).serialize(*this);
                    else if constexpr (is_free_serializable_v<archive, const_pure_type>)
                        serialize(*this, const_cast<const_ref_type>(t));
                    else if constexpr (is_mem_serializable_v<archive, pure_type>)
                        const_cast<pure_type&>(t).serialize(*this); // symmetric non-const serialization only
                    else if constexpr (is_free_serializable_v<archive, pure_type>)
                        serialize(*this, const_cast<pure_type&>(t));
                }
            }

            //-----------------------------------
            template<class T, class... Ts>
            auto operator()(T&& t, Ts&&... ts)->std::enable_if_t < (sizeof...(Ts) > 0) >
            {
                (*this)(std::forward<T>(t));
                (*this)(std::forward<Ts>(ts)...);
            }
        };

        template<class Stream>
        archive(Stream&&)->archive<Stream>;

        //---------------------------------------------------------------------
        template<class As, class T>
        class serialize_as_t
        {
            using as_type = As;
            using value_type = T;

            value_type val;

        public:
            explicit serialize_as_t(T&& v) : val(std::forward<T>(v)) {}
            
            template<class Archive>
            auto serialize(Archive&& a)
            {
                static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
                static_assert(is_castable_v<remove_cvref_t<T>, As>);
                static_assert(is_castable_v<As, T>);

                if constexpr (is_iarchive_v<Archive>)
                {
                    As tmp;
                    a(tmp);
                    val = static_cast<remove_cvref_t<T>>(tmp);
                }
                else
                {
                    a(static_cast<As>(val));
                }
            }
        };

        template<class As, class T>
        auto serialize_as(T&& t) { return serialize_as_t<As, T>(std::forward<T>(t)); }

        //---------------------------------------------------------------------
        // variadic serialize function
        template<class Archive, class T, class... Ts>
        auto serialize(Archive&& a, T&& t, Ts&& ... ts) -> std::enable_if_t<sizeof...(Ts) != 0>
        {
            serialize(std::forward<Archive>(a), std::forward<T>(t));
            serialize(std::forward<Archive>(a), std::forward<Ts>(ts)...);
        }

        //---------------------------------------------------------------------
        // trivial types are serialized as bytes
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t)-> std::enable_if_t<std::is_trivial_v<remove_cvref_t<T>>>
        {
            a(std::forward<T>(t));
        }

        //---------------------------------------------------------------------
        // tuple/pair

        namespace detail
        {
            // helper function
            template<std::size_t N, class Archive, class T>
            auto serialize_tuple_element(Archive&& a, T&& t)
            {
                if constexpr (N < std::tuple_size_v< remove_cvref_t<T>>)
                {
                    a(std::get<N>(std::forward<T>(t)));
                    serialize_tuple_element<N + 1>(std::forward<Archive>(a), std::forward<T>(t));
                }
            }
        }

        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_tuple_v<remove_cvref_t<T>>>
        {
            detail::serialize_tuple_element<0>(std::forward<Archive>(a), std::forward<T>(t));
        }

        //---------------------------------------------------------------------
        // serialization of contiguous containers (vector, string, valarray) of trivial types
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_trivial_sequence_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");

            if constexpr (is_iarchive_v<Archive>)
            {
                static_assert(!std::is_const_v<std::remove_reference_t<T>>);

                using size_type = decltype(std::size(t));
                size_type size{ 0 };
                a(size);
                t.resize(size);

                if (size)
                    a.read(*std::begin(t), size);
            }
            else
            {
                auto size = std::size(t);
                a(size);
                if (size)
                    a.write(*std::begin(t), size);
            }
        }

        //---------------------------------------------------------------------
        // serialization of sequences of non-trivial types
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_common_sequence_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");

            if constexpr (is_resizable_v<remove_cvref_t<T>>)
            {
                if constexpr (is_iarchive_v<Archive>)
                {
                    static_assert(!std::is_const_v<std::remove_reference_t<T>>);

                    using size_type = decltype(std::size(t));
                    size_type size{ 0 };
                    a(size);
                    t.resize(size);
                }
                else
                {
                    a(std::size(t));
                }
            }

            for (auto&& v : t)
            {
                a(v);
            }
        }

        //---------------------------------------------------------------------
        // queue/stack serialization
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_queue_v<T> || is_stack_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");
            static_assert(!std::is_const_v<std::remove_reference_t<T>>);
            using value_type = typename remove_cvref_t<T>::value_type;

            if constexpr (is_iarchive_v<Archive>)
            {
                static_assert(std::is_default_constructible_v<value_type>);

                // reading clears the queue
                remove_cvref_t<T>{}.swap(t);

                using size_type = decltype(std::size(t));
                size_type size{ 0 };
                a(size);

                for (size_type i = 0; i < size; ++i)
                {
                    value_type tmp;
                    a(tmp);
                    t.push(std::move(tmp));
                }
            }
            else
            {
                if constexpr (is_queue_v<T>)
                {
                    a(std::size(t));

                    // must preserve the original queue
                    remove_cvref_t<T> tmp;

                    while (!std::empty(t))
                    {
                        a(t.front());
                        tmp.push(std::move(t.front()));
                        t.pop();
                    }

                    t.swap(tmp);
                }
                else
                {
                    static_assert(is_stack_v<T>);

                    // must preserve the original stack
                    std::vector<value_type> tmp;
                    tmp.reserve(std::size(t));
                    
                    while (!std::empty(t))
                    {
                        tmp.push_back(std::move(t.top()));
                        t.pop();
                    }

                    // serialize as reversed vector
                    std::reverse(std::begin(tmp), std::end(tmp));
                    
                    a(tmp);
                    
                    // restore the original stack
                    for (auto&& v : tmp)
                    {
                        t.push(std::move(v));
                    }
                }
            }
        }

        //---------------------------------------------------------------------
        // serialization of ordered associative containers
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_ordered_associative_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
            using key_type = typename remove_cvref_t<T>::key_type;
            using value_type = typename remove_cvref_t<T>::value_type;

            if constexpr (is_iarchive_v<Archive>)
            {
                static_assert(!std::is_const_v<std::remove_reference_t<T>>);
                t.clear();
                using size_type = decltype(std::size(t));
                size_type size{ 0 };
                a(size);

                for (size_type i = 0; i < size; ++i)
                {
                    if constexpr (std::is_same_v<key_type, value_type>)
                    {
                        key_type tmp;
                        a(tmp);
                        t.insert(std::move(tmp));
                    }
                    else
                    {
                        std::pair<typename remove_cvref_t<T>::key_type, typename remove_cvref_t<T>::mapped_type> tmp;
                        a(tmp);
                        t.insert(std::move(tmp));
                    }
                }
            }
            else
            {
                a(std::size(t));

                for (auto&& v : t)
                {
                    a(v);
                }
            }
        }

        //---------------------------------------------------------------------
        // serialization of ordered associative containers
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_unordered_associative_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
            using key_type = typename remove_cvref_t<T>::key_type;
            using value_type = typename remove_cvref_t<T>::value_type;

            if constexpr (is_iarchive_v<Archive>)
            {
                static_assert(!std::is_const_v<std::remove_reference_t<T>>);
                t.clear();

                std::size_t size{ 0 };
                a(size);
                t.reserve(size);
                
                std::size_t bucket_count{ 0 };
                a(bucket_count);
                t.rehash(bucket_count);

                for (std::size_t i = 0; i < size; ++i)
                {
                    if constexpr (std::is_same_v<key_type, value_type>)
                    {
                        key_type tmp;
                        a(tmp);
                        t.insert(std::move(tmp));
                    }
                    else
                    {
                        std::pair<typename remove_cvref_t<T>::key_type, typename remove_cvref_t<T>::mapped_type> tmp;
                        a(tmp);
                        t.insert(std::move(tmp));
                    }
                }
            }
            else
            {
                a(std::size(t));
                a(t.bucket_count());

                for (auto&& v : t)
                {
                    a(v);
                }
            }
        }

        //---------------------------------------------------------------------
        // serialization of optional
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_optional_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
            
            if constexpr (is_iarchive_v<Archive>)
            {
                auto has_value = false;
                a(serialize_as<char>(has_value));
                if (has_value)
                {
                    remove_cvref_t<decltype(t.value())> value;
                    a(value);
                    t.emplace(std::move(value));
                }
            }
            else
            {
                a(serialize_as<char>(t.has_value()));

                if (t.has_value())
                {
                    a(t.value());
                }
            }
        }

        namespace detail
        {
            struct invalid_alternative : std::runtime_error
            {
                invalid_alternative(std::size_t index)
                    : std::runtime_error("invalid alternative: " + std::to_string(index))
                {}
            };
            // variant helpers
            template<std::size_t I, class Variant, class Fn>
            auto assign_alternative(std::size_t index, Variant& v, Fn fn)
            {
                if(index >= std::variant_size_v<Variant>)
                    throw invalid_alternative(index);

                if constexpr (I < std::variant_size_v<Variant>)
                {
                    if (index == I)
                    {
                        std::variant_alternative_t<I, Variant> value;
                        fn(value);
                        v.emplace<I>(std::move(value));
                    }
                    else if (index > I)
                    {
                        assign_alternative<I + 1>(index, v, fn);
                    }
                }
            }
        }
        //---------------------------------------------------------------------
        // serialization of variant
        template<class Archive, class T>
        auto serialize(Archive&& a, T&& t) -> std::enable_if_t<is_variant_v<T>>
        {
            static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);

            if constexpr (is_iarchive_v<Archive>)
            {
                std::size_t index{};
                a(index);

                if (index != std::variant_npos)
                {
                    detail::assign_alternative<0>(index, t, [&](auto& val) { a(val); });
                }
            }
            else
            {
                a(t.index());
                std::visit([&](auto&& val) { a(val); }, t);
            }
        }
    }
}
