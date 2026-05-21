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

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "datapool.h"
#include "tree.h"

namespace gb::yadro::container
{
    template<class CharT = char>
    class basic_gbdb
    {
    public:
        using char_type = CharT;
        using string_pool_type = basic_string_pool<CharT>;
        using string_id = typename string_pool_type::string_id;
        using string_view = std::basic_string_view<CharT>;
        using string_type = std::basic_string<CharT>;
        using path_view = std::initializer_list<string_view>;
        using path_span = std::span<const string_view>;

        struct string_ref
        {
            string_id id{};
            friend bool operator==(const string_ref&, const string_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct int_array_ref
        {
            unique_data_pool<std::int64_t>::array_id id{};
            friend bool operator==(const int_array_ref&, const int_array_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct uint_array_ref
        {
            unique_data_pool<std::uint64_t>::array_id id{};
            friend bool operator==(const uint_array_ref&, const uint_array_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct double_array_ref
        {
            unique_data_pool<double>::array_id id{};
            friend bool operator==(const double_array_ref&, const double_array_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct string_array_ref
        {
            unique_data_pool<string_id>::array_id id{};
            friend bool operator==(const string_array_ref&, const string_array_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct blob_ref
        {
            duplicate_data_pool<std::byte>::array_id id{};
            friend bool operator==(const blob_ref&, const blob_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        using value_type = std::variant<
            std::monostate,
            bool,
            std::int64_t,
            std::uint64_t,
            double,
            string_ref,
            int_array_ref,
            uint_array_ref,
            double_array_ref,
            string_array_ref,
            blob_ref>;

        struct node_payload
        {
            string_id key{};
            value_type value{};

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.key), self.value);
            }
        };

        using tree_type = indexed_tree<node_payload>;
        using node_id = typename tree_type::index_t;
        static constexpr node_id invalid_node = tree_type::invalid_index;
        static constexpr node_id root_node = 0;

        basic_gbdb()
            : _tree(node_payload{})
        {
            auto root_key = intern({});
            _tree.get_value(root_node).key = root_key;
        }

        [[nodiscard]] node_id node_count() const noexcept
        {
            return _node_count;
        }

        [[nodiscard]] string_view key(node_id node) const
        {
            return string(_tree.get_value(node).key);
        }

        [[nodiscard]] const value_type& value(node_id node) const
        {
            return _tree.get_value(node).value;
        }

        [[nodiscard]] value_type& value(node_id node)
        {
            return _tree.get_value(node).value;
        }

        [[nodiscard]] node_id find(path_view path) const
        {
            return find(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] node_id find(path_span path) const
        {
            node_id parent = root_node;
            for (auto part : path) {
                auto key = find_string(part);
                if (key == invalid_string)
                    return invalid_node;

                auto found = _child_index.find(child_key{ parent, key });
                if (found == _child_index.end())
                    return invalid_node;

                parent = found->second;
            }
            return parent;
        }

        [[nodiscard]] bool contains(path_view path) const
        {
            return contains(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] bool contains(path_span path) const
        {
            return find(path) != invalid_node;
        }

        [[nodiscard]] const value_type* get(path_view path) const
        {
            return get(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] const value_type* get(path_span path) const
        {
            auto node = find(path);
            return node == invalid_node ? nullptr : std::addressof(_tree.get_value(node).value);
        }

        [[nodiscard]] value_type* get(path_view path)
        {
            return get(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] value_type* get(path_span path)
        {
            auto node = find(path);
            return node == invalid_node ? nullptr : std::addressof(_tree.get_value(node).value);
        }

        node_id set(path_view path, value_type value)
        {
            return set(path_span{ path.begin(), path.size() }, std::move(value));
        }

        node_id set(path_span path, value_type value)
        {
            auto node = ensure_path(path);
            _tree.get_value(node).value = std::move(value);
            return node;
        }

        node_id set(path_view path, std::nullptr_t)
        {
            return set(path, value_type{ std::monostate{} });
        }

        node_id set(path_span path, std::nullptr_t)
        {
            return set(path, value_type{ std::monostate{} });
        }

        node_id set(path_view path, bool value)
        {
            return set(path, value_type{ value });
        }

        node_id set(path_span path, bool value)
        {
            return set(path, value_type{ value });
        }

        template<std::integral IntT>
            requires(!std::same_as<std::remove_cv_t<IntT>, bool>)
        node_id set(path_view path, IntT value)
        {
            if constexpr (std::is_signed_v<IntT>)
                return set(path, value_type{ static_cast<std::int64_t>(value) });
            else
                return set(path, value_type{ static_cast<std::uint64_t>(value) });
        }

        template<std::integral IntT>
            requires(!std::same_as<std::remove_cv_t<IntT>, bool>)
        node_id set(path_span path, IntT value)
        {
            if constexpr (std::is_signed_v<IntT>)
                return set(path, value_type{ static_cast<std::int64_t>(value) });
            else
                return set(path, value_type{ static_cast<std::uint64_t>(value) });
        }

        node_id set(path_view path, float value)
        {
            return set(path, static_cast<double>(value));
        }

        node_id set(path_span path, float value)
        {
            return set(path, static_cast<double>(value));
        }

        node_id set(path_view path, double value)
        {
            return set(path, value_type{ value });
        }

        node_id set(path_span path, double value)
        {
            return set(path, value_type{ value });
        }

        node_id set(path_view path, const CharT* value)
        {
            return set(path, string_view{ value });
        }

        node_id set(path_span path, const CharT* value)
        {
            return set(path, string_view{ value });
        }

        node_id set(path_view path, string_view value)
        {
            return set(path, value_type{ string_ref{ intern(value) } });
        }

        node_id set(path_span path, string_view value)
        {
            return set(path, value_type{ string_ref{ intern(value) } });
        }

        node_id set(path_view path, const string_type& value)
        {
            return set(path, string_view{ value });
        }

        node_id set(path_span path, const string_type& value)
        {
            return set(path, string_view{ value });
        }

        node_id set_array(path_view path, std::span<const std::int64_t> values)
        {
            return set_array(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_array(path_span path, std::span<const std::int64_t> values)
        {
            return set(path, value_type{ int_array_ref{ _int_arrays.insert(values) } });
        }

        node_id set_array(path_view path, std::span<const std::uint64_t> values)
        {
            return set_array(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_array(path_span path, std::span<const std::uint64_t> values)
        {
            return set(path, value_type{ uint_array_ref{ _uint_arrays.insert(values) } });
        }

        node_id set_array(path_view path, std::span<const double> values)
        {
            return set_array(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_array(path_span path, std::span<const double> values)
        {
            return set(path, value_type{ double_array_ref{ _double_arrays.insert(values) } });
        }

        node_id set_string_array(path_view path, std::initializer_list<string_view> values)
        {
            return set_string_array(path_span{ path.begin(), path.size() }, std::span<const string_view>{ values.begin(), values.size() });
        }

        node_id set_string_array(path_span path, std::span<const string_view> values)
        {
            std::vector<string_id> ids;
            ids.reserve(values.size());
            for (auto value : values)
                ids.push_back(intern(value));

            return set(path, value_type{ string_array_ref{ _string_arrays.insert(ids) } });
        }

        node_id set_blob(path_view path, std::span<const std::byte> values)
        {
            return set_blob(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_blob(path_span path, std::span<const std::byte> values)
        {
            return set(path, value_type{ blob_ref{ _blobs.insert(values) } });
        }

        [[nodiscard]] string_view string(string_id id) const
        {
            return _strings.view(id);
        }

        [[nodiscard]] string_view string(string_ref ref) const
        {
            return string(ref.id);
        }

        [[nodiscard]] std::span<const std::int64_t> array(int_array_ref ref) const
        {
            return _int_arrays.span(ref.id);
        }

        [[nodiscard]] std::span<const std::uint64_t> array(uint_array_ref ref) const
        {
            return _uint_arrays.span(ref.id);
        }

        [[nodiscard]] std::span<const double> array(double_array_ref ref) const
        {
            return _double_arrays.span(ref.id);
        }

        [[nodiscard]] std::span<const string_id> array(string_array_ref ref) const
        {
            return _string_arrays.span(ref.id);
        }

        [[nodiscard]] std::span<const std::byte> blob(blob_ref ref) const
        {
            return _blobs.span(ref.id);
        }

        [[nodiscard]] const tree_type& tree() const noexcept
        {
            return _tree;
        }

        [[nodiscard]] tree_type& tree() noexcept
        {
            return _tree;
        }

        template<class Ar>
        void serialize(this auto&& self, Ar&& archive)
        {
            if constexpr (gb::yadro::archive::iarchive_like<Ar>) {
                static_assert(!std::is_const_v<std::remove_reference_t<decltype(self)>>);

                self.reset_empty();
                self.load_string_pool(archive);
                self.load_array_pool(archive, self._int_arrays);
                self.load_array_pool(archive, self._uint_arrays);
                self.load_array_pool(archive, self._double_arrays);
                self.load_array_pool(archive, self._string_arrays);
                self.load_array_pool(archive, self._blobs);
                archive(self._tree);
                self.rebuild_indexes();
            }
            else {
                self.save_string_pool(archive);
                self.save_array_pool(archive, self._int_arrays);
                self.save_array_pool(archive, self._uint_arrays);
                self.save_array_pool(archive, self._double_arrays);
                self.save_array_pool(archive, self._string_arrays);
                self.save_array_pool(archive, self._blobs);
                archive(self._tree);
            }
        }

    private:
        static constexpr string_id invalid_string = (std::numeric_limits<string_id>::max)();

        struct child_key
        {
            node_id parent{};
            string_id key{};
            friend bool operator==(const child_key&, const child_key&) = default;
        };

        struct child_key_hash
        {
            [[nodiscard]] std::size_t operator()(const child_key& value) const noexcept
            {
                auto parent = static_cast<std::uint64_t>(value.parent);
                auto key = static_cast<std::uint64_t>(value.key);
                return static_cast<std::size_t>(parent ^ (key + 0x9e3779b97f4a7c15ULL + (parent << 6) + (parent >> 2)));
            }
        };

        [[nodiscard]] string_id find_string(string_view value) const
        {
            auto found = _string_lookup.find(string_type{ value });
            return found == _string_lookup.end() ? invalid_string : found->second;
        }

        string_id intern(string_view value)
        {
            string_type key{ value };
            auto found = _string_lookup.find(key);
            if (found != _string_lookup.end())
                return found->second;

            auto id = _strings.insert(value);
            _string_lookup.emplace(std::move(key), id);
            return id;
        }

        node_id ensure_path(path_span path)
        {
            node_id parent = root_node;
            for (auto part : path) {
                auto key = intern(part);
                auto lookup_key = child_key{ parent, key };
                auto found = _child_index.find(lookup_key);
                if (found != _child_index.end()) {
                    parent = found->second;
                    continue;
                }

                auto child = _tree.insert_child(parent, node_payload{ .key = key });
                ++_node_count;
                _child_index.emplace(lookup_key, child);
                parent = child;
            }
            return parent;
        }

        void reset_empty()
        {
            _strings = string_pool_type{};
            _string_lookup.clear();
            _int_arrays = unique_data_pool<std::int64_t>{};
            _uint_arrays = unique_data_pool<std::uint64_t>{};
            _double_arrays = unique_data_pool<double>{};
            _string_arrays = unique_data_pool<string_id>{};
            _blobs = duplicate_data_pool<std::byte>{};
            _tree = tree_type(node_payload{});
            _child_index.clear();
            _node_count = 1;
            _tree.get_value(root_node).key = intern({});
        }

        template<class Ar>
        void save_string_pool(Ar& archive) const
        {
            auto count = _strings.string_count();
            archive(gb::yadro::archive::serialize_as<std::uint64_t>(count));
            for (string_id id = 0; id < count; ++id) {
                string_type value{ _strings.view(id) };
                archive(value);
            }
        }

        template<class Ar>
        void load_string_pool(Ar& archive)
        {
            std::uint64_t count{};
            archive(gb::yadro::archive::serialize_as<std::uint64_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                string_type value;
                archive(value);
                auto id = intern(value);
                if (id != static_cast<string_id>(i))
                    throw std::runtime_error("gbdb string pool archive has non-canonical string order");
            }
        }

        template<class Ar, class Pool>
        static void save_array_pool(Ar& archive, const Pool& pool)
        {
            using array_id = typename Pool::array_id;
            using value_type = typename Pool::value_type;

            auto count = pool.array_count();
            archive(gb::yadro::archive::serialize_as<std::uint64_t>(count));
            for (array_id id = 0; id < count; ++id) {
                auto data = pool.span(id);
                std::vector<value_type> values(data.begin(), data.end());
                archive(values);
            }
        }

        template<class Ar, class Pool>
        static void load_array_pool(Ar& archive, Pool& pool)
        {
            using array_id = typename Pool::array_id;
            using value_type = typename Pool::value_type;

            pool = Pool{};
            std::uint64_t count{};
            archive(gb::yadro::archive::serialize_as<std::uint64_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                std::vector<value_type> values;
                archive(values);
                auto id = pool.insert(values);
                if (id != static_cast<array_id>(i))
                    throw std::runtime_error("gbdb data pool archive has non-canonical array order");
            }
        }

        void rebuild_indexes()
        {
            _string_lookup.clear();
            auto string_count = _strings.string_count();
            _string_lookup.reserve(string_count);
            for (string_id id = 0; id < string_count; ++id)
                _string_lookup.emplace(string_type{ _strings.view(id) }, id);

            _child_index.clear();
            _node_count = static_cast<node_id>(_tree.get_nodes().size());
            _child_index.reserve(_node_count);

            for (node_id node = 1; node < _node_count; ++node) {
                auto parent = _tree.get_parent(node);
                if (parent != invalid_node)
                    _child_index.emplace(child_key{ parent, _tree.get_value(node).key }, node);
            }
        }

        string_pool_type _strings;
        std::unordered_map<string_type, string_id> _string_lookup;
        unique_data_pool<std::int64_t> _int_arrays;
        unique_data_pool<std::uint64_t> _uint_arrays;
        unique_data_pool<double> _double_arrays;
        unique_data_pool<string_id> _string_arrays;
        duplicate_data_pool<std::byte> _blobs;
        tree_type _tree;
        std::unordered_map<child_key, node_id, child_key_hash> _child_index;
        node_id _node_count = 1;
    };

    using json_db = basic_gbdb<char>;
}
