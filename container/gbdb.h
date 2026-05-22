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
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

        node_id set_serialized_object(path_view path, std::span<const std::byte> values, string_view type = {}, std::optional<std::uint32_t> version = std::nullopt)
        {
            return set_serialized_object(path_span{ path.begin(), path.size() }, values, type, version);
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

        void load_object(object_ref ref) const
        {
            auto& metadata = object_metadata(ref);
            if (metadata.storage == object_blob_storage::inline_memory || metadata.external_loaded)
                return;

            metadata.external_cache = load_external_object_blob(metadata);
            metadata.external_loaded = true;
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
        duplicate_data_pool<table_column> _table_columns;
        std::vector<table_record> _tables;
        tree_type _tree;
        std::unordered_map<child_key, node_id, child_key_hash> _child_index;
        node_id _node_count = 1;
    };

    using json_db = basic_gbdb<char>;
}
