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
#include <fstream>
#include <sstream>
#include <iostream>
#include <cassert>
#include <array>
#include <vector>
#include <string>
#include <queue>
#include <stack>
#include <tuple>
#include <optional>
#include <map>
#include <unordered_map>
#include <span>

#include "archive_traits.h"

namespace gb::yadro::archive
{
    //---------------------------------------------------------------------
    enum class archive_format_t { binary, text, custom };
    //---------------------------------------------------------------------
    // archive defined for in- and out-streams, but not for io-streams
    template<class Stream, archive_format_t fmt = archive_format_t::custom>
    class archive
    {
        Stream s;
    public:
        using stream_type = std::remove_cvref_t<Stream>;
        using char_type = typename stream_type::char_type;
        static_assert(is_readable_v<stream_type> && !is_writable_v<stream_type>
            || !is_readable_v<stream_type> && is_writable_v<stream_type>,
            "stream must be readable or writable, but not both");

        //-----------------------------------
        explicit archive(auto&& ...args) : s(std::forward<decltype(args)>(args)...)
        {
        }

        //-----------------------------------
        auto& get_stream() { return s; }

        //-----------------------------------
        // read an array T
        template<class T>
        auto read(T& t, std::size_t count = 1)
        {
            static_assert(is_readable_v<stream_type>);
            static_assert(std::is_trivial_v<T>);
            if constexpr (fmt == archive_format_t::binary)
                s.rdbuf()->sgetn(static_cast<char_type*>(static_cast<void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
            else if constexpr (fmt == archive_format_t::text)
            {
                for (size_t i = 0; i < count; ++i)
                {   // TODO: spaces are skipped
                    s >> std::addressof(t)[i];
                }
            }
            else if constexpr (fmt == archive_format_t::custom)
                s.read(static_cast<char_type*>(static_cast<void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
        }

        //-----------------------------------
        // write an array of T
        template<class T>
        auto write(const T& t, std::size_t count = 1)
        {
            static_assert(is_writable_v<stream_type>);
            static_assert(std::is_trivial_v<T>);

            if constexpr (fmt == archive_format_t::binary)
                s.rdbuf()->sputn(static_cast<const char_type*>(static_cast<const void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
            else if constexpr (fmt == archive_format_t::text)
            {
                for (size_t i = 0; i < count; ++i)
                    s << std::addressof(t)[i];
                s << '\n';
            }
            else if constexpr (fmt == archive_format_t::custom)
                s.write(static_cast<const char_type*>(static_cast<const void*>(std::addressof(t))),
                    count * (sizeof(T) / sizeof(char_type)));
        }

        //-----------------------------------
        template<class T>
        void operator()(T&& t)
        {
            using pure_type = std::remove_cvref_t<T>;
            static_assert(!std::is_pointer_v<pure_type>, "pointers cannot be serialized");
            static_assert(!std::is_array_v<pure_type>); // TODO: process arrays of trivial types
            
            if constexpr (is_readable_v<stream_type>)
            {   // read
                static_assert(!std::is_const_v<std::remove_reference_t<T>>);
                static_assert(is_serializable_v<archive, pure_type>, "type isn't read-serializable");
                //  non-const r-values are treated as l-values
                if constexpr (is_mem_serializable_v<archive, pure_type>)
                    t.serialize(*this);
                else if constexpr (is_free_serializable_v<archive, pure_type>)
                    serialize(*this, t);
                else if constexpr (std::is_trivial_v<pure_type>)
                    read(t);
            }
            else
            {   // write
                using const_pure_type = std::add_const_t<pure_type>;
                using const_ref_type = std::add_lvalue_reference_t<const_pure_type>;

                static_assert(is_serializable_v<archive, const_pure_type>
                    || is_serializable_v<archive, pure_type>, "type isn't write-serializable");

                if constexpr (is_mem_serializable_v<archive, const_pure_type>)
                    const_cast<const_ref_type>(t).serialize(*this);
                else if constexpr (is_free_serializable_v<archive, const_pure_type>)
                    serialize(*this, const_cast<const_ref_type>(t));
                else if constexpr (is_mem_serializable_v<archive, pure_type>)
                    const_cast<pure_type&>(t).serialize(*this); // symmetric non-const serialization only
                else if constexpr (is_free_serializable_v<archive, pure_type>)
                    serialize(*this, const_cast<pure_type&>(t));
                else if constexpr (std::is_trivial_v<pure_type>)
                    write(t);
            }
        }

        //-----------------------------------
        template<class... Ts>
        void operator()(Ts&&... ts) requires(sizeof...(Ts) > 1)
        {
            ((*this)(std::forward<Ts>(ts)), ...);
        }
    };

    template<class Stream, archive_format_t Fmt>
    archive(Stream&&)->archive<Stream, Fmt>;
    //---------------------------------------------------------------------
    struct imem_stream;

    struct omem_stream
    {
        using char_type = char;
        explicit omem_stream(auto&& ... args) : _buf(std::forward<decltype(args)>(args)...) {}

        void write(const char_type* c, std::streamsize size)
        {
            auto prev_size = _buf.size();
            _buf.resize(prev_size + size);
            std::copy_n(c, size, _buf.begin() + prev_size);
        }
    private:
        std::vector<char_type> _buf;
        friend struct imem_stream;
    };

    struct imem_stream
    {
        using char_type = char;
        explicit imem_stream(omem_stream&& om) : _buf(std::move(om._buf)) {}
        explicit imem_stream(const omem_stream&) = delete;
        explicit imem_stream(archive<omem_stream, archive_format_t::custom>&& oma) : imem_stream(std::move(oma.get_stream())) {}
        explicit imem_stream(const archive<omem_stream, archive_format_t::custom>&) = delete;

        void read(char_type* c, std::streamsize size)
        {
            assert(_read_pos + size <= _buf.size());
            std::copy_n(_buf.begin() + _read_pos, size, c);
            _read_pos += size;
        }
    private:
        std::vector<char_type> _buf;
        std::size_t _read_pos{};
    };


    template<class Stream>
    using bin_archive = archive<Stream, archive_format_t::binary>;

    template<class Stream>
    using text_archive = archive<Stream, archive_format_t::text>;

    using imem_archive = archive<imem_stream, archive_format_t::custom>;
    using omem_archive = archive<omem_stream, archive_format_t::custom>;

    //---------------------------------------------------------------------
    // serialize through conversion to type T
    template<class As, class T>
    class serialize_as_t
    {
        T t;
    public:
        explicit serialize_as_t(auto&& ... args) : t(std::forward<decltype(args)>(args)...) {}
        decltype(auto) get() const { return t; }
        decltype(auto) get() { return t; }

        template<class Ar>
        auto serialize(Ar& a) requires(is_iarchive_v<Ar>)
        {
            As tmp;
            a(tmp);
            t = static_cast<std::remove_cvref_t<T>>(tmp);
        }
        template<class Ar>
        auto serialize(Ar& a) const requires(is_oarchive_v<Ar>)
        {
            a(static_cast<As>(t));
        }
    };

    template<class As, class T>
    serialize_as_t(T&& t)->serialize_as_t<As, T>;

    template<class As, class T>
    auto serialize_as(T&& t) { return serialize_as_t<As, T>(std::forward<T>(t)); }

    //---------------------------------------------------------------------
    // variadic serialize function
    template<class Archive, class... Ts>
    void serialize(Archive&& a, Ts&& ... ts) requires(sizeof...(Ts) > 1)
    {
        (serialize(std::forward<Archive>(a), std::forward<Ts>(ts)), ...);
    }

    //---------------------------------------------------------------------
    // tuple/pair

    template<class Archive, class T>
    auto serialize(Archive&& a, T&& t) requires(is_tuple_v<std::remove_cvref_t<T>>)
    {
        std::apply(std::forward<Archive>(a), std::forward<T>(t));
    }

    //---------------------------------------------------------------------
    // serialization of contiguous containers (vector, string, valarray) of trivial types
    template<class Archive, class T>
    auto serialize(Archive&& a, T&& t) requires(is_trivial_sequence_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");
        static_assert(is_iarchive_v<Archive> || is_oarchive_v<Archive>, "invalid archive");

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
    auto serialize(Archive&& a, T&& t) requires(is_common_sequence_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");

        if constexpr (is_resizable_v<std::remove_cvref_t<T>>)
        {
            if constexpr (is_iarchive_v<Archive>)
            {
                static_assert(!std::is_const_v<std::remove_reference_t<T>>);
                static_assert(std::is_default_constructible_v<std::remove_cvref_t<decltype(t[0])>>);

                using size_type = decltype(std::size(t));
                size_type size{ 0 };
                a(size);
                t.resize(size); // must be default constructibel to resize
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
    auto serialize(Archive&& a, T&& t) requires(is_queue_v<T> || is_stack_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>, "Archive can't be const");
        static_assert(!std::is_const_v<std::remove_reference_t<T>>);
        using value_type = typename std::remove_cvref_t<T>::value_type;

        if constexpr (is_iarchive_v<Archive>)
        {
            static_assert(std::is_default_constructible_v<value_type>);

            // reading clears the queue
            std::remove_cvref_t<T>{}.swap(t);

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
                std::remove_cvref_t<T> tmp;

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
    auto serialize(Archive&& a, T&& t) requires(is_ordered_associative_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        using key_type = typename std::remove_cvref_t<T>::key_type;
        using value_type = typename std::remove_cvref_t<T>::value_type;

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
                    std::pair<typename std::remove_cvref_t<T>::key_type, typename std::remove_cvref_t<T>::mapped_type> tmp;
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
    auto serialize(Archive&& a, T&& t) requires(is_unordered_associative_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        using key_type = typename std::remove_cvref_t<T>::key_type;
        using value_type = typename std::remove_cvref_t<T>::value_type;

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
                    std::pair<typename std::remove_cvref_t<T>::key_type, typename std::remove_cvref_t<T>::mapped_type> tmp;
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
    auto serialize(Archive&& a, T&& t) requires(is_optional_v<T>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);

        if constexpr (is_iarchive_v<Archive>)
        {
            auto has_value = false;
            a(serialize_as<char>(has_value));
            if (has_value)
            {
                std::remove_cvref_t<decltype(t.value())> value;
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
            if (index >= std::variant_size_v<Variant>)
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
    auto serialize(Archive&& a, T&& t) requires(is_variant_v<T>)
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
    //---------------------------------------------------------------------
    // serialization of std::array
    template<class Archive, class T, std::size_t N>
    auto serialize(Archive&& a, std::array<T, N>& t) requires(is_iarchive_v<Archive>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        for (std::size_t i = 0; i < N; ++i)
            a(t[i]);
    }
    //---------------------------------------------------------------------
    // serialization of std::array
    template<class Archive, class T, std::size_t N>
    auto serialize(Archive&& a, const std::array<T, N>& t) requires(is_oarchive_v<Archive>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        for (std::size_t i = 0; i < N; ++i)
            a(t[i]);
    }
    //---------------------------------------------------------------------
    // serialize span
    template<class Archive, class T, std::size_t Extent>
    auto serialize(Archive&& a, std::span<T, Extent>& s) requires(is_iarchive_v<Archive>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        for (auto it = s.begin(); it != s.end(); ++it)
            a(*it);
    }
    //---------------------------------------------------------------------
    // serialize span
    template<class Archive, class T, std::size_t Extent>
    auto serialize(Archive&& a, const std::span<T, Extent>& s) requires(is_oarchive_v<Archive>)
    {
        static_assert(!std::is_const_v<std::remove_reference_t<Archive>>);
        for (auto it = s.begin(); it != s.end(); ++it)
            a(*it);
    }
}
