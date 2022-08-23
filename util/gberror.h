#pragma once
#include <exception>
#include <stdexcept>
#include <concepts>
#include <functional>
#include <string>
#include <source_location>

namespace gb::yadro::util
{
    void gbassert(auto v, std::source_location location = std::source_location::current()) 
        requires(std::invocable<decltype(v)> || std::convertible_to<decltype(v), bool>)
    {
        if constexpr (std::invocable<decltype(v)>)
        {
            if (!std::invoke(v))
                throw std::logic_error(location.file_name() + ("(" + std::to_string(location.line()) + ")"));
        }
        else
        {
            if (!v)
                throw std::logic_error(location.file_name() + ("(" + std::to_string(location.line()) + ")"));
        }

    }
}
