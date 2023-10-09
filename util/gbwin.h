#pragma once

// windows utilities

#if defined(_Windows) || defined(__WIN32__) || defined(_WIN32) || defined(_WIN64)

#include <functional>
#include "gberror.h"
#include <Windows.h>
#undef min
#undef max

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
    inline auto get_function = [](dll& d, const char* name) { return d.get_function<Fn>(name); };

    template<class Ret, class ...Args>
    inline auto get_function<Ret(Args...)> = [](dll& d, const char* name) { return d.get_function<Ret(*)(Args...)>(name); };
#endif
}
#endif
