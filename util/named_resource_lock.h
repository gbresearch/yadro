//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2026, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//-----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef GBWINDOWS
#include <windows.h>
#endif

namespace gb::yadro::util
{
    [[nodiscard]] inline std::filesystem::path normalized_resource_path(const std::filesystem::path& path)
    {
        std::error_code ec;
        auto absolute = std::filesystem::absolute(path, ec);
        if (ec) {
            ec.clear();
            absolute = path;
        }

        auto normalized = std::filesystem::weakly_canonical(absolute, ec);
        if (ec)
            normalized = absolute.lexically_normal();
        return normalized.lexically_normal();
    }

    [[nodiscard]] inline std::string comparable_resource_path(const std::filesystem::path& path)
    {
        auto text = normalized_resource_path(path).generic_string();
#ifdef _WIN32
        std::ranges::transform(text, text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
#endif
        return text;
    }

    [[nodiscard]] inline std::uint64_t fnv1a_64(std::string_view text) noexcept
    {
        auto hash = 14695981039346656037ull;
        for (auto ch : text) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] inline std::string named_resource_id(std::string_view scope, const std::filesystem::path& resource)
    {
        std::ostringstream name;
        name << scope << '_' << std::hex << std::setw(16) << std::setfill('0')
            << fnv1a_64(comparable_resource_path(resource));
        return name.str();
    }

    class named_resource_lock
    {
    public:
        named_resource_lock() noexcept = default;
        ~named_resource_lock() { reset(); }

        named_resource_lock(const named_resource_lock&) = delete;
        named_resource_lock& operator=(const named_resource_lock&) = delete;

        named_resource_lock(named_resource_lock&& other) noexcept
            : _handle(std::exchange(other._handle, nullptr))
        {}

        named_resource_lock& operator=(named_resource_lock&& other) noexcept
        {
            if (this != &other) {
                reset();
                _handle = std::exchange(other._handle, nullptr);
            }
            return *this;
        }

        [[nodiscard]] static named_resource_lock acquire(std::string_view scope, const std::filesystem::path& resource)
        {
            if (resource.empty())
                return {};

#ifdef GBWINDOWS
            auto id = named_resource_id(scope, resource);
            auto name = std::wstring{ id.begin(), id.end() };
            auto* handle = CreateMutexW(nullptr, TRUE, name.c_str());
            if (!handle)
                throw std::runtime_error("failed to create named resource lock: " + id);

            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                CloseHandle(handle);
                throw std::runtime_error("named resource is already locked: "
                    + normalized_resource_path(resource).generic_string());
            }

            return named_resource_lock{ handle };
#else
            throw std::runtime_error("named_resource_lock is not implemented on this platform");
#endif
        }

        [[nodiscard]] explicit operator bool() const noexcept { return _handle != nullptr; }

    private:
        explicit named_resource_lock(void* handle) noexcept
            : _handle(handle)
        {}

        void reset() noexcept
        {
            if (!_handle)
                return;
#ifdef GBWINDOWS
            auto* handle = static_cast<HANDLE>(_handle);
            ReleaseMutex(handle);
            CloseHandle(handle);
#endif
            _handle = nullptr;
        }

        void* _handle = nullptr;
    };

    [[nodiscard]] inline bool is_named_resource_locked(std::string_view scope, const std::filesystem::path& resource)
    {
        if (resource.empty())
            return false;

#ifdef GBWINDOWS
        auto id = named_resource_id(scope, resource);
        auto name = std::wstring{ id.begin(), id.end() };
        auto* handle = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
        if (!handle)
            return false;

        CloseHandle(handle);
        return true;
#else
        return false;
#endif
    }
}
