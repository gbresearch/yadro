//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
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
//  works are solely in the form of machine-executable object code generated
//  from a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//  OTHER DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../util/hash_util.h"

namespace gb::yadro::container
{
    struct duplicate_array_policy {};
    struct unique_array_policy {};

    namespace detail
    {
        template<class T>
        [[nodiscard]] uint64_t hash_array(std::span<const T> values)
        {
            if constexpr (std::is_trivially_copyable_v<T>) {
                return gb::yadro::util::xxhash128::hash_mem(values.data(), values.size_bytes()).reduce();
            }
            else if constexpr (requires(const T& value) { std::hash<T>{}(value); }) {
                uint64_t seed = 0x9e3779b97f4a7c15ULL ^ static_cast<uint64_t>(values.size());
                for (const auto& value : values) {
                    auto h = static_cast<uint64_t>(std::hash<T>{}(value));
                    seed ^= h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
                }
                return seed;
            }
            else {
                return gb::yadro::util::xxhash128::hash_mem(
                    values.data(),
                    values.size() * sizeof(T)).reduce();
            }
        }

        template<class SizeT, class ValueT>
        [[nodiscard]] SizeT checked_size_cast(ValueT value)
        {
            if (value > static_cast<ValueT>(std::numeric_limits<SizeT>::max()))
                throw std::length_error("data_pool size exceeds size_type capacity");
            return static_cast<SizeT>(value);
        }
    }

    template<
        class T,
        class SizeT = std::uint32_t,
        class LiveSizeT = std::uint32_t,
        class Policy = duplicate_array_policy>
    class data_pool
    {
    public:
        using value_type = T;
        using size_type = SizeT;
        using live_size_type = LiveSizeT;
        using array_id = std::uint32_t;
        using element_id = SizeT;
        using hash_type = std::uint64_t;

        struct array_record
        {
            std::uint64_t offset = 0;
            std::uint64_t hash = 0;
            size_type size = 0;
            live_size_type live_size = 0;
        };

        static constexpr array_id invalid_array = (std::numeric_limits<array_id>::max)();

        data_pool() = default;

        data_pool(const data_pool& other)
        {
            copy_from(other);
        }

        data_pool& operator=(const data_pool& other)
        {
            if (this != std::addressof(other)) {
                data_pool copy(other);
                *this = std::move(copy);
            }
            return *this;
        }

        data_pool(data_pool&& other) noexcept
            : _arena(std::move(other._arena))
            , _arena_size(other._arena_size)
            , _arena_capacity(other._arena_capacity)
            , _records(std::move(other._records))
            , _tombstones(std::move(other._tombstones))
            , _unique_index(std::move(other._unique_index))
        {
            other._arena_size = 0;
            other._arena_capacity = 0;
            other._records.clear();
        }

        data_pool& operator=(data_pool&& other) noexcept
        {
            if (this != std::addressof(other)) {
                clear();
                _arena = std::move(other._arena);
                _arena_size = other._arena_size;
                _arena_capacity = other._arena_capacity;
                _records = std::move(other._records);
                _tombstones = std::move(other._tombstones);
                _unique_index = std::move(other._unique_index);
                other._arena_size = 0;
                other._arena_capacity = 0;
                other._records.clear();
            }
            return *this;
        }

        ~data_pool()
        {
            clear();
        }

        template<std::ranges::contiguous_range Range>
        array_id insert(const Range& values)
            requires std::convertible_to<std::ranges::range_reference_t<const Range>, T>
        {
            return insert(std::span{ std::ranges::data(values), std::ranges::size(values) });
        }

        array_id insert(std::span<const T> values)
        {
            auto h = detail::hash_array(values);
            if constexpr (std::is_same_v<Policy, unique_array_policy>) {
                auto [first, last] = _unique_index.equal_range(h);
                for (auto i = first; i != last; ++i) {
                    if (equals(i->second, values))
                        return i->second;
                }
            }

            auto id = append(values, h);
            if constexpr (std::is_same_v<Policy, unique_array_policy>)
                _unique_index.emplace(h, id);
            return id;
        }

        [[nodiscard]] array_id array_count() const noexcept
        {
            return static_cast<array_id>(_records.size());
        }

        [[nodiscard]] size_type size(array_id id) const
        {
            return record(id).size;
        }

        [[nodiscard]] live_size_type live_size(array_id id) const
        {
            return record(id).live_size;
        }

        [[nodiscard]] hash_type hash(array_id id) const
        {
            return record(id).hash;
        }

        [[nodiscard]] bool is_live(array_id id, element_id element) const
        {
            const auto& r = record(id);
            if (element >= r.size)
                return false;
            return !is_erased(id, element);
        }

        [[nodiscard]] T& operator()(array_id id, element_id element)
        {
            assert(is_live(id, element));
            return data(record(id))[element];
        }

        [[nodiscard]] const T& operator()(array_id id, element_id element) const
        {
            assert(is_live(id, element));
            return data(record(id))[element];
        }

        [[nodiscard]] std::span<T> span(array_id id)
        {
            auto& r = record(id);
            return { data(r), static_cast<std::size_t>(r.size) };
        }

        [[nodiscard]] std::span<const T> span(array_id id) const
        {
            const auto& r = record(id);
            return { data(r), static_cast<std::size_t>(r.size) };
        }

        void erase(array_id id, element_id element)
        {
            auto& r = record(id);
            if (element >= r.size)
                throw std::out_of_range("data_pool element_id is out of range");
            if (is_erased(id, element))
                return;

            std::destroy_at(std::addressof(data(r)[element]));
            ensure_tombstones(id, r.size).set(element);
            --r.live_size;
        }

        void compact()
        {
            std::size_t compact_capacity = 0;
            for (const auto& r : _records) {
                compact_capacity = aligned_offset(compact_capacity);
                compact_capacity += sizeof(T) * static_cast<std::size_t>(r.live_size);
            }

            std::unique_ptr<std::byte[]> new_arena =
                compact_capacity == 0 ? nullptr : std::make_unique_for_overwrite<std::byte[]>(compact_capacity);
            std::size_t new_arena_size = 0;
            std::vector<array_record> new_records;
            new_records.reserve(_records.size());

            for (array_id id = 0; id < _records.size(); ++id) {
                auto& old_record = _records[id];
                auto new_size = static_cast<size_type>(old_record.live_size);
                auto offset = aligned_offset(new_arena_size);
                auto required = offset + sizeof(T) * static_cast<std::size_t>(new_size);

                auto* dst = reinterpret_cast<T*>(compact_capacity == 0 ? nullptr : new_arena.get() + offset);
                auto* src = data(old_record);
                std::size_t out = 0;

                for (size_type i = 0; i < old_record.size; ++i) {
                    if (!is_erased(id, i)) {
                        std::construct_at(std::addressof(dst[out]), std::move_if_noexcept(src[i]));
                        std::destroy_at(std::addressof(src[i]));
                        ++out;
                    }
                }
                new_arena_size = required;

                auto compacted_hash = detail::hash_array(std::span<const T>{ dst, static_cast<std::size_t>(new_size) });
                new_records.push_back(array_record{
                    .offset = static_cast<std::uint64_t>(offset),
                    .hash = compacted_hash,
                    .size = new_size,
                    .live_size = static_cast<live_size_type>(new_size)
                    });
            }

            _arena = std::move(new_arena);
            _arena_size = new_arena_size;
            _arena_capacity = compact_capacity;
            _records = std::move(new_records);
            _tombstones.clear();

            if constexpr (std::is_same_v<Policy, unique_array_policy>)
                rebuild_unique_index();
        }

        void clear() noexcept
        {
            for (array_id id = 0; id < _records.size(); ++id) {
                auto& r = _records[id];
                auto* ptr = data(r);
                for (size_type i = 0; i < r.size; ++i) {
                    if (!is_erased(id, i))
                        std::destroy_at(std::addressof(ptr[i]));
                }
            }
            _records.clear();
            _arena.reset();
            _arena_size = 0;
            _arena_capacity = 0;
            _tombstones.clear();
            _unique_index.clear();
        }

    private:
        struct tombstone_bitmap
        {
            std::vector<std::uint64_t> bits;

            explicit tombstone_bitmap(std::size_t size)
                : bits((size + 63) / 64)
            {
            }

            [[nodiscard]] bool test(std::size_t index) const
            {
                return (bits[index / 64] & (std::uint64_t{ 1 } << (index % 64))) != 0;
            }

            void set(std::size_t index)
            {
                bits[index / 64] |= std::uint64_t{ 1 } << (index % 64);
            }
        };

        [[nodiscard]] array_record& record(array_id id)
        {
            if (id >= _records.size())
                throw std::out_of_range("data_pool array_id is out of range");
            return _records[id];
        }

        [[nodiscard]] const array_record& record(array_id id) const
        {
            if (id >= _records.size())
                throw std::out_of_range("data_pool array_id is out of range");
            return _records[id];
        }

        [[nodiscard]] T* data(array_record& r) noexcept
        {
            return reinterpret_cast<T*>(_arena ? _arena.get() + r.offset : nullptr);
        }

        [[nodiscard]] const T* data(const array_record& r) const noexcept
        {
            return reinterpret_cast<const T*>(_arena ? _arena.get() + r.offset : nullptr);
        }

        [[nodiscard]] std::size_t aligned_offset(std::size_t offset) const noexcept
        {
            constexpr auto alignment = alignof(T);
            if constexpr (std::has_single_bit(alignment))
                return (offset + alignment - 1) & ~(alignment - 1);
            else
                return offset + ((alignment - offset % alignment) % alignment);
        }

        array_id append(std::span<const T> values, hash_type h)
        {
            if (_records.size() == invalid_array)
                throw std::length_error("data_pool array_id capacity exceeded");

            auto size = detail::checked_size_cast<size_type>(values.size());
            auto live_size = detail::checked_size_cast<live_size_type>(values.size());
            auto offset = aligned_offset(_arena_size);
            auto required = offset + values.size_bytes();
            reserve_bytes(required);

            auto* dst = reinterpret_cast<T*>(values.empty() ? nullptr : _arena.get() + offset);
            std::size_t constructed = 0;
            try {
                for (; constructed < values.size(); ++constructed)
                    std::construct_at(std::addressof(dst[constructed]), values[constructed]);
            }
            catch (...) {
                while (constructed > 0) {
                    --constructed;
                    std::destroy_at(std::addressof(dst[constructed]));
                }
                throw;
            }
            _arena_size = required;

            auto id = static_cast<array_id>(_records.size());
            _records.push_back(array_record{
                .offset = static_cast<std::uint64_t>(offset),
                .hash = h,
                .size = size,
                .live_size = live_size
                });
            return id;
        }

        [[nodiscard]] bool equals(array_id id, std::span<const T> values) const
        {
            const auto& r = record(id);
            if (r.size != values.size() || r.live_size != r.size)
                return false;
            return std::ranges::equal(span(id), values);
        }

        [[nodiscard]] bool is_erased(array_id id, element_id element) const
        {
            auto found = _tombstones.find(id);
            return found != _tombstones.end() && found->second.test(element);
        }

        tombstone_bitmap& ensure_tombstones(array_id id, size_type size)
        {
            auto [it, _] = _tombstones.try_emplace(id, static_cast<std::size_t>(size));
            return it->second;
        }

        void rebuild_unique_index()
        {
            _unique_index.clear();
            for (array_id id = 0; id < _records.size(); ++id)
                _unique_index.emplace(_records[id].hash, id);
        }

        [[nodiscard]] static std::size_t grow_capacity(std::size_t capacity, std::size_t required)
        {
            auto next = capacity == 0 ? std::size_t{ 256 } : capacity * 2;
            while (next < required)
                next *= 2;
            return next;
        }

        void reserve_bytes(std::size_t required)
        {
            if (required <= _arena_capacity)
                return;

            auto next_capacity = grow_capacity(_arena_capacity, required);
            auto next_arena = std::make_unique_for_overwrite<std::byte[]>(next_capacity);

            for (auto& r : _records) {
                auto* src = data(r);
                auto* dst = reinterpret_cast<T*>(next_arena.get() + r.offset);
                for (size_type i = 0; i < r.size; ++i) {
                    if (!is_erased(static_cast<array_id>(std::addressof(r) - _records.data()), i)) {
                        std::construct_at(std::addressof(dst[i]), std::move_if_noexcept(src[i]));
                        std::destroy_at(std::addressof(src[i]));
                    }
                }
            }

            _arena = std::move(next_arena);
            _arena_capacity = next_capacity;
        }

        void copy_from(const data_pool& other)
        {
            _arena_size = other._arena_size;
            _arena_capacity = other._arena_capacity;
            _records = other._records;
            _tombstones = other._tombstones;
            _unique_index = other._unique_index;

            if (_arena_capacity == 0)
                return;

            _arena = std::make_unique_for_overwrite<std::byte[]>(_arena_capacity);
            std::vector<std::pair<array_id, size_type>> constructed;

            try {
                for (array_id id = 0; id < _records.size(); ++id) {
                    auto& dst_record = _records[id];
                    const auto& src_record = other._records[id];
                    auto* dst = data(dst_record);
                    auto* src = other.data(src_record);

                    for (size_type element = 0; element < dst_record.size; ++element) {
                        if (!is_erased(id, element)) {
                            std::construct_at(std::addressof(dst[element]), src[element]);
                            constructed.emplace_back(id, element);
                        }
                    }
                }
            }
            catch (...) {
                for (auto [id, element] : constructed) {
                    auto* dst = data(_records[id]);
                    std::destroy_at(std::addressof(dst[element]));
                }

                _records.clear();
                _arena.reset();
                _arena_size = 0;
                _arena_capacity = 0;
                _tombstones.clear();
                _unique_index.clear();
                throw;
            }
        }

        std::unique_ptr<std::byte[]> _arena;
        std::size_t _arena_size = 0;
        std::size_t _arena_capacity = 0;
        std::vector<array_record> _records;
        std::unordered_map<array_id, tombstone_bitmap> _tombstones;
        [[no_unique_address]] std::unordered_multimap<hash_type, array_id> _unique_index;
    };

    template<class T, class SizeT = std::uint32_t, class LiveSizeT = std::uint32_t>
    using duplicate_data_pool = data_pool<T, SizeT, LiveSizeT, duplicate_array_policy>;

    template<class T, class SizeT = std::uint32_t, class LiveSizeT = std::uint32_t>
    using unique_data_pool = data_pool<T, SizeT, LiveSizeT, unique_array_policy>;

    template<class CharT = char, class SizeT = std::uint32_t, class OffsetT = std::uint32_t>
    class basic_string_pool
    {
    public:
        using char_type = CharT;
        using size_type = SizeT;
        using offset_type = OffsetT;
        using string_id = std::uint32_t;

        struct string_record
        {
            std::uint64_t hash = 0;
            offset_type offset = 0;
            size_type size = 0;
        };

        string_id insert(std::basic_string_view<CharT> value)
        {
            auto h = hash_string(value);
            auto [first, last] = _index.equal_range(h);
            for (auto i = first; i != last; ++i) {
                if (view(i->second) == value)
                    return i->second;
            }

            if (_records.size() == (std::numeric_limits<string_id>::max)())
                throw std::length_error("string_pool string_id capacity exceeded");

            auto id = static_cast<string_id>(_records.size());
            auto offset = detail::checked_size_cast<offset_type>(_buffer.size());
            _buffer.insert(_buffer.end(), value.begin(), value.end());
            _buffer.push_back(CharT{});
            _records.push_back(string_record{
                .hash = h,
                .offset = offset,
                .size = detail::checked_size_cast<size_type>(value.size())
                });
            _index.emplace(h, id);
            return id;
        }

        [[nodiscard]] string_id string_count() const noexcept
        {
            return static_cast<string_id>(_records.size());
        }

        [[nodiscard]] size_type size(string_id id) const
        {
            return record(id).size;
        }

        [[nodiscard]] std::uint64_t hash(string_id id) const
        {
            return record(id).hash;
        }

        [[nodiscard]] const CharT* c_str(string_id id) const
        {
            return _buffer.data() + record(id).offset;
        }

        [[nodiscard]] std::basic_string_view<CharT> view(string_id id) const
        {
            const auto& r = record(id);
            return { _buffer.data() + r.offset, static_cast<std::size_t>(r.size) };
        }

    private:
        [[nodiscard]] const string_record& record(string_id id) const
        {
            if (id >= _records.size())
                throw std::out_of_range("string_pool string_id is out of range");
            return _records[id];
        }

        [[nodiscard]] static std::uint64_t hash_string(std::basic_string_view<CharT> value)
        {
            return gb::yadro::util::xxhash128::hash_mem(
                value.data(),
                value.size() * sizeof(CharT)).reduce();
        }

        std::vector<CharT> _buffer;
        std::vector<string_record> _records;
        std::unordered_multimap<std::uint64_t, string_id> _index;
    };

    using string_pool = basic_string_pool<char>;
}
