//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#pragma once

#include "gbdb_json.h"

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gb::yadro::container
{
    [[nodiscard]] inline std::vector<std::string_view> split_json_path(std::string_view normalized_path);

    namespace detail
    {
        [[nodiscard]] inline std::string escape_json_key(std::string_view text)
        {
            std::string result;
            result.reserve(text.size() + 2);
            for (auto ch : text) {
                switch (ch) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20)
                        throw std::invalid_argument("gbdb JSON path contains a control character");
                    result.push_back(ch);
                    break;
                }
            }
            return result;
        }

        [[nodiscard]] inline std::string json_string(std::string_view text)
        {
            return "\"" + escape_json_key(text) + "\"";
        }

        template<class T>
        [[nodiscard]] inline std::string numeric_array_json(std::span<const T> values)
        {
            std::string result = "[";
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0)
                    result.push_back(',');
                if constexpr (std::floating_point<T>) {
                    std::ostringstream out;
                    out << std::setprecision(17) << values[i];
                    result += out.str();
                }
                else {
                    result += std::to_string(values[i]);
                }
            }
            result.push_back(']');
            return result;
        }

        [[nodiscard]] inline std::string value_json(const json_db& db, const json_db::value_type& value)
        {
            if (std::holds_alternative<std::monostate>(value))
                return "null";
            if (auto typed = std::get_if<bool>(&value))
                return *typed ? "true" : "false";
            if (auto typed = std::get_if<std::int64_t>(&value))
                return std::to_string(*typed);
            if (auto typed = std::get_if<std::uint64_t>(&value))
                return std::to_string(*typed);
            if (auto typed = std::get_if<double>(&value)) {
                std::ostringstream out;
                out << std::setprecision(17) << *typed;
                return out.str();
            }
            if (auto ref = std::get_if<json_db::string_ref>(&value))
                return json_string(db.string(*ref));
            if (auto ref = std::get_if<json_db::int_array_ref>(&value))
                return numeric_array_json(db.array(*ref));
            if (auto ref = std::get_if<json_db::uint_array_ref>(&value))
                return numeric_array_json(db.array(*ref));
            if (auto ref = std::get_if<json_db::double_array_ref>(&value))
                return numeric_array_json(db.array(*ref));
            if (auto ref = std::get_if<json_db::string_array_ref>(&value)) {
                auto values = db.array(*ref);
                std::string result = "[";
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (i != 0)
                        result.push_back(',');
                    result += json_string(db.string(values[i]));
                }
                result.push_back(']');
                return result;
            }
            throw std::runtime_error("gbdb value cannot be exported as a scalar JSON value");
        }

        [[nodiscard]] inline bool has_children(const json_db& db, json_db::node_id node)
        {
            return db.tree().get_child(node) != json_db::invalid_node;
        }

        inline void write_indent(std::ostream& out, std::uint32_t level, std::uint32_t indent)
        {
            for (std::uint32_t i = 0; i < level * indent; ++i)
                out << ' ';
        }

        inline void write_node_json(std::ostream& out, const json_db& db, json_db::node_id node,
            const json_write_options& options, std::uint32_t level);

        inline void write_object_json(std::ostream& out, const json_db& db, json_db::node_id node,
            const json_write_options& options, std::uint32_t level)
        {
            out << '{';
            if (options.pretty)
                out << '\n';

            bool wrote_member = false;
            for (auto child = db.tree().get_child(node); child != json_db::invalid_node; child = db.tree().get_sibling(child)) {
                if (wrote_member) {
                    out << ',';
                    if (options.pretty)
                        out << '\n';
                }
                if (options.pretty)
                    write_indent(out, level + 1, options.indent);
                out << json_string(db.key(child)) << ':';
                if (options.pretty)
                    out << ' ';
                write_node_json(out, db, child, options, level + 1);
                wrote_member = true;
            }

            if (wrote_member && options.pretty) {
                out << '\n';
                write_indent(out, level, options.indent);
            }
            out << '}';
        }

        inline void write_node_json(std::ostream& out, const json_db& db, json_db::node_id node,
            const json_write_options& options, std::uint32_t level)
        {
            auto& value = db.value(node);
            if (has_children(db, node)) {
                if (!std::holds_alternative<std::monostate>(value))
                    throw std::logic_error("JSON writer cannot represent a node with both a value and child members");
                write_object_json(out, db, node, options, level);
                return;
            }

            out << value_json(db, value);
        }

        [[nodiscard]] inline std::string wrap_json_at_path(std::string_view normalized_path, std::string_view json_text)
        {
            if (normalized_path.empty())
                return std::string{ json_text };

            auto parts = split_json_path(normalized_path);

            std::string result;
            for (auto part : parts) {
                result += "{\"";
                result += escape_json_key(part);
                result += "\":";
            }
            result += json_text;
            result.append(parts.size(), '}');
            return result;
        }
    }

    [[nodiscard]] inline std::string normalize_json_path(std::string_view path)
    {
        std::string normalized{ path };
        std::ranges::replace(normalized, '\\', '/');

        std::string_view view{ normalized };
        while (!view.empty() && view.front() == '/')
            view.remove_prefix(1);
        while (!view.empty() && view.back() == '/')
            view.remove_suffix(1);

        if (view.find("//") != std::string_view::npos)
            throw std::invalid_argument("gbdb JSON path contains an empty component");

        return std::string{ view };
    }

    [[nodiscard]] inline std::vector<std::string_view> split_json_path(std::string_view normalized_path)
    {
        std::vector<std::string_view> parts;
        if (normalized_path.empty())
            return parts;

        std::size_t begin = 0;
        while (begin < normalized_path.size()) {
            auto end = normalized_path.find('/', begin);
            if (end == begin)
                throw std::invalid_argument("gbdb JSON path contains an empty component");

            if (end == std::string_view::npos) {
                parts.push_back(normalized_path.substr(begin));
                return parts;
            }

            parts.push_back(normalized_path.substr(begin, end - begin));
            begin = end + 1;
            if (begin == normalized_path.size())
                throw std::invalid_argument("gbdb JSON path contains an empty component");
        }

        return parts;
    }

    [[nodiscard]] inline std::string write_json_path(const json_db& db, std::string_view path,
        const json_write_options& options = {})
    {
        auto normalized_path = normalize_json_path(path);
        if (normalized_path.empty())
            return write_json(db, options);

        auto parts = split_json_path(normalized_path);
        auto node = db.find(std::span<const std::string_view>{ parts });
        if (node == json_db::invalid_node)
            throw std::runtime_error("gbdb JSON path not found: " + normalized_path);

        if (auto& value = db.value(node); !detail::has_children(db, node) && !std::holds_alternative<std::monostate>(value))
            return detail::value_json(db, value);

        std::ostringstream out;
        out << '{';
        if (options.pretty) {
            out << '\n';
            detail::write_indent(out, 1, options.indent);
        }
        out << detail::json_string(parts.back()) << ':';
        if (options.pretty)
            out << ' ';
        detail::write_node_json(out, db, node, options, 1);
        if (options.pretty) {
            out << '\n';
            detail::write_indent(out, 0, options.indent);
        }
        out << '}';
        return std::move(out).str();
    }

    inline void write_json_path(std::ostream& out, const json_db& db, std::string_view path,
        const json_write_options& options = {})
    {
        detail::write_stream(out, write_json_path(db, path, options));
    }

    inline void insert_json_at_path(json_db& target, std::string_view path, std::string_view text,
        json_merge_policy policy = json_merge_policy::replace_existing, const json_read_options& options = {})
    {
        auto wrapped = detail::wrap_json_at_path(normalize_json_path(path), text);
        insert_json(target, read_json(wrapped, options), policy);
    }

    inline void insert_json_file_at_path(json_db& target, std::string_view path, const std::filesystem::path& file,
        json_merge_policy policy = json_merge_policy::replace_existing, const json_read_options& options = {})
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            throw std::runtime_error("failed to open JSON import file: " + file.string());
        insert_json_at_path(target, path, detail::read_stream(in), policy, options);
    }

    [[nodiscard]] inline std::optional<std::string> get_json_string(const json_db& db, json_db::path_view path)
    {
        if (auto* value = db.get(path)) {
            if (auto* ref = std::get_if<json_db::string_ref>(value))
                return std::string{ db.string(*ref) };
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::uint64_t> get_json_uint(const json_db& db, json_db::path_view path)
    {
        if (auto* value = db.get(path)) {
            if (auto* typed = std::get_if<std::uint64_t>(value))
                return *typed;
            if (auto* signed_value = std::get_if<std::int64_t>(value); signed_value && *signed_value >= 0)
                return static_cast<std::uint64_t>(*signed_value);
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<bool> get_json_bool(const json_db& db, json_db::path_view path)
    {
        if (auto* value = db.get(path)) {
            if (auto* typed = std::get_if<bool>(value))
                return *typed;
        }
        return std::nullopt;
    }
}
