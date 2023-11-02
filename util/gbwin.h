#pragma once

// windows utilities

#if defined(_Windows) || defined(__WIN32__) || defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include <functional>
#include <filesystem>
#include "gberror.h"
#include <Windows.h>
#include <Rpcdce.h>
#undef min
#undef max

#pragma comment(lib, "Rpcrt4")

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    struct dll
    {
        dll() = default;
        dll(const char* dll_name)
        {
            gbassert(dll_name);
            _handle = LoadLibraryA(dll_name);
        }
        dll(const wchar_t* dll_name)
        {
            gbassert(dll_name);
            _handle = LoadLibraryW(dll_name);
        }
#if defined(__cplusplus) && (__cplusplus >= 201103L)
        dll(dll&& other) noexcept : _handle(other._handle) { other._handle = nullptr; }
        dll& operator = (dll&& other) noexcept
        {
            _handle = other._handle;
            other._handle = nullptr;
            return *this;
        }
#endif
        ~dll() { if (_handle) FreeLibrary(_handle); }
        explicit operator bool() const { return _handle; }

        void reset() { reset_handle(nullptr); }

        void reset(const char* dll_name)
        {
            gbassert(dll_name);
            reset_handle(LoadLibraryA(dll_name));
        }

        void reset(const wchar_t* dll_name)
        {
            gbassert(dll_name);
            reset_handle(LoadLibraryW(dll_name));
        }

        template<class Fn>
        Fn get_function(const char* fun_name) const
        {
            return _handle ? reinterpret_cast<Fn>(GetProcAddress(_handle, fun_name)) : nullptr;
        }

    private:
        HMODULE _handle{ 0 };
        void reset_handle(HMODULE handle)
        {
            if (_handle)
                FreeLibrary(_handle);
            _handle = handle;
        }
    };

#if defined(__cplusplus) && (__cplusplus >= 201402L)
    template<class Fn>
    inline auto get_dll_function = [](dll& d, const char* name) { return d.get_function<Fn>(name); };

    template<class Ret, class ...Args>
    inline auto get_dll_function<Ret(Args...)> = [](dll& d, const char* name) { return d.get_function<Ret(*)(Args...)>(name); };
#endif

    //------------------------------------------------------------------------------
    // GUID 34 chars (excluding the hyphens): "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
    inline auto get_uuid_wstring(bool use_hyphens = false, bool use_braces = false)
    {
        UUID uuid;

        if (auto status = UuidCreate(&uuid); status == RPC_S_OK)
        {
            wchar_t guid_string[40]{};
            if (auto ret = StringFromGUID2(uuid, guid_string, sizeof(guid_string) / sizeof(guid_string[0])); ret > 0)
            {
                std::wstring result(guid_string);
                if (!use_hyphens)
                    std::erase(result, '-');
                if (!use_braces)
                    return result.substr(1, result.size() - 2);
                return result;
            }
            else
                throw std::runtime_error("StringFromGUID2 failed with error " + std::to_string(ret));
        }
        else
            throw std::runtime_error("UuidCreate failed with error " + std::to_string(status));
    }

    //------------------------------------------------------------------------------
    inline auto get_uuid_string(bool use_hyphens = false, bool use_braces = false)
    {
        auto wuuid = get_uuid_wstring(use_hyphens, use_braces);
        if (auto size_needed = WideCharToMultiByte(CP_UTF8, 0, wuuid.data(), (int)wuuid.size(), nullptr, 0, nullptr, nullptr);
            size_needed > 0)
        {
            std::string result(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, wuuid.data(), (int)wuuid.size(), result.data(), size_needed, nullptr, nullptr);
            return result;
        }
        else
            throw std::runtime_error("WideCharToMultiByte() failed: " + std::to_string(size_needed));
    }

    //------------------------------------------------------------------------------
    // unique file name in Windows temporary directory
    inline auto get_temp_file_path(const std::string& extension = ".tmp")
    {
        return std::filesystem::temp_directory_path() / (get_uuid_string() + extension);
    }

}
#endif
