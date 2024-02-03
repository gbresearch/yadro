//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2024, Gene Bushuyev
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
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cassert>
#include <ranges>
#include <vector>
#include <concepts>
#include <tuple>
#include <algorithm>
#include <numeric>
#include <initializer_list>
#include <type_traits>
#include "../util/gberror.h"

namespace gb::yadro::container
{
    //---------------------------------------------------------------------------------------------
    // concepts
    //---------------------------------------------------------------------------------------------
    template<class T>
    concept tensor_c = requires
    {
        std::declval<T>().data();
        std::declval<T>().size();
        std::declval<T>().cardinality();
        std::declval<T>().dimension(std::declval<std::size_t>());
    };

    //---------------------------------------------------------------------------------------------
    // indexers
    //---------------------------------------------------------------------------------------------

    template<std::size_t D, std::size_t ...Ds>
    struct static_indexer_t
    {
        // mapping indexes to container index
        constexpr auto operator()(auto i, auto ...ds) const
        {
            static_assert(sizeof...(Ds) == sizeof...(ds));
            gb::yadro::util::gbassert(i < D);
            if constexpr (sizeof...(Ds) == 0)
                return i;
            else
                return i + D * static_indexer_t<Ds...>{}(ds...);
        }

        auto operator== (const static_indexer_t& other) const { return true; }
        static consteval auto size() { return (D*...*Ds); }
        static consteval auto cardinality() { return sizeof ...(Ds) + 1; }
        
        static constexpr auto dimension(auto index)
        {
            gb::yadro::util::gbassert(index < cardinality());
            if constexpr (sizeof ...(Ds) != 0)
                if (index == 0)
                    return D;
                else
                    return static_indexer_t<Ds...>::dimension(index - 1);
            else
                return D;
        }

        auto serialize(auto&& archive)
        {
        }
    };

    //---------------------------------------------------------------------------------------------
    // dynamic indexer with runtime Cardinality
    struct dynamic_indexer_t
    {
        explicit dynamic_indexer_t(std::convertible_to<std::size_t> auto&& ... indexes)
            : _indexes{ {static_cast<std::size_t>(indexes), 0}... }
        {
            if constexpr (sizeof ...(indexes) != 0)
            {
                _indexes[0].second = 1;
                for (std::size_t i = 1; i < _indexes.size(); ++i)
                    _indexes[i].second = _indexes[i - 1].second * _indexes[i - 1].first;
            }
        }

        // mapping indexes to container index
        constexpr auto operator()(std::convertible_to<std::size_t> auto&& ...indexes) const
        {
            check_indexes(indexes...);

            std::size_t index = 0;

            for (std::size_t idx = 0; auto i : std::initializer_list{ static_cast<std::size_t>(indexes)... })
                index += i * _indexes[idx++].second;

            return index;
        }

        constexpr auto operator== (const dynamic_indexer_t& other) const { return _indexes == other._indexes; }

        constexpr auto size() const { return _indexes.back().first * _indexes.back().second; }
        constexpr auto cardinality() const { return _indexes.size(); }
        auto dimension(std::size_t index) const { gb::yadro::util::gbassert(index < cardinality());  return _indexes[index].first; }

        constexpr auto check_indexes(std::convertible_to<std::size_t> auto&& ... indexes) const
        {
            gb::yadro::util::gbassert(sizeof...(indexes) == _indexes.size());
            for (std::size_t idx = 0; std::size_t i: std::initializer_list{ static_cast<std::size_t>(indexes)... })
            {
                gb::yadro::util::gbassert(_indexes[idx++].first > i);
            }
        }

        auto serialize(this auto&& self, auto&& archive)
        {
            std::invoke(std::forward<decltype(archive)>(archive), std::forward<decltype(self)>(self)._indexes);
        }

    private:
        std::vector<std::pair<std::size_t, std::size_t>> _indexes; // {dimension, multiple}[N]
    };

    //---------------------------------------------------------------------------------------------
    // basic_tensor derives from indexer to enable empty base class optimization
    template<class T, std::ranges::range container_t, class indexer_t>
    struct basic_tensor : indexer_t
    {
        using indexer_t::size;
        using indexer_t::cardinality;
        using indexer_t::dimension;

        basic_tensor() = default;
        explicit basic_tensor(std::convertible_to<indexer_t> auto&& indexer, auto&& ... args)
            : indexer_t(std::forward<decltype(indexer)>(indexer)),
            _data( std::forward<decltype(args)>(args)... )
        {
        }

        explicit basic_tensor(std::convertible_to<T> auto&& ... data) : _data{ static_cast<T>(data)... }
        {
        }


        constexpr decltype(auto) operator()(this auto&& self, std::convertible_to<std::size_t> auto... indexes)
        {
            return std::forward<decltype(self)>(self)._data[self.indexer()(static_cast<std::size_t>(indexes)...)];
        }

        auto operator== (const basic_tensor& other) const { return indexer() == other.indexer() && _data == other._data; }
        const auto& indexer() const { return static_cast<const indexer_t&>(*this); }
        auto& indexer() { return static_cast<indexer_t&>(*this); }
        
        auto index_of(std::convertible_to<std::size_t> auto... indexes) const
        {
            return indexer()(static_cast<std::size_t>(indexes)...);
        }

        auto& data() { return _data; }
        const auto& data() const { return _data; }

        auto is_compatible(tensor_c auto&& other) const
        {
            // the other tensor must have the same size an cardinality, it may have different dimensions
            return other.size() == size() && other.cardinality() == cardinality();
        }

        auto serialize(this auto&& self, auto&& archive)
        {
            std::invoke(std::forward<decltype(archive)>(archive), std::forward<decltype(self)>(self).indexer(),
                std::forward<decltype(self)>(self)._data);
        }

    private:
        container_t _data;
    };

    //---------------------------------------------------------------------------------------------
    // static tensor with constant compile-time dimensions
    template<class T, std::size_t ...Ds>
    struct tensor : basic_tensor<T, std::array<T, (1*...*Ds)>, static_indexer_t<Ds... >>
    {
        using indexer_t = static_indexer_t<Ds... >;
        using base_t = basic_tensor<T, std::array<T, (1*...*Ds)>, indexer_t>;
        using dimensions_t = std::index_sequence<Ds...>;
        using base_t::size;
        using base_t::cardinality;
        using base_t::dimension;
        using base_t::data;
        using base_t::is_compatible;

        tensor() = default;

        // constructing tensor and assigning values in flat view
        explicit tensor(std::convertible_to<T> auto&& ... data) : base_t(std::forward<decltype(data)>(data)...)
        {
            static_assert(sizeof...(data) == 0 || indexer_t::size() == sizeof...(data));
        }

        // assigning any compatible tensor (copying data)
        auto& operator= (tensor_c auto&& other)
        {
            gb::yadro::util::gbassert(is_compatible(other));
            std::ranges::copy(other.data(), std::begin(data()));
            return *this;
        }

    };

    //---------------------------------------------------------------------------------------------
    // dynamic tensor with dimensions assigned at run time
    template<class T>
    struct tensor<T> : basic_tensor<T, std::vector<T>, dynamic_indexer_t>
    {
        using indexer_t = dynamic_indexer_t;
        using base_t = basic_tensor<T, std::vector<T>, indexer_t>;
        using base_t::size;
        using base_t::cardinality;
        using base_t::dimension;
        using base_t::data;
        using base_t::is_compatible;

        tensor() = default;

        // constructing tensor of specified dimensions
        explicit tensor(std::convertible_to<std::size_t> auto ... dimensions) :
            base_t(indexer_t(static_cast<std::size_t>(dimensions) ...), (1 * ... * dimensions))
        {
        }

        // constructing from static tensor
        template<std::convertible_to<T> V, std::size_t ...Ds>
        explicit tensor(const tensor<V, Ds...>& other) : tensor(Ds...)
        {
            std::ranges::copy(other.data(), std::begin(data()));
        }

        // assigning any compatible tensor
        auto& operator= (tensor_c auto&& other)
        {
            gb::yadro::util::gbassert(is_compatible(other));
            std::ranges::copy(other.data(), std::begin(data()));
            return *this;
        }
    };

    //---------------------------------------------------------------------------------------------
    // operators
    namespace tensor_operators
    {
        auto operator== (tensor_c auto&& tensor1, tensor_c auto&& tensor2)
        {
            if (tensor1.is_compatible(tensor2))
            {
                auto matched_dimensions = true;
                for (std::size_t index = 0, cardinality = tensor1.cardinality();
                    index < cardinality && matched_dimensions; ++index)
                    matched_dimensions = tensor1.dimension(index) == tensor2.dimension(index);
                
                if (matched_dimensions)
                {
                    auto [first, second] = std::ranges::mismatch(tensor1.data(), tensor1.data());
                    return first == tensor1.data().end() && second == tensor1.data().end();
                }
            }
            return false;
        }
    }

}
