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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "../archive/archive.h"
#include "../util/string_util.h"
#include "datapool.h"
#include "tree.h"

namespace gb::yadro::container
{
    template<class Payload, class Id = std::uint32_t>
    struct payload_reference_type
    {
        Id id{};
        friend bool operator==(const payload_reference_type&, const payload_reference_type&) = default;

        template<class Ar>
        void serialize(this auto&& self, Ar&& archive)
        {
            archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
        }
    };

    template<class Payload>
    struct payload_traits
    {
        using payload_type = Payload;
        using stored_type = Payload;
        using view_type = Payload;

        template<class CharT>
        using pool_type = void;

        template<class Pool, class Value>
        [[nodiscard]] static stored_type store(Pool&, Value&& value)
        {
            return stored_type{ std::forward<Value>(value) };
        }

        template<class Pool>
        [[nodiscard]] static view_type get(const Pool&, const stored_type& value)
        {
            return value;
        }
    };

    template<class CharT>
    struct payload_traits<std::basic_string<CharT>>
    {
        using payload_type = std::basic_string<CharT>;
        using stored_type = payload_reference_type<payload_type>;
        using view_type = std::basic_string_view<CharT>;

        template<class DbCharT>
        using pool_type = basic_string_pool<CharT>;

        template<class Pool, class Value>
        [[nodiscard]] static stored_type store(Pool& pool, Value&& value)
        {
            return stored_type{ pool.insert(std::basic_string_view<CharT>{ value }) };
        }

        template<class Pool>
        [[nodiscard]] static view_type get(const Pool& pool, stored_type ref)
        {
            return pool.view(ref.id);
        }
    };

    namespace gbdb_detail
    {
        template<class T, class... Ts>
        struct type_index;

        template<class T, class... Rest>
        struct type_index<T, T, Rest...>
            : std::integral_constant<std::size_t, 0>
        {};

        template<class T, class First, class... Rest>
        struct type_index<T, First, Rest...>
            : std::integral_constant<std::size_t, 1 + type_index<T, Rest...>::value>
        {};

        template<class T>
        struct type_index<T>
        {
            static_assert(!std::same_as<T, T>, "Payload type is not part of this basic_gbdb");
        };

        template<class... Ts>
        struct compact_value_type
        {
            using type = std::variant<Ts...>;
        };

        template<class T>
        struct compact_value_type<T>
        {
            using type = T;
        };

        template<class CharT, class Payload>
        struct payload_pool
        {
            using traits = payload_traits<Payload>;
            using candidate = typename traits::template pool_type<CharT>;
            using type = std::conditional_t<std::is_void_v<candidate>, std::monostate, candidate>;
        };
    }

    template<class CharT = char, class... Payloads>
    class basic_gbdb;

    template<class CharT, class... Payloads>
    class basic_gbdb
    {
        static_assert(sizeof...(Payloads) > 0, "Use basic_gbdb<CharT> for the JSON DB specialization");

    public:
        using char_type = CharT;
        using string_pool_type = basic_string_pool<CharT>;
        using string_id = typename string_pool_type::string_id;
        using string_view = std::basic_string_view<CharT>;
        using string_type = std::basic_string<CharT>;
        using path_view = std::initializer_list<string_view>;
        using path_span = std::span<const string_view>;
        using value_type = typename gbdb_detail::compact_value_type<typename payload_traits<Payloads>::stored_type...>::type;
        using pools_type = std::tuple<typename gbdb_detail::payload_pool<CharT, Payloads>::type...>;

        struct node_payload
        {
            string_id key{};
            std::optional<value_type> value;
        };

        using tree_type = indexed_tree<node_payload>;
        using node_id = typename tree_type::index_t;
        static constexpr node_id invalid_node = tree_type::invalid_index;
        static constexpr node_id root_node = 0;

        basic_gbdb()
            : _tree(node_payload{})
        {
            _tree.get_value(root_node).key = intern({});
        }

        [[nodiscard]] node_id node_count() const noexcept
        {
            return _node_count;
        }

        [[nodiscard]] string_view key(node_id node) const
        {
            return _keys.view(_tree.get_value(node).key);
        }

        [[nodiscard]] node_id find(path_view path) const
        {
            return find(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] node_id find(string_view path) const
        {
            auto parts = split_slash_path(path);
            return find(path_span{ parts });
        }

        [[nodiscard]] node_id find(path_span path) const
        {
            node_id parent = root_node;
            for (auto part : path) {
                auto key_id = find_key(part);
                if (key_id == invalid_string)
                    return invalid_node;

                auto found = _child_index.find(child_key{ parent, key_id });
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

        [[nodiscard]] bool contains(string_view path) const
        {
            auto parts = split_slash_path(path);
            return contains(path_span{ parts });
        }

        [[nodiscard]] bool contains(path_span path) const
        {
            return find(path) != invalid_node;
        }

        template<class Value>
        node_id set(path_view path, Value&& value)
        {
            return set(path_span{ path.begin(), path.size() }, std::forward<Value>(value));
        }

        template<class Value>
        node_id set(string_view path, Value&& value)
        {
            auto parts = split_slash_path(path);
            return set(path_span{ parts }, std::forward<Value>(value));
        }

        template<class Value>
        node_id set(path_span path, Value&& value)
        {
            using payload_type = std::remove_cvref_t<Value>;
            static_assert((std::same_as<payload_type, Payloads> || ...), "Payload type is not part of this basic_gbdb");
            auto node = ensure_path(path);
            _tree.get_value(node).value = store_payload<payload_type>(std::forward<Value>(value));
            return node;
        }

        template<class Payload>
        [[nodiscard]] std::optional<typename payload_traits<Payload>::view_type> get(path_view path) const
        {
            return get<Payload>(path_span{ path.begin(), path.size() });
        }

        template<class Payload>
        [[nodiscard]] std::optional<typename payload_traits<Payload>::view_type> get(string_view path) const
        {
            auto parts = split_slash_path(path);
            return get<Payload>(path_span{ parts });
        }

        template<class Payload>
        [[nodiscard]] std::optional<typename payload_traits<Payload>::view_type> get(path_span path) const
        {
            auto node = find(path);
            if (node == invalid_node)
                return std::nullopt;

            const auto& value = _tree.get_value(node).value;
            if (!value)
                return std::nullopt;

            using stored_type = typename payload_traits<Payload>::stored_type;
            if constexpr (sizeof...(Payloads) == 1) {
                if constexpr (std::same_as<stored_type, value_type>)
                    return payload_traits<Payload>::get(pool<Payload>(), *value);
                else
                    return std::nullopt;
            }
            else {
                if (const auto* stored = std::get_if<stored_type>(std::addressof(*value)))
                    return payload_traits<Payload>::get(pool<Payload>(), *stored);
                return std::nullopt;
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

        template<class Payload>
        static constexpr std::size_t payload_index = gbdb_detail::type_index<Payload, Payloads...>::value;

        template<class Payload>
        [[nodiscard]] auto& pool()
        {
            return std::get<payload_index<Payload>>(_pools);
        }

        template<class Payload>
        [[nodiscard]] const auto& pool() const
        {
            return std::get<payload_index<Payload>>(_pools);
        }

        template<class Payload, class Value>
        [[nodiscard]] value_type store_payload(Value&& value)
        {
            auto stored = payload_traits<Payload>::store(pool<Payload>(), std::forward<Value>(value));
            if constexpr (sizeof...(Payloads) == 1)
                return stored;
            else
                return value_type{ std::move(stored) };
        }

        [[nodiscard]] string_id find_key(string_view value) const
        {
            auto found = _key_lookup.find(string_type{ value });
            return found == _key_lookup.end() ? invalid_string : found->second;
        }

        string_id intern(string_view value)
        {
            string_type key{ value };
            auto found = _key_lookup.find(key);
            if (found != _key_lookup.end())
                return found->second;

            auto id = _keys.insert(value);
            _key_lookup.emplace(std::move(key), id);
            return id;
        }

        [[nodiscard]] static std::vector<string_view> split_slash_path(string_view path)
        {
            std::vector<string_view> parts;
            if (path.empty())
                return parts;

            std::size_t begin = 0;
            while (begin < path.size()) {
                auto end = path.find(CharT{ '/' }, begin);
                if (end == begin)
                    throw std::invalid_argument("gbdb slash path contains an empty component");

                if (end == string_view::npos) {
                    parts.push_back(path.substr(begin));
                    return parts;
                }

                parts.push_back(path.substr(begin, end - begin));
                begin = end + 1;
                if (begin == path.size())
                    throw std::invalid_argument("gbdb slash path contains an empty component");
            }

            return parts;
        }

        node_id ensure_path(path_span path)
        {
            node_id parent = root_node;
            for (auto part : path) {
                auto key_id = intern(part);
                auto lookup_key = child_key{ parent, key_id };
                auto found = _child_index.find(lookup_key);
                if (found != _child_index.end()) {
                    parent = found->second;
                    continue;
                }

                auto child = _tree.insert_child(parent, node_payload{ .key = key_id });
                ++_node_count;
                _child_index.emplace(lookup_key, child);
                parent = child;
            }
            return parent;
        }

        string_pool_type _keys;
        std::unordered_map<string_type, string_id> _key_lookup;
        pools_type _pools;
        tree_type _tree;
        std::unordered_map<child_key, node_id, child_key_hash> _child_index;
        node_id _node_count = 1;
    };

    template<class CharT>
    class basic_gbdb<CharT>
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

        struct object_ref
        {
            std::uint32_t id{};
            friend bool operator==(const object_ref&, const object_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        enum class blob_load_policy : std::uint8_t
        {
            immediate,
            deferred
        };

        enum class object_blob_storage : std::uint8_t
        {
            inline_memory,
            external_file
        };

        enum class table_column_type : std::uint8_t
        {
            int64,
            uint64,
            double_,
            string
        };

        struct table_ref
        {
            std::uint32_t id{};
            friend bool operator==(const table_ref&, const table_ref&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(gb::yadro::archive::serialize_as<std::uint64_t>(self.id));
            }
        };

        struct table_schema_column
        {
            string_view name;
            table_column_type type{};
        };

        struct table_column
        {
            string_id name{};
            table_column_type type{};
            std::uint32_t array{};
            friend bool operator==(const table_column&, const table_column&) = default;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.name),
                    gb::yadro::archive::serialize_as<std::uint32_t>(self.type),
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.array));
            }
        };

        struct table_record
        {
            std::uint32_t row_count = 0;
            duplicate_data_pool<table_column>::array_id columns = duplicate_data_pool<table_column>::invalid_array;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.row_count),
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.columns));
            }
        };

        struct object_record
        {
            std::vector<std::byte> blob;
            string_id type = (std::numeric_limits<string_id>::max)();
            bool has_version = false;
            std::uint32_t version = 0;
            object_blob_storage storage = object_blob_storage::inline_memory;
            blob_load_policy load_policy = blob_load_policy::immediate;
            string_id blob_uri = (std::numeric_limits<string_id>::max)();
            std::uint64_t blob_size_bytes = 0;
            string_id blob_md5 = (std::numeric_limits<string_id>::max)();
            mutable bool external_loaded = false;
            mutable std::vector<std::byte> external_cache;

            template<class Ar>
            void serialize(this auto&& self, Ar&& archive)
            {
                archive(
                    self.blob,
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.type),
                    self.has_version,
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.version),
                    gb::yadro::archive::serialize_as<std::uint32_t>(self.storage),
                    gb::yadro::archive::serialize_as<std::uint32_t>(self.load_policy),
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.blob_uri),
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.blob_size_bytes),
                    gb::yadro::archive::serialize_as<std::uint64_t>(self.blob_md5));
                if constexpr (gb::yadro::archive::iarchive_like<Ar>) {
                    self.external_loaded = false;
                    self.external_cache.clear();
                }
            }
        };

        struct external_blob_info
        {
            string_view uri;
            std::uint64_t size_bytes = 0;
            string_view md5;
            blob_load_policy load_policy = blob_load_policy::deferred;
            bool loaded = false;
        };

        enum class log_level : std::uint8_t
        {
            trace,
            debug,
            info,
            audit,
            warning,
            error,
            critical
        };

        struct log_options_type
        {
            log_level min_level = log_level::warning;
            bool log_regular_operations = false;
            bool log_warnings = true;
        };

        struct log_event
        {
            log_level level = log_level::info;
            std::string category;
            std::string event;
            std::string path;
            std::string message;
            bool force = false;
        };

        using log_sink = std::function<void(const log_event&)>;

        enum class scan_mode : std::uint8_t
        {
            quick,
            full,
            deep
        };

        enum class scan_severity : std::uint8_t
        {
            info,
            warning,
            error,
            critical
        };

        struct scan_progress
        {
            std::uint64_t scanned_nodes = 0;
            std::uint64_t total_nodes = 0;
            std::string path;
        };

        struct scan_issue
        {
            scan_severity severity = scan_severity::info;
            std::string category;
            std::string path;
            std::string message;
        };

        struct scan_statistics
        {
            std::uint64_t node_count = 0;
            std::uint64_t reachable_node_count = 0;
            std::uint64_t orphaned_node_count = 0;
            std::uint64_t max_depth = 0;
            std::uint64_t value_count = 0;
            std::uint64_t null_value_count = 0;
            std::uint64_t bool_value_count = 0;
            std::uint64_t int_value_count = 0;
            std::uint64_t uint_value_count = 0;
            std::uint64_t double_value_count = 0;
            std::uint64_t string_value_count = 0;
            std::uint64_t int_array_value_count = 0;
            std::uint64_t uint_array_value_count = 0;
            std::uint64_t double_array_value_count = 0;
            std::uint64_t string_array_value_count = 0;
            std::uint64_t blob_value_count = 0;
            std::uint64_t object_value_count = 0;
            std::uint64_t table_value_count = 0;
            std::uint64_t string_count = 0;
            std::uint64_t int_array_count = 0;
            std::uint64_t uint_array_count = 0;
            std::uint64_t double_array_count = 0;
            std::uint64_t string_array_count = 0;
            std::uint64_t blob_count = 0;
            std::uint64_t object_count = 0;
            std::uint64_t external_blob_count = 0;
            std::uint64_t table_count = 0;
            std::uint64_t warning_count = 0;
            std::uint64_t error_count = 0;
            std::uint64_t critical_count = 0;
        };

        struct scan_report
        {
            scan_statistics stats;
            std::vector<scan_issue> issues;
            bool cancelled = false;

            [[nodiscard]] bool ok() const noexcept
            {
                return !cancelled && stats.error_count == 0 && stats.critical_count == 0;
            }
        };

        struct scan_options
        {
            scan_mode mode = scan_mode::quick;
            bool log_findings = true;
            bool collect_paths = true;
            bool check_external_blob_md5 = false;
            bool stop_on_first_error = false;
            std::function<bool(const scan_progress&)> progress;
        };

        struct table_cell
        {
            std::variant<std::int64_t, std::uint64_t, double, string_type> value;

            table_cell(std::int64_t value) : value(value) {}
            table_cell(std::uint64_t value) : value(value) {}
            table_cell(double value) : value(value) {}
            table_cell(float value) : value(static_cast<double>(value)) {}
            table_cell(const CharT* value) : value(string_type{ value }) {}
            table_cell(string_view value) : value(string_type{ value }) {}
            table_cell(const string_type& value) : value(value) {}
            table_cell(string_type&& value) : value(std::move(value)) {}

            template<std::integral IntT>
                requires(!std::same_as<std::remove_cv_t<IntT>, bool>
                    && !std::same_as<std::remove_cv_t<IntT>, std::int64_t>
                    && !std::same_as<std::remove_cv_t<IntT>, std::uint64_t>)
            table_cell(IntT value)
                : value([&] {
                    if constexpr (std::is_signed_v<IntT>)
                        return std::variant<std::int64_t, std::uint64_t, double, string_type>{ static_cast<std::int64_t>(value) };
                    else
                        return std::variant<std::int64_t, std::uint64_t, double, string_type>{ static_cast<std::uint64_t>(value) };
                    }())
            {}
        };

        class table_builder
        {
            friend class basic_gbdb;

        public:
            table_builder() = default;

            explicit table_builder(std::span<const table_schema_column> schema)
            {
                _columns.reserve(schema.size());
                for (auto column : schema) {
                    column_data data;
                    data.name = string_type{ column.name };
                    data.type = column.type;
                    switch (column.type) {
                    case table_column_type::int64: data.values = std::vector<std::int64_t>{}; break;
                    case table_column_type::uint64: data.values = std::vector<std::uint64_t>{}; break;
                    case table_column_type::double_: data.values = std::vector<double>{}; break;
                    case table_column_type::string: data.values = std::vector<string_type>{}; break;
                    }
                    _columns.push_back(std::move(data));
                }
            }

            void append_row(std::initializer_list<table_cell> values)
            {
                if (values.size() != _columns.size())
                    throw std::logic_error("gbdb table row has wrong column count");

                std::size_t index = 0;
                for (const auto& value : values)
                    append_cell(_columns[index++], value);
                ++_row_count;
            }

            void append_row(std::span<const table_cell> values)
            {
                if (values.size() != _columns.size())
                    throw std::logic_error("gbdb table row has wrong column count");

                for (std::size_t i = 0; i < values.size(); ++i)
                    append_cell(_columns[i], values[i]);
                ++_row_count;
            }

            [[nodiscard]] std::uint32_t row_count() const noexcept
            {
                return _row_count;
            }

            [[nodiscard]] std::uint32_t column_count() const noexcept
            {
                return static_cast<std::uint32_t>(_columns.size());
            }

        private:
            struct column_data
            {
                string_type name;
                table_column_type type{};
                std::variant<
                    std::vector<std::int64_t>,
                    std::vector<std::uint64_t>,
                    std::vector<double>,
                    std::vector<string_type>> values;
            };

            static void append_cell(column_data& column, const table_cell& cell)
            {
                switch (column.type) {
                case table_column_type::int64:
                    std::get<std::vector<std::int64_t>>(column.values).push_back(as_int64(cell));
                    break;
                case table_column_type::uint64:
                    std::get<std::vector<std::uint64_t>>(column.values).push_back(as_uint64(cell));
                    break;
                case table_column_type::double_:
                    std::get<std::vector<double>>(column.values).push_back(as_double(cell));
                    break;
                case table_column_type::string:
                    std::get<std::vector<string_type>>(column.values).push_back(as_string(cell));
                    break;
                }
            }

            static std::int64_t as_int64(const table_cell& cell)
            {
                return std::visit([](const auto& value) -> std::int64_t {
                    using cell_type = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<cell_type, std::int64_t>)
                        return value;
                    else if constexpr (std::same_as<cell_type, std::uint64_t>) {
                        if (value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                            throw std::out_of_range("gbdb table uint64 cell does not fit int64 column");
                        return static_cast<std::int64_t>(value);
                    }
                    else
                        throw std::logic_error("gbdb table cell type does not match int64 column");
                }, cell.value);
            }

            static std::uint64_t as_uint64(const table_cell& cell)
            {
                return std::visit([](const auto& value) -> std::uint64_t {
                    using cell_type = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<cell_type, std::uint64_t>)
                        return value;
                    else if constexpr (std::same_as<cell_type, std::int64_t>) {
                        if (value < 0)
                            throw std::out_of_range("gbdb table negative int64 cell does not fit uint64 column");
                        return static_cast<std::uint64_t>(value);
                    }
                    else
                        throw std::logic_error("gbdb table cell type does not match uint64 column");
                }, cell.value);
            }

            static double as_double(const table_cell& cell)
            {
                return std::visit([](const auto& value) -> double {
                    using cell_type = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<cell_type, double>
                        || std::same_as<cell_type, std::int64_t>
                        || std::same_as<cell_type, std::uint64_t>)
                        return static_cast<double>(value);
                    else
                        throw std::logic_error("gbdb table cell type does not match double column");
                }, cell.value);
            }

            static string_type as_string(const table_cell& cell)
            {
                return std::visit([](const auto& value) -> string_type {
                    using cell_type = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<cell_type, string_type>)
                        return value;
                    else
                        throw std::logic_error("gbdb table cell type does not match string column");
                }, cell.value);
            }

            std::vector<column_data> _columns;
            std::uint32_t _row_count = 0;
        };

        class table_view
        {
            friend class basic_gbdb;

        public:
            [[nodiscard]] std::uint32_t row_count() const noexcept
            {
                return _record->row_count;
            }

            [[nodiscard]] std::uint32_t column_count() const noexcept
            {
                return static_cast<std::uint32_t>(_db->_table_columns.size(_record->columns));
            }

            [[nodiscard]] string_view column_name(std::uint32_t column) const
            {
                return _db->string(metadata(column).name);
            }

            [[nodiscard]] table_column_type column_type(std::uint32_t column) const
            {
                return metadata(column).type;
            }

            [[nodiscard]] std::span<const std::int64_t> int64_column(std::uint32_t column) const
            {
                auto& meta = require_column(column, table_column_type::int64);
                return _db->_int_arrays.span(meta.array);
            }

            [[nodiscard]] std::span<const std::uint64_t> uint64_column(std::uint32_t column) const
            {
                auto& meta = require_column(column, table_column_type::uint64);
                return _db->_uint_arrays.span(meta.array);
            }

            [[nodiscard]] std::span<const double> double_column(std::uint32_t column) const
            {
                auto& meta = require_column(column, table_column_type::double_);
                return _db->_double_arrays.span(meta.array);
            }

            [[nodiscard]] std::span<const string_id> string_column(std::uint32_t column) const
            {
                auto& meta = require_column(column, table_column_type::string);
                return _db->_string_arrays.span(meta.array);
            }

            [[nodiscard]] table_cell cell(std::uint32_t row, std::uint32_t column) const
            {
                if (row >= row_count())
                    throw std::out_of_range("gbdb table row is out of range");

                switch (column_type(column)) {
                case table_column_type::int64: return table_cell{ int64_column(column)[row] };
                case table_column_type::uint64: return table_cell{ uint64_column(column)[row] };
                case table_column_type::double_: return table_cell{ double_column(column)[row] };
                case table_column_type::string: return table_cell{ _db->string(string_column(column)[row]) };
                }
                throw std::logic_error("gbdb table column has invalid type");
            }

        private:
            table_view(const basic_gbdb& db, const table_record& record)
                : _db(std::addressof(db)), _record(std::addressof(record))
            {}

            [[nodiscard]] const table_column& metadata(std::uint32_t column) const
            {
                auto columns = _db->_table_columns.span(_record->columns);
                if (column >= columns.size())
                    throw std::out_of_range("gbdb table column is out of range");
                return columns[column];
            }

            [[nodiscard]] const table_column& require_column(std::uint32_t column, table_column_type type) const
            {
                auto& meta = metadata(column);
                if (meta.type != type)
                    throw std::logic_error("gbdb table column type mismatch");
                return meta;
            }

            const basic_gbdb* _db = nullptr;
            const table_record* _record = nullptr;
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
            blob_ref,
            object_ref,
            table_ref>;

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

        [[nodiscard]] node_id find(string_view path) const
        {
            auto parts = split_slash_path(path);
            return find(path_span{ parts });
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

        [[nodiscard]] bool contains(string_view path) const
        {
            auto parts = split_slash_path(path);
            return contains(path_span{ parts });
        }

        [[nodiscard]] bool contains(path_span path) const
        {
            return find(path) != invalid_node;
        }

        [[nodiscard]] const value_type* get(path_view path) const
        {
            return get(path_span{ path.begin(), path.size() });
        }

        [[nodiscard]] const value_type* get(string_view path) const
        {
            auto parts = split_slash_path(path);
            return get(path_span{ parts });
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

        [[nodiscard]] value_type* get(string_view path)
        {
            auto parts = split_slash_path(path);
            return get(path_span{ parts });
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

        node_id set(string_view path, value_type value)
        {
            auto parts = split_slash_path(path);
            return set(path_span{ parts }, std::move(value));
        }

        node_id set(path_span path, value_type value)
        {
            auto node = ensure_path(path);
            _tree.get_value(node).value = std::move(value);
            emit_log({
                .level = log_level::debug,
                .category = "db.operation",
                .event = "set",
                .path = path_to_string(path)
            });
            return node;
        }

        node_id set(path_view path, std::nullptr_t)
        {
            return set(path, value_type{ std::monostate{} });
        }

        node_id set(string_view path, std::nullptr_t)
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

        node_id set(string_view path, bool value)
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
        node_id set(string_view path, IntT value)
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

        node_id set(string_view path, float value)
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

        node_id set(string_view path, double value)
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

        node_id set(string_view path, const CharT* value)
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

        node_id set(string_view path, string_view value)
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

        node_id set(string_view path, const string_type& value)
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

        node_id set_array(string_view path, std::span<const std::int64_t> values)
        {
            auto parts = split_slash_path(path);
            return set_array(path_span{ parts }, values);
        }

        node_id set_array(path_span path, std::span<const std::int64_t> values)
        {
            return set(path, value_type{ int_array_ref{ _int_arrays.insert(values) } });
        }

        node_id set_array(path_view path, std::span<const std::uint64_t> values)
        {
            return set_array(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_array(string_view path, std::span<const std::uint64_t> values)
        {
            auto parts = split_slash_path(path);
            return set_array(path_span{ parts }, values);
        }

        node_id set_array(path_span path, std::span<const std::uint64_t> values)
        {
            return set(path, value_type{ uint_array_ref{ _uint_arrays.insert(values) } });
        }

        node_id set_array(path_view path, std::span<const double> values)
        {
            return set_array(path_span{ path.begin(), path.size() }, values);
        }

        node_id set_array(string_view path, std::span<const double> values)
        {
            auto parts = split_slash_path(path);
            return set_array(path_span{ parts }, values);
        }

        node_id set_array(path_span path, std::span<const double> values)
        {
            return set(path, value_type{ double_array_ref{ _double_arrays.insert(values) } });
        }

        node_id set_string_array(path_view path, std::initializer_list<string_view> values)
        {
            return set_string_array(path_span{ path.begin(), path.size() }, std::span<const string_view>{ values.begin(), values.size() });
        }

        node_id set_string_array(string_view path, std::initializer_list<string_view> values)
        {
            auto parts = split_slash_path(path);
            return set_string_array(path_span{ parts }, std::span<const string_view>{ values.begin(), values.size() });
        }

        node_id set_string_array(string_view path, std::span<const string_view> values)
        {
            auto parts = split_slash_path(path);
            return set_string_array(path_span{ parts }, values);
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

        node_id set_blob(string_view path, std::span<const std::byte> values)
        {
            auto parts = split_slash_path(path);
            return set_blob(path_span{ parts }, values);
        }

        node_id set_blob(path_span path, std::span<const std::byte> values)
        {
            return set(path, value_type{ blob_ref{ _blobs.insert(values) } });
        }

        node_id set_serialized_object(path_view path, std::span<const std::byte> values, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            return set_serialized_object(path_span{ path.begin(), path.size() }, values, type, version);
        }

        node_id set_serialized_object(string_view path, std::span<const std::byte> values, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            auto parts = split_slash_path(path);
            return set_serialized_object(path_span{ parts }, values, type, version);
        }

        node_id set_serialized_object(path_span path, std::span<const std::byte> values, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            if (_serialized_objects.size() == (std::numeric_limits<std::uint32_t>::max)())
                throw std::length_error("gbdb object_id capacity exceeded");

            object_record record;
            record.blob.assign(values.begin(), values.end());
            if (!type.empty())
                record.type = intern(type);
            if (version) {
                record.has_version = true;
                record.version = *version;
            }

            auto object_id = static_cast<std::uint32_t>(_serialized_objects.size());
            _serialized_objects.push_back(record);

            auto node = ensure_path(path);
            clear_children(node);
            _tree.get_value(node).value = value_type{ object_ref{ object_id } };
            return node;
        }

        node_id set_deferred_serialized_object(path_view path, string_view blob_uri, std::uint64_t blob_size_bytes, string_view blob_md5,
            string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            return set_deferred_serialized_object(path_span{ path.begin(), path.size() }, blob_uri, blob_size_bytes, blob_md5, type, version);
        }

        node_id set_deferred_serialized_object(string_view path, string_view blob_uri, std::uint64_t blob_size_bytes, string_view blob_md5,
            string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            auto parts = split_slash_path(path);
            return set_deferred_serialized_object(path_span{ parts }, blob_uri, blob_size_bytes, blob_md5, type, version);
        }

        node_id set_deferred_serialized_object(path_span path, string_view blob_uri, std::uint64_t blob_size_bytes, string_view blob_md5,
            string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            if (_serialized_objects.size() == (std::numeric_limits<std::uint32_t>::max)())
                throw std::length_error("gbdb object_id capacity exceeded");
            if (blob_uri.empty())
                throw std::invalid_argument("gbdb deferred object requires blob_uri");
            if (blob_md5.empty())
                throw std::invalid_argument("gbdb deferred object requires blob_md5");

            object_record record;
            record.storage = object_blob_storage::external_file;
            record.load_policy = blob_load_policy::deferred;
            record.blob_uri = intern(blob_uri);
            record.blob_size_bytes = blob_size_bytes;
            record.blob_md5 = intern(blob_md5);
            if (!type.empty())
                record.type = intern(type);
            if (version) {
                record.has_version = true;
                record.version = *version;
            }

            auto object_id = static_cast<std::uint32_t>(_serialized_objects.size());
            _serialized_objects.push_back(std::move(record));

            auto node = ensure_path(path);
            clear_children(node);
            _tree.get_value(node).value = value_type{ object_ref{ object_id } };
            return node;
        }

        template<class T>
        node_id set_object(path_view path, const T& value, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            return set_object(path_span{ path.begin(), path.size() }, value, type, version);
        }

        template<class T>
        node_id set_object(string_view path, const T& value, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            auto parts = split_slash_path(path);
            return set_object(path_span{ parts }, value, type, version);
        }

        template<class T>
        node_id set_object(path_span path, const T& value, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            gb::yadro::archive::omem_archive<> out;
            out(value);
            auto bytes = std::as_bytes(std::span{ out.get_stream().buffer() });
            return set_serialized_object(path, bytes, type, version);
        }

        [[nodiscard]] table_builder make_table(std::span<const table_schema_column> schema) const
        {
            return table_builder{ schema };
        }

        node_id set_table(path_view path, table_builder&& table)
        {
            return set_table(path_span{ path.begin(), path.size() }, std::move(table));
        }

        node_id set_table(string_view path, table_builder&& table)
        {
            auto parts = split_slash_path(path);
            return set_table(path_span{ parts }, std::move(table));
        }

        node_id set_table(path_span path, table_builder&& table)
        {
            std::vector<table_column> columns;
            columns.reserve(table._columns.size());

            for (auto& column : table._columns) {
                table_column meta;
                meta.name = intern(column.name);
                meta.type = column.type;

                switch (column.type) {
                case table_column_type::int64:
                    meta.array = _int_arrays.insert(std::get<std::vector<std::int64_t>>(column.values));
                    break;
                case table_column_type::uint64:
                    meta.array = _uint_arrays.insert(std::get<std::vector<std::uint64_t>>(column.values));
                    break;
                case table_column_type::double_:
                    meta.array = _double_arrays.insert(std::get<std::vector<double>>(column.values));
                    break;
                case table_column_type::string: {
                    std::vector<string_id> ids;
                    auto& values = std::get<std::vector<string_type>>(column.values);
                    ids.reserve(values.size());
                    for (auto& value : values)
                        ids.push_back(intern(value));
                    meta.array = _string_arrays.insert(ids);
                    break;
                }
                }
                columns.push_back(meta);
            }

            auto columns_id = _table_columns.insert(columns);
            if (_tables.size() == (std::numeric_limits<std::uint32_t>::max)())
                throw std::length_error("gbdb table_id capacity exceeded");
            auto table_id = static_cast<std::uint32_t>(_tables.size());
            _tables.push_back(table_record{ .row_count = table._row_count, .columns = columns_id });
            return set(path, value_type{ table_ref{ table_id } });
        }

        node_id set_table(path_view path, table_view table)
        {
            return set_table(path_span{ path.begin(), path.size() }, table);
        }

        node_id set_table(string_view path, table_view table)
        {
            auto parts = split_slash_path(path);
            return set_table(path_span{ parts }, table);
        }

        node_id set_table(path_span path, table_view table)
        {
            std::vector<table_schema_column> schema;
            schema.reserve(table.column_count());
            for (std::uint32_t column = 0; column < table.column_count(); ++column)
                schema.push_back(table_schema_column{ table.column_name(column), table.column_type(column) });

            auto builder = make_table(schema);
            std::vector<table_cell> row;
            row.reserve(table.column_count());
            for (std::uint32_t r = 0; r < table.row_count(); ++r) {
                row.clear();
                for (std::uint32_t c = 0; c < table.column_count(); ++c)
                    row.push_back(table.cell(r, c));
                builder.append_row(row);
            }
            return set_table(path, std::move(builder));
        }

        [[nodiscard]] table_view table(table_ref ref) const
        {
            if (ref.id >= _tables.size())
                throw std::out_of_range("gbdb table_ref is out of range");
            return table_view{ *this, _tables[ref.id] };
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

        [[nodiscard]] std::span<const std::byte> serialized_object(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (metadata.storage == object_blob_storage::inline_memory)
                return metadata.blob;

            load_object(ref);
            return metadata.external_cache;
        }

        void set_external_blob_base_directory(std::filesystem::path path)
        {
            _external_blob_base_directory = std::move(path);
        }

        [[nodiscard]] const std::filesystem::path& external_blob_base_directory() const noexcept
        {
            return _external_blob_base_directory;
        }

        [[nodiscard]] bool is_object_deferred(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            return metadata.storage == object_blob_storage::external_file && metadata.load_policy == blob_load_policy::deferred;
        }

        [[nodiscard]] bool is_object_loaded(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            return metadata.storage == object_blob_storage::inline_memory || metadata.external_loaded;
        }

        [[nodiscard]] std::optional<external_blob_info> object_external_blob(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (metadata.storage != object_blob_storage::external_file)
                return std::nullopt;

            return external_blob_info{
                .uri = string(metadata.blob_uri),
                .size_bytes = metadata.blob_size_bytes,
                .md5 = string(metadata.blob_md5),
                .load_policy = metadata.load_policy,
                .loaded = metadata.external_loaded
            };
        }

        [[nodiscard]] log_options_type log_options() const noexcept
        {
            return _log_options;
        }

        void set_log_options(log_options_type options) noexcept
        {
            _log_options = options;
        }

        void add_log_sink(log_sink sink)
        {
            if (!sink)
                throw std::invalid_argument("gbdb log sink must be callable");
            _log_sinks.push_back(std::move(sink));
        }

        void add_log_file(const std::filesystem::path& file)
        {
            auto stream = std::make_shared<std::ofstream>(file, std::ios::binary | std::ios::app);
            if (!*stream)
                throw std::runtime_error("Failed to open gbdb log file");

            _log_file_streams.push_back(stream);
            add_log_sink([stream](const log_event& event) {
                *stream << basic_gbdb::log_event_to_json(event) << '\n';
                stream->flush();
            });
        }

        void clear_log_sinks() noexcept
        {
            _log_sinks.clear();
            _log_file_streams.clear();
        }

        void log(log_event event) const noexcept
        {
            emit_log(event);
        }

        [[nodiscard]] scan_report scan_database(scan_options options = {}) const
        {
            scan_report report;
            report.stats.node_count = _node_count;
            report.stats.string_count = _strings.string_count();
            report.stats.int_array_count = _int_arrays.array_count();
            report.stats.uint_array_count = _uint_arrays.array_count();
            report.stats.double_array_count = _double_arrays.array_count();
            report.stats.string_array_count = _string_arrays.array_count();
            report.stats.blob_count = _blobs.array_count();
            report.stats.object_count = _serialized_objects.size();
            report.stats.table_count = _tables.size();

            std::vector<bool> visited(_node_count, false);
            std::vector<string_type> path;

            auto add_issue = [&](scan_severity severity, std::string category, const std::string& issue_path, std::string message) {
                add_scan_issue(report, options, severity, std::move(category), issue_path, std::move(message));
            };

            auto valid_string = [&](string_id id) noexcept {
                return id < _strings.string_count();
            };

            auto validate_string = [&](string_id id, const std::string& issue_path, std::string_view role) {
                if (valid_string(id))
                    return true;

                add_issue(scan_severity::error, "reference", issue_path, std::string{ role } + " string_id is out of range");
                return false;
            };

            auto validate_table = [&](table_ref ref, const std::string& issue_path) {
                if (ref.id >= _tables.size()) {
                    add_issue(scan_severity::error, "reference", issue_path, "table_ref is out of range");
                    return;
                }

                if (options.mode == scan_mode::quick)
                    return;

                const auto& table = _tables[ref.id];
                if (table.columns >= _table_columns.array_count()) {
                    add_issue(scan_severity::error, "table", issue_path, "table column metadata is out of range");
                    return;
                }

                for (const auto& column : _table_columns.span(table.columns)) {
                    validate_string(column.name, issue_path, "table column name");
                    switch (column.type) {
                    case table_column_type::int64:
                        if (column.array >= _int_arrays.array_count())
                            add_issue(scan_severity::error, "table", issue_path, "table int64 column array is out of range");
                        else if (_int_arrays.size(column.array) != table.row_count)
                            add_issue(scan_severity::error, "table", issue_path, "table int64 column row count mismatch");
                        break;
                    case table_column_type::uint64:
                        if (column.array >= _uint_arrays.array_count())
                            add_issue(scan_severity::error, "table", issue_path, "table uint64 column array is out of range");
                        else if (_uint_arrays.size(column.array) != table.row_count)
                            add_issue(scan_severity::error, "table", issue_path, "table uint64 column row count mismatch");
                        break;
                    case table_column_type::double_:
                        if (column.array >= _double_arrays.array_count())
                            add_issue(scan_severity::error, "table", issue_path, "table double column array is out of range");
                        else if (_double_arrays.size(column.array) != table.row_count)
                            add_issue(scan_severity::error, "table", issue_path, "table double column row count mismatch");
                        break;
                    case table_column_type::string:
                        if (column.array >= _string_arrays.array_count()) {
                            add_issue(scan_severity::error, "table", issue_path, "table string column array is out of range");
                        }
                        else {
                            auto values = _string_arrays.span(column.array);
                            if (values.size() != table.row_count)
                                add_issue(scan_severity::error, "table", issue_path, "table string column row count mismatch");
                            for (auto value_id : values)
                                validate_string(value_id, issue_path, "table cell");
                        }
                        break;
                    }
                }
            };

            auto validate_external_blob = [&](const object_record& metadata, const std::string& issue_path) {
                if (metadata.storage != object_blob_storage::external_file)
                    return;

                ++report.stats.external_blob_count;
                if (options.mode == scan_mode::quick)
                    return;

                auto has_uri = validate_string(metadata.blob_uri, issue_path, "external blob uri");
                auto has_md5 = validate_string(metadata.blob_md5, issue_path, "external blob md5");
                if (!has_uri || !has_md5)
                    return;

                std::filesystem::path file;
                try {
                    file = resolve_external_blob_uri(string(metadata.blob_uri));
                }
                catch (const std::exception& e) {
                    add_issue(scan_severity::error, "external_blob", issue_path, e.what());
                    return;
                }

                std::error_code ec;
                if (!std::filesystem::exists(file, ec) || ec) {
                    add_issue(scan_severity::error, "external_blob", issue_path, "external blob file does not exist");
                    return;
                }

                auto file_size = std::filesystem::file_size(file, ec);
                if (ec) {
                    add_issue(scan_severity::error, "external_blob", issue_path, "external blob file size cannot be read");
                    return;
                }

                if (file_size != metadata.blob_size_bytes) {
                    add_issue(scan_severity::error, "external_blob", issue_path, "external blob file size mismatch");
                    return;
                }

                if (options.mode != scan_mode::deep && !options.check_external_blob_md5)
                    return;

                std::ifstream in(file, std::ios::binary);
                if (!in) {
                    add_issue(scan_severity::error, "external_blob", issue_path, "external blob file cannot be opened");
                    return;
                }

                std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
                auto bytes = std::as_bytes(std::span{ data });
                if (md5_bytes(bytes) != string(metadata.blob_md5))
                    add_issue(scan_severity::error, "blob.integrity", issue_path, "external blob MD5 mismatch");
            };

            auto scan_value = [&](const value_type& value, const std::string& issue_path) {
                std::visit([&](const auto& item) {
                    using item_type = std::remove_cvref_t<decltype(item)>;
                    if constexpr (std::same_as<item_type, std::monostate>) {
                        ++report.stats.null_value_count;
                    }
                    else if constexpr (std::same_as<item_type, bool>) {
                        ++report.stats.value_count;
                        ++report.stats.bool_value_count;
                    }
                    else if constexpr (std::same_as<item_type, std::int64_t>) {
                        ++report.stats.value_count;
                        ++report.stats.int_value_count;
                    }
                    else if constexpr (std::same_as<item_type, std::uint64_t>) {
                        ++report.stats.value_count;
                        ++report.stats.uint_value_count;
                    }
                    else if constexpr (std::same_as<item_type, double>) {
                        ++report.stats.value_count;
                        ++report.stats.double_value_count;
                    }
                    else if constexpr (std::same_as<item_type, string_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.string_value_count;
                        validate_string(item.id, issue_path, "value");
                    }
                    else if constexpr (std::same_as<item_type, int_array_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.int_array_value_count;
                        if (item.id >= _int_arrays.array_count())
                            add_issue(scan_severity::error, "reference", issue_path, "int_array_ref is out of range");
                    }
                    else if constexpr (std::same_as<item_type, uint_array_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.uint_array_value_count;
                        if (item.id >= _uint_arrays.array_count())
                            add_issue(scan_severity::error, "reference", issue_path, "uint_array_ref is out of range");
                    }
                    else if constexpr (std::same_as<item_type, double_array_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.double_array_value_count;
                        if (item.id >= _double_arrays.array_count())
                            add_issue(scan_severity::error, "reference", issue_path, "double_array_ref is out of range");
                    }
                    else if constexpr (std::same_as<item_type, string_array_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.string_array_value_count;
                        if (item.id >= _string_arrays.array_count()) {
                            add_issue(scan_severity::error, "reference", issue_path, "string_array_ref is out of range");
                        }
                        else {
                            for (auto string_id : _string_arrays.span(item.id))
                                validate_string(string_id, issue_path, "string array value");
                        }
                    }
                    else if constexpr (std::same_as<item_type, blob_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.blob_value_count;
                        if (item.id >= _blobs.array_count())
                            add_issue(scan_severity::error, "reference", issue_path, "blob_ref is out of range");
                    }
                    else if constexpr (std::same_as<item_type, object_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.object_value_count;
                        if (item.id >= _serialized_objects.size()) {
                            add_issue(scan_severity::error, "reference", issue_path, "object_ref is out of range");
                        }
                        else {
                            const auto& metadata = _serialized_objects[item.id];
                            if (metadata.type != invalid_string)
                                validate_string(metadata.type, issue_path, "object type");
                            validate_external_blob(metadata, issue_path);
                        }
                    }
                    else if constexpr (std::same_as<item_type, table_ref>) {
                        ++report.stats.value_count;
                        ++report.stats.table_value_count;
                        validate_table(item, issue_path);
                    }
                }, value);
            };

            std::function<bool(node_id, std::uint64_t)> scan_node = [&](node_id node, std::uint64_t depth) -> bool {
                if (node >= _node_count) {
                    add_issue(scan_severity::critical, "tree", scan_path_to_string(path), "node index is out of range");
                    return false;
                }

                if (visited[node]) {
                    add_issue(scan_severity::critical, "tree", scan_path_to_string(path), "tree cycle or duplicate child reference detected");
                    return false;
                }

                visited[node] = true;
                ++report.stats.reachable_node_count;
                report.stats.max_depth = (std::max)(report.stats.max_depth, depth);

                const auto& payload = _tree.get_value(node);
                auto issue_path = scan_path_to_string(path);
                if (!validate_string(payload.key, issue_path, "node key"))
                    return !options.stop_on_first_error;

                scan_value(payload.value, issue_path);
                if (options.stop_on_first_error && !report.ok())
                    return false;

                if (options.progress) {
                    if (!options.progress(scan_progress{
                        .scanned_nodes = report.stats.reachable_node_count,
                        .total_nodes = report.stats.node_count,
                        .path = issue_path })) {
                        report.cancelled = true;
                        return false;
                    }
                }

                for (auto child = _tree.get_child(node); child != invalid_node; child = _tree.get_sibling(child)) {
                    if (child >= _node_count) {
                        add_issue(scan_severity::critical, "tree", issue_path, "child node index is out of range");
                        return false;
                    }

                    const auto& child_payload = _tree.get_value(child);
                    if (valid_string(child_payload.key))
                        path.push_back(string_type{ string(child_payload.key) });
                    else
                        path.push_back({});

                    auto keep_going = scan_node(child, depth + 1);
                    path.pop_back();
                    if (!keep_going)
                        return false;
                }

                return true;
            };

            scan_node(root_node, 0);

            if (!report.cancelled) {
                for (node_id node = 0; node < _node_count; ++node)
                    if (!visited[node])
                        ++report.stats.orphaned_node_count;
            }

            return report;
        }

        void load_object(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (metadata.storage == object_blob_storage::inline_memory || metadata.external_loaded)
                return;

            try {
                metadata.external_cache = load_external_object_blob(metadata);
                metadata.external_loaded = true;
            }
            catch (const std::exception& e) {
                emit_log({
                    .level = log_level::critical,
                    .category = "blob.integrity",
                    .event = "load_failed",
                    .message = e.what()
                });
                throw;
            }
            catch (...) {
                emit_log({
                    .level = log_level::critical,
                    .category = "blob.integrity",
                    .event = "load_failed",
                    .message = "Unknown gbdb external object blob load failure"
                });
                throw;
            }
        }

        void unload_object(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (metadata.storage != object_blob_storage::external_file)
                return;

            metadata.external_cache.clear();
            metadata.external_cache.shrink_to_fit();
            metadata.external_loaded = false;
        }

        void mark_object_immediate(object_ref ref)
        {
            auto& metadata = mutable_object_metadata(ref);
            if (metadata.storage == object_blob_storage::inline_memory)
                return;

            load_object(ref);
            metadata.blob = metadata.external_cache;
            metadata.external_cache.clear();
            metadata.external_cache.shrink_to_fit();
            metadata.external_loaded = false;
            metadata.storage = object_blob_storage::inline_memory;
            metadata.load_policy = blob_load_policy::immediate;
            metadata.blob_uri = invalid_string;
            metadata.blob_size_bytes = 0;
            metadata.blob_md5 = invalid_string;
        }

        void mark_object_deferred(object_ref ref, string_view blob_uri)
        {
            auto& metadata = mutable_object_metadata(ref);
            auto bytes = serialized_object(ref);
            std::vector<std::byte> cache(bytes.begin(), bytes.end());
            auto size_bytes = static_cast<std::uint64_t>(bytes.size());
            auto digest = md5_bytes(bytes);
            write_external_object_blob(blob_uri, bytes);

            metadata.external_cache = std::move(cache);
            metadata.external_loaded = true;
            metadata.blob.clear();
            metadata.blob.shrink_to_fit();
            metadata.storage = object_blob_storage::external_file;
            metadata.load_policy = blob_load_policy::deferred;
            metadata.blob_uri = intern(blob_uri);
            metadata.blob_size_bytes = size_bytes;
            metadata.blob_md5 = intern(digest);
        }

        [[nodiscard]] std::optional<string_view> object_type(object_ref ref) const
        {
            auto type = object_metadata(ref).type;
            if (type == (std::numeric_limits<string_id>::max)())
                return std::nullopt;
            return string(type);
        }

        [[nodiscard]] std::optional<std::uint32_t> object_version(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (!metadata.has_version)
                return std::nullopt;
            return metadata.version;
        }

        template<std::default_initializable T>
        [[nodiscard]] T object(object_ref ref) const
        {
            T value{};
            object(ref, value);
            return value;
        }

        template<class T>
        void object(object_ref ref, T& value) const
        {
            auto bytes = serialized_object(ref);
            std::vector<char> buffer(bytes.size());
            std::ranges::transform(bytes, buffer.begin(), [](std::byte value) {
                return static_cast<char>(std::to_integer<unsigned char>(value));
            });

            gb::yadro::archive::imem_archive<> in(std::move(buffer));
            in(value);
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
                archive(self._serialized_objects);
                self.load_array_pool(archive, self._table_columns);
                archive(self._tables);
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
                archive(self._serialized_objects);
                self.save_array_pool(archive, self._table_columns);
                archive(self._tables);
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

        [[nodiscard]] static std::vector<string_view> split_slash_path(string_view path)
        {
            std::vector<string_view> parts;
            if (path.empty())
                return parts;

            std::size_t begin = 0;
            while (begin < path.size()) {
                auto end = path.find(CharT{ '/' }, begin);
                if (end == begin)
                    throw std::invalid_argument("gbdb slash path contains an empty component");

                if (end == string_view::npos) {
                    parts.push_back(path.substr(begin));
                    return parts;
                }

                parts.push_back(path.substr(begin, end - begin));
                begin = end + 1;
                if (begin == path.size())
                    throw std::invalid_argument("gbdb slash path contains an empty component");
            }

            return parts;
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

        [[nodiscard]] const object_record& object_metadata(object_ref ref) const
        {
            if (ref.id >= _serialized_objects.size())
                throw std::out_of_range("gbdb object_ref is out of range");
            return _serialized_objects[ref.id];
        }

        [[nodiscard]] object_record& mutable_object_metadata(object_ref ref)
        {
            if (ref.id >= _serialized_objects.size())
                throw std::out_of_range("gbdb object_ref is out of range");
            return _serialized_objects[ref.id];
        }

        [[nodiscard]] std::filesystem::path resolve_external_blob_uri(string_view blob_uri) const
        {
            std::filesystem::path file{ string_type{ blob_uri } };
            if (file.is_relative()) {
                if (_external_blob_base_directory.empty())
                    throw std::runtime_error("gbdb external object blob requires a base directory");
                file = _external_blob_base_directory / file;
            }
            return file;
        }

        [[nodiscard]] std::vector<std::byte> load_external_object_blob(const object_record& metadata) const
        {
            auto file = resolve_external_blob_uri(string(metadata.blob_uri));

            std::ifstream in(file, std::ios::binary);
            if (!in)
                throw std::runtime_error("Failed to open gbdb external object blob");

            std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
            if (static_cast<std::uint64_t>(data.size()) != metadata.blob_size_bytes)
                throw std::runtime_error("gbdb external object blob size mismatch");

            auto bytes = std::as_bytes(std::span{ data });
            if (md5_bytes(bytes) != string(metadata.blob_md5))
                throw std::runtime_error("gbdb external object blob MD5 mismatch");

            std::vector<std::byte> result;
            result.reserve(data.size());
            for (auto value : data)
                result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
            return result;
        }

        [[nodiscard]] static constexpr bool is_serious(log_level level) noexcept
        {
            return level == log_level::error || level == log_level::critical;
        }

        [[nodiscard]] static constexpr bool is_warning(log_level level) noexcept
        {
            return level == log_level::warning;
        }

        [[nodiscard]] static constexpr bool is_regular(log_level level) noexcept
        {
            return level == log_level::trace || level == log_level::debug || level == log_level::info || level == log_level::audit;
        }

        [[nodiscard]] bool should_log(log_level level) const noexcept
        {
            // Serious failures bypass filters so configured sinks receive them even when diagnostic logging is off.
            if (is_serious(level))
                return true;
            if (is_warning(level))
                return _log_options.log_warnings && level >= _log_options.min_level;
            if (is_regular(level))
                return _log_options.log_regular_operations && level >= _log_options.min_level;
            return false;
        }

        void emit_log(const log_event& event) const noexcept
        {
            if (event.force) {
                for (const auto& sink : _log_sinks) {
                    try {
                        sink(event);
                    }
                    catch (...) {
                    }
                }
                return;
            }

            if (!should_log(event.level))
                return;

            for (const auto& sink : _log_sinks) {
                try {
                    sink(event);
                }
                catch (...) {
                    // Logging must not change database operation semantics.
                }
            }
        }

        [[nodiscard]] static constexpr log_level scan_issue_log_level(scan_severity severity) noexcept
        {
            switch (severity) {
            case scan_severity::info: return log_level::info;
            case scan_severity::warning: return log_level::warning;
            case scan_severity::error: return log_level::error;
            case scan_severity::critical: return log_level::critical;
            }
            return log_level::error;
        }

        static void append_scan_issue(scan_report& report, const scan_options& options, scan_severity severity,
            std::string category, const std::string& path, std::string message)
        {
            switch (severity) {
            case scan_severity::warning:
                ++report.stats.warning_count;
                break;
            case scan_severity::error:
                ++report.stats.error_count;
                break;
            case scan_severity::critical:
                ++report.stats.critical_count;
                break;
            case scan_severity::info:
                break;
            }

            if (options.collect_paths) {
                report.issues.push_back(scan_issue{
                    .severity = severity,
                    .category = std::move(category),
                    .path = path,
                    .message = std::move(message)
                });
            }
            else {
                report.issues.push_back(scan_issue{
                    .severity = severity,
                    .category = std::move(category),
                    .message = std::move(message)
                });
            }
        }

        void add_scan_issue(scan_report& report, const scan_options& options, scan_severity severity,
            std::string category, const std::string& path, std::string message) const
        {
            auto logged_category = category;
            auto logged_message = message;
            basic_gbdb::append_scan_issue(report, options, severity, std::move(category), path, std::move(message));

            if (!options.log_findings)
                return;

            emit_log({
                .level = scan_issue_log_level(severity),
                .category = "db.scan",
                .event = "issue",
                .path = path,
                .message = logged_category + ": " + logged_message,
                .force = true
            });
        }

        [[nodiscard]] static std::string scan_path_to_string(std::span<const string_type> path)
        {
            std::string result;
            for (auto& part : path) {
                if (!result.empty())
                    result.push_back('/');
                append_narrow(result, string_view{ part });
            }
            return result;
        }

        [[nodiscard]] std::string path_to_string(path_span path) const
        {
            std::string result;
            for (auto part : path) {
                if (!result.empty())
                    result.push_back('/');
                append_narrow(result, part);
            }
            return result;
        }

        static void append_narrow(std::string& result, string_view text)
        {
            if constexpr (std::same_as<CharT, char>) {
                result.append(text.begin(), text.end());
            }
            else {
                for (auto ch : text)
                    result.push_back(static_cast<char>(ch));
            }
        }

        [[nodiscard]] static std::string_view log_level_name(log_level level) noexcept
        {
            switch (level) {
            case log_level::trace: return "trace";
            case log_level::debug: return "debug";
            case log_level::info: return "info";
            case log_level::audit: return "audit";
            case log_level::warning: return "warning";
            case log_level::error: return "error";
            case log_level::critical: return "critical";
            }
            return "unknown";
        }

        static void append_json_string(std::string& result, std::string_view text)
        {
            static constexpr char hex[] = "0123456789abcdef";

            result.push_back('"');
            for (auto ch : text) {
                auto value = static_cast<unsigned char>(ch);
                switch (value) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (value < 0x20) {
                        result += "\\u00";
                        result.push_back(hex[value >> 4]);
                        result.push_back(hex[value & 0x0f]);
                    }
                    else {
                        result.push_back(static_cast<char>(value));
                    }
                    break;
                }
            }
            result.push_back('"');
        }

        [[nodiscard]] static std::string log_event_to_json(const log_event& event)
        {
            std::string result;
            result.reserve(event.category.size() + event.event.size() + event.path.size() + event.message.size() + 96);
            result.push_back('{');
            result += R"("level":)";
            append_json_string(result, log_level_name(event.level));
            result += R"(,"category":)";
            append_json_string(result, event.category);
            result += R"(,"event":)";
            append_json_string(result, event.event);
            result += R"(,"path":)";
            append_json_string(result, event.path);
            result += R"(,"message":)";
            append_json_string(result, event.message);
            result.push_back('}');
            return result;
        }

        [[nodiscard]] static std::string md5_bytes(std::span<const std::byte> bytes)
        {
            gb::yadro::util::md5 hash;
            hash.update(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
            return hash.finalize().to_string();
        }

        void write_external_object_blob(string_view blob_uri, std::span<const std::byte> bytes)
        {
            auto file = resolve_external_blob_uri(blob_uri);
            if (auto parent = file.parent_path(); !parent.empty())
                std::filesystem::create_directories(parent);

            std::ofstream out(file, std::ios::binary);
            if (!out)
                throw std::runtime_error("Failed to open gbdb external object blob for writing");
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out)
                throw std::runtime_error("Failed to write gbdb external object blob");
        }

        void clear_children(node_id node)
        {
            if (_tree.get_child(node) == invalid_node)
                return;

            for (auto child = _tree.get_child(node); child != invalid_node; child = _tree.get_child(node))
                _tree.delete_subtree(child);
            rebuild_indexes();
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
            _serialized_objects.clear();
            _external_blob_base_directory.clear();
            _table_columns = duplicate_data_pool<table_column>{};
            _tables.clear();
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
        std::vector<object_record> _serialized_objects;
        std::filesystem::path _external_blob_base_directory;
        log_options_type _log_options;
        std::vector<log_sink> _log_sinks;
        std::vector<std::shared_ptr<std::ofstream>> _log_file_streams;
        duplicate_data_pool<table_column> _table_columns;
        std::vector<table_record> _tables;
        tree_type _tree;
        std::unordered_map<child_key, node_id, child_key_hash> _child_index;
        node_id _node_count = 1;
    };

    using json_db = basic_gbdb<char>;

    template<class CharT = char>
    class basic_concurrent_gbdb
    {
    public:
        using db_type = basic_gbdb<CharT>;
        using snapshot_type = std::shared_ptr<const db_type>;

        basic_concurrent_gbdb()
            : _snapshot(std::make_shared<const db_type>())
        {}

        explicit basic_concurrent_gbdb(db_type db)
            : _snapshot(std::make_shared<const db_type>(std::move(db)))
        {}

        [[nodiscard]] snapshot_type snapshot() const noexcept
        {
            return _snapshot.load(std::memory_order_acquire);
        }

        void replace(db_type db)
        {
            std::scoped_lock lock(_writer_mutex);
            _snapshot.store(std::make_shared<const db_type>(std::move(db)), std::memory_order_release);
        }

        template<class Function>
        decltype(auto) update(Function&& function)
        {
            using result_type = std::invoke_result_t<Function&&, db_type&>;

            std::scoped_lock lock(_writer_mutex);
            auto next = std::make_shared<db_type>(*snapshot());

            if constexpr (std::is_void_v<result_type>) {
                std::invoke(std::forward<Function>(function), *next);
                _snapshot.store(std::shared_ptr<const db_type>{ std::move(next) }, std::memory_order_release);
            }
            else {
                result_type result = std::invoke(std::forward<Function>(function), *next);
                _snapshot.store(std::shared_ptr<const db_type>{ std::move(next) }, std::memory_order_release);
                return result;
            }
        }

    private:
        std::atomic<snapshot_type> _snapshot;
        std::mutex _writer_mutex;
    };

    using concurrent_json_db = basic_concurrent_gbdb<char>;

    void print_color_report(const json_db::scan_report& rep, std::ostream& os);
    void print_json_report(const json_db::scan_report& rep, std::ostream& os);  
}
