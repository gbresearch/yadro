//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#pragma once

#include "gbdb.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <Windows.h>
#endif

namespace gb::yadro::container::registry
{
#if defined(_WIN32)
    enum class registry_value_type
    {
        string,
        expand_string,
        dword,
        qword,
        binary,
        multi_string
    };

    enum class registry_type_policy
    {
        fail_unsupported,
        skip_unsupported
    };

    enum class registry_collision_policy
    {
        fail_existing,
        overwrite
    };

    struct registry_error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct registry_value
    {
        registry_value_type type = registry_value_type::string;
        std::u16string utf16;
        std::vector<std::u16string> multi_utf16;
        std::vector<std::byte> binary;
        std::uint32_t dword_value = 0;
        std::uint64_t qword_value = 0;

        [[nodiscard]] static registry_value string(std::u16string value)
        {
            registry_value result;
            result.type = registry_value_type::string;
            result.utf16 = std::move(value);
            return result;
        }

        [[nodiscard]] static registry_value expand_string(std::u16string value)
        {
            registry_value result;
            result.type = registry_value_type::expand_string;
            result.utf16 = std::move(value);
            return result;
        }

        [[nodiscard]] static registry_value dword(std::uint32_t value)
        {
            registry_value result;
            result.type = registry_value_type::dword;
            result.dword_value = value;
            return result;
        }

        [[nodiscard]] static registry_value qword(std::uint64_t value)
        {
            registry_value result;
            result.type = registry_value_type::qword;
            result.qword_value = value;
            return result;
        }

        [[nodiscard]] static registry_value binary_value(std::span<const std::byte> value)
        {
            registry_value result;
            result.type = registry_value_type::binary;
            result.binary.assign(value.begin(), value.end());
            return result;
        }

        [[nodiscard]] static registry_value multi_string(std::vector<std::u16string> value)
        {
            registry_value result;
            result.type = registry_value_type::multi_string;
            result.multi_utf16 = std::move(value);
            return result;
        }
    };

    struct registry_read_options
    {
        bool mark_imported = true;
        registry_type_policy type_policy = registry_type_policy::fail_unsupported;
        registry_collision_policy collision_policy = registry_collision_policy::fail_existing;
        std::string metadata_key = "$registry";
        std::string default_value_name = "@default";
    };

    struct registry_write_options
    {
        registry_collision_policy collision_policy = registry_collision_policy::fail_existing;
        registry_type_policy type_policy = registry_type_policy::fail_unsupported;
        bool dry_run = false;
        bool log_operations = true;
        std::string metadata_key = "$registry";
        std::string default_value_name = "@default";
    };

    struct registry_import_result
    {
        std::uint64_t keys_read = 0;
        std::uint64_t values_read = 0;
        std::uint64_t values_skipped = 0;
        std::uint32_t max_depth = 0;
    };

    struct registry_write_result
    {
        std::uint64_t keys_planned = 0;
        std::uint64_t values_planned = 0;
        std::uint64_t values_written = 0;
        std::uint64_t values_skipped = 0;
    };

    [[nodiscard]] inline std::u16string utf8_to_utf16(std::string_view text)
    {
        if (text.empty())
            return {};

        auto needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (needed <= 0)
            throw registry_error("Invalid UTF-8 text");

        std::wstring wide(static_cast<std::size_t>(needed), L'\0');
        auto written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), needed);
        if (written != needed)
            throw registry_error("Failed to convert UTF-8 text to UTF-16");

        std::u16string result;
        result.reserve(wide.size());
        for (auto ch : wide)
            result.push_back(static_cast<char16_t>(ch));
        return result;
    }

    [[nodiscard]] inline std::string utf16_to_utf8(std::u16string_view text)
    {
        if (text.empty())
            return {};

        std::wstring wide;
        wide.reserve(text.size());
        for (auto ch : text)
            wide.push_back(static_cast<wchar_t>(ch));

        auto needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            throw registry_error("Invalid UTF-16 text");

        std::string result(static_cast<std::size_t>(needed), '\0');
        auto written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), result.data(), needed, nullptr, nullptr);
        if (written != needed)
            throw registry_error("Failed to convert UTF-16 text to UTF-8");
        return result;
    }

    [[nodiscard]] inline std::string registry_value_type_name(registry_value_type type)
    {
        switch (type) {
        case registry_value_type::string: return "sz";
        case registry_value_type::expand_string: return "expand_sz";
        case registry_value_type::dword: return "dword";
        case registry_value_type::qword: return "qword";
        case registry_value_type::binary: return "binary";
        case registry_value_type::multi_string: return "multi_sz";
        }
        return "unknown";
    }

    [[nodiscard]] inline registry_value_type registry_value_type_from_name(std::string_view name)
    {
        if (name == "sz")
            return registry_value_type::string;
        if (name == "expand_sz")
            return registry_value_type::expand_string;
        if (name == "dword")
            return registry_value_type::dword;
        if (name == "qword")
            return registry_value_type::qword;
        if (name == "binary")
            return registry_value_type::binary;
        if (name == "multi_sz")
            return registry_value_type::multi_string;
        throw registry_error("Unknown registry value type metadata");
    }

    [[nodiscard]] inline std::string registry_type_policy_name(registry_type_policy policy)
    {
        return policy == registry_type_policy::fail_unsupported ? "fail_unsupported" : "skip_unsupported";
    }

    [[nodiscard]] inline std::string registry_collision_policy_name(registry_collision_policy policy)
    {
        return policy == registry_collision_policy::fail_existing ? "fail_existing" : "overwrite";
    }

    [[nodiscard]] inline std::vector<std::u16string> split_registry_path(std::u16string_view path)
    {
        std::vector<std::u16string> result;
        std::size_t first = 0;
        while (first <= path.size()) {
            auto last = path.find(u'\\', first);
            if (last == std::u16string_view::npos)
                last = path.size();
            if (last != first)
                result.emplace_back(path.substr(first, last - first));
            if (last == path.size())
                break;
            first = last + 1;
        }
        return result;
    }

    [[nodiscard]] inline std::u16string join_registry_path(std::u16string_view parent, std::u16string_view child)
    {
        if (parent.empty())
            return std::u16string{ child };
        if (child.empty())
            return std::u16string{ parent };
        std::u16string result{ parent };
        result.push_back(u'\\');
        result.append(child);
        return result;
    }

    class mock_registry_backend
    {
    public:
        void create_key(std::u16string_view key)
        {
            _keys.emplace(std::u16string{ key });
        }

        [[nodiscard]] bool key_exists(std::u16string_view key) const
        {
            return _keys.contains(std::u16string{ key });
        }

        void set_value(std::u16string_view key, std::u16string_view name, registry_value value)
        {
            create_key(key);
            _values[std::u16string{ key }][std::u16string{ name }] = std::move(value);
        }

        [[nodiscard]] bool value_exists(std::u16string_view key, std::u16string_view name) const
        {
            auto found = _values.find(std::u16string{ key });
            return found != _values.end() && found->second.contains(std::u16string{ name });
        }

        [[nodiscard]] std::optional<registry_value> value(std::u16string_view key, std::u16string_view name) const
        {
            auto found = _values.find(std::u16string{ key });
            if (found == _values.end())
                return std::nullopt;
            auto value_found = found->second.find(std::u16string{ name });
            if (value_found == found->second.end())
                return std::nullopt;
            return value_found->second;
        }

        [[nodiscard]] std::vector<std::pair<std::u16string, registry_value>> values(std::u16string_view key) const
        {
            std::vector<std::pair<std::u16string, registry_value>> result;
            auto found = _values.find(std::u16string{ key });
            if (found == _values.end())
                return result;

            result.reserve(found->second.size());
            for (auto& [name, value] : found->second)
                result.emplace_back(name, value);
            return result;
        }

        [[nodiscard]] std::vector<std::u16string> subkeys(std::u16string_view key) const
        {
            std::vector<std::u16string> result;
            std::unordered_set<std::u16string> unique;
            std::u16string prefix{ key };
            if (!prefix.empty())
                prefix.push_back(u'\\');

            for (auto& candidate : _keys) {
                if (candidate.size() <= prefix.size() || candidate.compare(0, prefix.size(), prefix) != 0)
                    continue;

                auto rest = std::u16string_view{ candidate }.substr(prefix.size());
                auto separator = rest.find(u'\\');
                auto child = std::u16string{ rest.substr(0, separator) };
                if (unique.insert(child).second)
                    result.push_back(std::move(child));
            }
            return result;
        }

    private:
        std::unordered_set<std::u16string> _keys;
        std::unordered_map<std::u16string, std::unordered_map<std::u16string, registry_value>> _values;
    };

    namespace detail
    {
        inline constexpr std::string_view schema_version = "1";

        [[nodiscard]] inline std::string utc_timestamp()
        {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm utc{};
            gmtime_s(&utc, &now);

            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
            return buffer;
        }

        [[nodiscard]] inline std::vector<json_db::string_view> path_views(const std::vector<std::string>& path)
        {
            std::vector<json_db::string_view> result;
            result.reserve(path.size());
            for (auto& part : path)
                result.push_back(part);
            return result;
        }

        [[nodiscard]] inline std::string path_to_string(const std::vector<std::string>& path)
        {
            std::string result;
            for (auto& part : path) {
                if (!result.empty())
                    result.push_back('/');
                result += part;
            }
            return result;
        }

        inline void log_registry(const json_db& db, json_db::log_level level, std::string category, std::string event,
            std::string path = {}, std::string message = {}, bool force = true)
        {
            db.log({
                .level = level,
                .category = std::move(category),
                .event = std::move(event),
                .path = std::move(path),
                .message = std::move(message),
                .force = force
            });
        }

        [[nodiscard]] inline bool is_metadata_key(std::string_view key, const std::string& metadata_key)
        {
            return key == metadata_key;
        }

        inline void set_path_value(json_db& db, const std::vector<std::string>& path, json_db::value_type value)
        {
            auto views = path_views(path);
            db.set(std::span<const json_db::string_view>{ views.data(), views.size() }, std::move(value));
        }

        template<class T>
        inline void set_path(json_db& db, const std::vector<std::string>& path, T&& value)
        {
            auto views = path_views(path);
            db.set(std::span<const json_db::string_view>{ views.data(), views.size() }, std::forward<T>(value));
        }

        inline void set_registry_value_type(json_db& db, const std::vector<std::string>& path, registry_value_type type,
            const std::string& metadata_key)
        {
            auto metadata_path = path;
            metadata_path.push_back(metadata_key);
            metadata_path.push_back("type");
            set_path(db, metadata_path, registry_value_type_name(type));
            if (type == registry_value_type::expand_string) {
                metadata_path.back() = "is_expand";
                set_path(db, metadata_path, true);
            }
        }

        [[nodiscard]] inline std::optional<registry_value_type> registry_value_type_metadata(const json_db& db,
            const std::vector<std::string>& path, const std::string& metadata_key)
        {
            auto metadata_path = path;
            metadata_path.push_back(metadata_key);
            metadata_path.push_back("type");
            auto views = path_views(metadata_path);
            auto value = db.get(std::span<const json_db::string_view>{ views.data(), views.size() });
            if (!value)
                return std::nullopt;
            auto ref = std::get_if<json_db::string_ref>(value);
            if (!ref)
                throw registry_error("Registry type metadata must be a string");
            return registry_value_type_from_name(db.string(*ref));
        }

        [[nodiscard]] inline bool has_db_value(const json_db::value_type& value)
        {
            return !std::holds_alternative<std::monostate>(value);
        }

        [[nodiscard]] inline registry_value make_registry_value(const json_db& db, const std::vector<std::string>& path,
            const json_db::value_type& value, registry_value_type type)
        {
            switch (type) {
            case registry_value_type::string:
                if (auto ref = std::get_if<json_db::string_ref>(&value))
                    return registry_value::string(utf8_to_utf16(db.string(*ref)));
                break;
            case registry_value_type::expand_string:
                if (auto ref = std::get_if<json_db::string_ref>(&value))
                    return registry_value::expand_string(utf8_to_utf16(db.string(*ref)));
                break;
            case registry_value_type::dword:
                if (auto numeric = std::get_if<std::uint64_t>(&value)) {
                    if (*numeric > (std::numeric_limits<std::uint32_t>::max)())
                        throw registry_error("Registry DWORD value is out of range: " + path_to_string(path));
                    return registry_value::dword(static_cast<std::uint32_t>(*numeric));
                }
                break;
            case registry_value_type::qword:
                if (auto numeric = std::get_if<std::uint64_t>(&value))
                    return registry_value::qword(*numeric);
                break;
            case registry_value_type::binary:
                if (auto ref = std::get_if<json_db::blob_ref>(&value))
                    return registry_value::binary_value(db.blob(*ref));
                break;
            case registry_value_type::multi_string:
                if (auto ref = std::get_if<json_db::string_array_ref>(&value)) {
                    std::vector<std::u16string> values;
                    for (auto id : db.array(*ref))
                        values.push_back(utf8_to_utf16(db.string(id)));
                    return registry_value::multi_string(std::move(values));
                }
                break;
            }
            throw registry_error("Unsupported DB value for requested registry type: " + path_to_string(path));
        }

        [[nodiscard]] inline registry_value_type infer_registry_type(const json_db& db, const std::vector<std::string>& path,
            const json_db::value_type& value, const std::string& metadata_key)
        {
            if (auto metadata = registry_value_type_metadata(db, path, metadata_key))
                return *metadata;

            if (std::holds_alternative<json_db::string_ref>(value))
                return registry_value_type::string;
            if (auto numeric = std::get_if<std::uint64_t>(&value))
                return *numeric <= (std::numeric_limits<std::uint32_t>::max)() ? registry_value_type::dword : registry_value_type::qword;
            if (std::holds_alternative<json_db::blob_ref>(value))
                return registry_value_type::binary;
            if (std::holds_alternative<json_db::string_array_ref>(value))
                return registry_value_type::multi_string;
            throw registry_error("Unsupported DB value for registry export: " + path_to_string(path));
        }

        struct planned_write
        {
            std::u16string key;
            std::u16string name;
            registry_value value;
            std::string path;
        };

        template<class Backend>
        void collect_writes(const json_db& db, Backend& backend, json_db::node_id node, std::u16string_view registry_key,
            std::vector<std::string>& db_path, const registry_write_options& options, std::vector<planned_write>& writes,
            registry_write_result& result)
        {
            auto key = std::string{ db.key(node) };
            if (is_metadata_key(key, options.metadata_key))
                return;

            db_path.push_back(key);
            auto value = db.value(node);
            if (has_db_value(value)) {
                auto registry_name = key == options.default_value_name ? std::u16string{} : utf8_to_utf16(key);
                if (backend.value_exists(registry_key, registry_name) && options.collision_policy == registry_collision_policy::fail_existing)
                    throw registry_error("Registry value collision: " + path_to_string(db_path));

                auto type = infer_registry_type(db, db_path, value, options.metadata_key);
                writes.push_back(planned_write{
                    .key = std::u16string{ registry_key },
                    .name = std::move(registry_name),
                    .value = make_registry_value(db, db_path, value, type),
                    .path = path_to_string(db_path)
                });
                ++result.values_planned;
            }

            auto child_key = join_registry_path(registry_key, utf8_to_utf16(key));
            db.tree().foreach_child(node, [&](auto child) {
                collect_writes(db, backend, child, child_key, db_path, options, writes, result);
                return true;
            });
            db_path.pop_back();
        }
    }

    inline void set_registry_value_type(json_db& db, std::span<const json_db::string_view> path, registry_value_type type,
        std::string_view metadata_key = "$registry")
    {
        std::vector<std::string> owned_path;
        owned_path.reserve(path.size());
        for (auto part : path)
            owned_path.emplace_back(part);
        detail::set_registry_value_type(db, owned_path, type, std::string{ metadata_key });
    }

    inline void set_registry_value_type(json_db& db, json_db::path_view path, registry_value_type type,
        std::string_view metadata_key = "$registry")
    {
        set_registry_value_type(db, std::span<const json_db::string_view>{ path.begin(), path.size() }, type, metadata_key);
    }

    template<class Backend>
    registry_import_result read_registry_tree(json_db& db, Backend& backend, std::u16string_view root,
        const registry_read_options& options = {})
    {
        registry_import_result result;
        detail::log_registry(db, json_db::log_level::audit, "registry.read", "begin", utf16_to_utf8(root), {}, true);

        if (options.mark_imported) {
            db.set({ options.metadata_key, "schema_version" }, std::string{ detail::schema_version });
            db.set({ options.metadata_key, "imported" }, true);
            db.set({ options.metadata_key, "imported_at_utc" }, detail::utc_timestamp());
            db.set({ options.metadata_key, "root" }, utf16_to_utf8(root));
            db.set({ options.metadata_key, "registry_type_policy" }, registry_type_policy_name(options.type_policy));
            db.set({ options.metadata_key, "registry_collision_policy" }, registry_collision_policy_name(options.collision_policy));
        }

        auto read_key = [&](auto&& self, std::u16string current_key, std::vector<std::string>& db_path, std::uint32_t depth) -> void {
            ++result.keys_read;
            result.max_depth = (std::max)(result.max_depth, depth);

            for (auto& [name16, value] : backend.values(current_key)) {
                auto value_name = name16.empty() ? options.default_value_name : utf16_to_utf8(name16);
                auto value_path = db_path;
                value_path.push_back(value_name);
                auto views = detail::path_views(value_path);

                if (db.get(std::span<const json_db::string_view>{ views.data(), views.size() })
                    && options.collision_policy == registry_collision_policy::fail_existing) {
                    throw registry_error("DB value collision during registry import: " + detail::path_to_string(value_path));
                }

                switch (value.type) {
                case registry_value_type::string:
                case registry_value_type::expand_string:
                    detail::set_path(db, value_path, utf16_to_utf8(value.utf16));
                    break;
                case registry_value_type::dword:
                    detail::set_path_value(db, value_path, json_db::value_type{ static_cast<std::uint64_t>(value.dword_value) });
                    break;
                case registry_value_type::qword:
                    detail::set_path_value(db, value_path, json_db::value_type{ value.qword_value });
                    break;
                case registry_value_type::binary:
                    db.set_blob(std::span<const json_db::string_view>{ views.data(), views.size() }, value.binary);
                    break;
                case registry_value_type::multi_string: {
                    std::vector<std::string> strings;
                    strings.reserve(value.multi_utf16.size());
                    for (auto& item : value.multi_utf16)
                        strings.push_back(utf16_to_utf8(item));
                    std::vector<json_db::string_view> string_views;
                    string_views.reserve(strings.size());
                    for (auto& item : strings)
                        string_views.push_back(item);
                    db.set_string_array(std::span<const json_db::string_view>{ views.data(), views.size() },
                        std::span<const json_db::string_view>{ string_views.data(), string_views.size() });
                    break;
                }
                }

                detail::set_registry_value_type(db, value_path, value.type, options.metadata_key);
                ++result.values_read;
            }

            for (auto& subkey : backend.subkeys(current_key)) {
                db_path.push_back(utf16_to_utf8(subkey));
                self(self, join_registry_path(current_key, subkey), db_path, depth + 1);
                db_path.pop_back();
            }
        };

        std::vector<std::string> path;
        read_key(read_key, std::u16string{ root }, path, 0);
        detail::log_registry(db, json_db::log_level::audit, "registry.read", "completed", utf16_to_utf8(root), {}, true);
        return result;
    }

    template<class Backend>
    registry_write_result write_registry_tree(const json_db& db, Backend& backend, std::u16string_view root,
        const registry_write_options& options = {})
    {
        registry_write_result result;
        std::vector<detail::planned_write> writes;
        detail::log_registry(db, json_db::log_level::audit, "registry.write", "begin", utf16_to_utf8(root), {}, options.log_operations);

        try {
            std::vector<std::string> path;
            db.tree().foreach_child(json_db::root_node, [&](auto child) {
                detail::collect_writes(db, backend, child, root, path, options, writes, result);
                return true;
            });

            if (!options.dry_run) {
                backend.create_key(root);
                for (auto& write : writes) {
                    backend.set_value(write.key, write.name, std::move(write.value));
                    ++result.values_written;
                }
            }

            detail::log_registry(db, json_db::log_level::audit, "registry.write", options.dry_run ? "dry_run_completed" : "completed",
                utf16_to_utf8(root), {}, options.log_operations);
            return result;
        }
        catch (const registry_error& e) {
            auto category = std::string{ std::string_view{ e.what() }.find("collision") != std::string_view::npos ? "registry.collision" : "registry.type" };
            detail::log_registry(db, json_db::log_level::error, category, "failed", utf16_to_utf8(root), e.what(), true);
            throw;
        }
        catch (const std::exception& e) {
            detail::log_registry(db, json_db::log_level::error, "registry.error", "failed", utf16_to_utf8(root), e.what(), true);
            throw;
        }
    }

    class win32_registry_backend
    {
    public:
        explicit win32_registry_backend(HKEY root) : _root(root) {}

        void create_key(std::u16string_view key)
        {
            HKEY handle{};
            auto status = ::RegCreateKeyExW(_root, to_wide(key).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_WRITE, nullptr, &handle, nullptr);
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to create registry key");
            ::RegCloseKey(handle);
        }

        [[nodiscard]] bool key_exists(std::u16string_view key) const
        {
            HKEY handle{};
            auto status = ::RegOpenKeyExW(_root, to_wide(key).c_str(), 0, KEY_READ, &handle);
            if (status == ERROR_SUCCESS) {
                ::RegCloseKey(handle);
                return true;
            }
            return false;
        }

        void set_value(std::u16string_view key, std::u16string_view name, registry_value value)
        {
            HKEY handle{};
            auto status = ::RegCreateKeyExW(_root, to_wide(key).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE, nullptr, &handle, nullptr);
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to open registry key for writing");

            auto close_handle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>{ handle, &::RegCloseKey };
            auto wide_name = to_wide(name);
            switch (value.type) {
            case registry_value_type::string:
            case registry_value_type::expand_string: {
                auto data = to_wide(value.utf16);
                data.push_back(L'\0');
                auto type = value.type == registry_value_type::expand_string ? REG_EXPAND_SZ : REG_SZ;
                status = ::RegSetValueExW(handle, wide_name.c_str(), 0, type,
                    reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size() * sizeof(wchar_t)));
                break;
            }
            case registry_value_type::dword:
                status = ::RegSetValueExW(handle, wide_name.c_str(), 0, REG_DWORD,
                    reinterpret_cast<const BYTE*>(&value.dword_value), sizeof(value.dword_value));
                break;
            case registry_value_type::qword:
                status = ::RegSetValueExW(handle, wide_name.c_str(), 0, REG_QWORD,
                    reinterpret_cast<const BYTE*>(&value.qword_value), sizeof(value.qword_value));
                break;
            case registry_value_type::binary:
                status = ::RegSetValueExW(handle, wide_name.c_str(), 0, REG_BINARY,
                    reinterpret_cast<const BYTE*>(value.binary.data()), static_cast<DWORD>(value.binary.size()));
                break;
            case registry_value_type::multi_string: {
                std::wstring data;
                for (auto& item : value.multi_utf16) {
                    data += to_wide(item);
                    data.push_back(L'\0');
                }
                data.push_back(L'\0');
                status = ::RegSetValueExW(handle, wide_name.c_str(), 0, REG_MULTI_SZ,
                    reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size() * sizeof(wchar_t)));
                break;
            }
            }

            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to write registry value");
        }

        [[nodiscard]] bool value_exists(std::u16string_view key, std::u16string_view name) const
        {
            HKEY handle{};
            auto status = ::RegOpenKeyExW(_root, to_wide(key).c_str(), 0, KEY_QUERY_VALUE, &handle);
            if (status != ERROR_SUCCESS)
                return false;

            auto close_handle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>{ handle, &::RegCloseKey };
            status = ::RegQueryValueExW(handle, to_wide(name).c_str(), nullptr, nullptr, nullptr, nullptr);
            return status == ERROR_SUCCESS;
        }

        [[nodiscard]] std::optional<registry_value> value(std::u16string_view key, std::u16string_view name) const
        {
            HKEY handle{};
            auto status = ::RegOpenKeyExW(_root, to_wide(key).c_str(), 0, KEY_QUERY_VALUE, &handle);
            if (status != ERROR_SUCCESS)
                return std::nullopt;

            auto close_handle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>{ handle, &::RegCloseKey };
            return query_value(handle, to_wide(name));
        }

        [[nodiscard]] std::vector<std::pair<std::u16string, registry_value>> values(std::u16string_view key) const
        {
            std::vector<std::pair<std::u16string, registry_value>> result;
            HKEY handle{};
            auto status = ::RegOpenKeyExW(_root, to_wide(key).c_str(), 0, KEY_QUERY_VALUE, &handle);
            if (status != ERROR_SUCCESS)
                return result;

            auto close_handle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>{ handle, &::RegCloseKey };
            DWORD value_count{};
            DWORD max_name_length{};
            status = ::RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                &value_count, &max_name_length, nullptr, nullptr, nullptr);
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to enumerate registry values");

            std::wstring name(max_name_length + 2, L'\0');
            for (DWORD index = 0; index < value_count; ++index) {
                DWORD name_length = static_cast<DWORD>(name.size());
                status = ::RegEnumValueW(handle, index, name.data(), &name_length, nullptr, nullptr, nullptr, nullptr);
                if (status != ERROR_SUCCESS)
                    throw registry_error("Failed to enumerate registry value name");

                auto value_name = std::wstring{ name.data(), name_length };
                auto data = query_value(handle, value_name);
                if (data)
                    result.emplace_back(from_wide(value_name), std::move(*data));
            }
            return result;
        }

        [[nodiscard]] std::vector<std::u16string> subkeys(std::u16string_view key) const
        {
            std::vector<std::u16string> result;
            HKEY handle{};
            auto status = ::RegOpenKeyExW(_root, to_wide(key).c_str(), 0, KEY_READ, &handle);
            if (status != ERROR_SUCCESS)
                return result;

            auto close_handle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>{ handle, &::RegCloseKey };
            DWORD subkey_count{};
            DWORD max_name_length{};
            status = ::RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkey_count, &max_name_length,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to query registry subkey information: " + std::to_string(status));

            std::wstring name(max_name_length + 2, L'\0');
            for (DWORD index = 0; index < subkey_count; ++index) {
                DWORD name_length = static_cast<DWORD>(name.size());
                status = ::RegEnumKeyExW(handle, index, name.data(), &name_length, nullptr, nullptr, nullptr, nullptr);
                if (status != ERROR_SUCCESS)
                    throw registry_error("Failed to enumerate registry subkey name");
                result.push_back(from_wide(std::wstring_view{ name.data(), name_length }));
            }
            return result;
        }

    private:
        [[nodiscard]] static std::wstring to_wide(std::u16string_view value)
        {
            std::wstring result;
            result.reserve(value.size());
            for (auto ch : value)
                result.push_back(static_cast<wchar_t>(ch));
            return result;
        }

        [[nodiscard]] static std::u16string from_wide(std::wstring_view value)
        {
            std::u16string result;
            result.reserve(value.size());
            for (auto ch : value)
                result.push_back(static_cast<char16_t>(ch));
            return result;
        }

        [[nodiscard]] static registry_value parse_value(DWORD type, std::span<const std::byte> data)
        {
            switch (type) {
            case REG_SZ:
            case REG_EXPAND_SZ: {
                auto chars = std::wstring_view{
                    reinterpret_cast<const wchar_t*>(data.data()),
                    data.size() / sizeof(wchar_t)
                };
                while (!chars.empty() && chars.back() == L'\0')
                    chars.remove_suffix(1);
                return type == REG_EXPAND_SZ ? registry_value::expand_string(from_wide(chars)) : registry_value::string(from_wide(chars));
            }
            case REG_DWORD:
                if (data.size() < sizeof(std::uint32_t))
                    throw registry_error("Invalid REG_DWORD size");
                {
                    std::uint32_t value{};
                    std::memcpy(&value, data.data(), sizeof(value));
                    return registry_value::dword(value);
                }
            case REG_QWORD:
                if (data.size() < sizeof(std::uint64_t))
                    throw registry_error("Invalid REG_QWORD size");
                {
                    std::uint64_t value{};
                    std::memcpy(&value, data.data(), sizeof(value));
                    return registry_value::qword(value);
                }
            case REG_BINARY:
                return registry_value::binary_value(data);
            case REG_MULTI_SZ: {
                std::vector<std::u16string> strings;
                auto chars = std::wstring_view{
                    reinterpret_cast<const wchar_t*>(data.data()),
                    data.size() / sizeof(wchar_t)
                };
                std::size_t first = 0;
                while (first < chars.size() && chars[first] != L'\0') {
                    auto last = chars.find(L'\0', first);
                    if (last == std::wstring_view::npos)
                        last = chars.size();
                    strings.push_back(from_wide(chars.substr(first, last - first)));
                    first = last + 1;
                }
                return registry_value::multi_string(std::move(strings));
            }
            default:
                throw registry_error("Unsupported registry value type");
            }
        }

        [[nodiscard]] static std::optional<registry_value> query_value(HKEY handle, const std::wstring& name)
        {
            DWORD type{};
            DWORD byte_count{};
            auto status = ::RegQueryValueExW(handle, name.c_str(), nullptr, &type, nullptr, &byte_count);
            if (status == ERROR_FILE_NOT_FOUND)
                return std::nullopt;
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to query registry value size");

            std::vector<std::byte> data(byte_count);
            status = ::RegQueryValueExW(handle, name.c_str(), nullptr, &type,
                reinterpret_cast<BYTE*>(data.data()), &byte_count);
            if (status != ERROR_SUCCESS)
                throw registry_error("Failed to read registry value");
            data.resize(byte_count);
            return parse_value(type, data);
        }

        HKEY _root = nullptr;
    };
#endif
}
