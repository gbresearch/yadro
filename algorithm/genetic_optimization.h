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
#include <map>
#include <unordered_set>
#include <functional>
#include <tuple>
#include <chrono>
#include <random>
#include <ostream>
#include <fstream>
#include <array>

#include "../util/gberror.h"
#include "../util/misc.h"
#include "../util/tuple_functions.h"
#include "../util/gbmacro.h"
#include "../async/threadpool.h"

namespace gb::yadro::algorithm
{
    // neighbor expansion optimization algorithm, minimizing target function
    template<class Fn, class CompareFn, class ...Types>
    struct genetic_optimization_t
    {
        genetic_optimization_t(Fn target_fn, CompareFn compare, std::tuple<Types, Types> ... min_max)
            requires ((std::invocable<Fn, Types...> 
        && std::invocable<CompareFn, std::invoke_result_t< Fn, Types...>, std::invoke_result_t< Fn, Types...>>)
        && ... && std::convertible_to<double, Types>)
        : _target_fn(target_fn), _min_max_params(min_max...), _opt_map(compare)
        {
            _weights.fill(1.0);
        }

        genetic_optimization_t(Fn target_fn, std::tuple<Types, Types> ... min_max)
            requires (std::invocable<Fn, Types...> && ... && std::convertible_to<double, Types>)
        : _target_fn(target_fn), _min_max_params(min_max...), _opt_map(std::less<>{})
        {
            _weights.fill(1.0);
        }

        // load data from archive
        genetic_optimization_t(const std::string& archive_file, Fn target_fn, CompareFn compare, 
            std::tuple<Types, Types> ... min_max)
            requires ((std::invocable<Fn, Types...>
        && std::invocable<CompareFn, std::invoke_result_t< Fn, Types...>, std::invoke_result_t< Fn, Types...>>)
        && ... && std::convertible_to<double, Types>)
        : _target_fn(target_fn), _min_max_params(min_max...), _opt_map(compare)
        { 
            load(archive_file);
        }

        // load data from archive
        genetic_optimization_t(const std::string& archive_file, Fn target_fn,
            std::tuple<Types, Types> ... min_max)
            requires (std::invocable<Fn, Types...> && ... && std::convertible_to<double, Types>)
        : _target_fn(target_fn), _min_max_params(min_max...), _opt_map(std::less<>{})
        {
            load(archive_file);
        }

        // save data to file
        void save(const std::string& archive_file) const
        {
            std::ofstream ofs(archive_file, std::ios::binary);
            serialize(gb::yadro::archive::bin_archive(ofs));
        }

        // load data from file
        void load(const std::string& archive_file)
        {
            std::ifstream ifs(archive_file, std::ios::binary);
            serialize(gb::yadro::archive::bin_archive(ifs));
        }

        // function that allows assigning parameter weights
        // diffent parameters can have different cost of calculation and different effect on target function
        // assigning weights allows changing the probability of mutation, which is determined by normal distribution,
        // bigger weight corresponds to lower probability of mutation
        // default weight of every parameter is 1, which corresponds to one standard deviation
        auto assign_weights(std::convertible_to<double> auto&& ... weights) requires (sizeof...(weights) == sizeof...(Types))
        {
            static_assert(sizeof...(weights) == sizeof...(Types));
            _weights = std::array<double, sizeof...(Types)>{static_cast<double>(weights)...};
        }
        
        // adding a known good solution can speed up optimzation
        // adding a solution with a known target
        auto add_solution(std::convertible_to<target_t> auto&& target, auto&& ... params)
            requires (sizeof...(params) == sizeof...(Types))
        {
            _opt_map.emplace(std::forward<decltype(target)>(target), std::tuple{std::forward<decltype(params)>(params)...});
        }

        // adding a solution with an unknown target (can be expensive)
        auto add_solution(auto&& ... params) requires (sizeof...(params) == sizeof...(Types))
        {
            // order of evalution of function parameters is unspecified, must calculate target first
            auto target = std::invoke(_target_fn, params ...);
            _opt_map.emplace(std::move(target), std::tuple{std::forward<decltype(params)>(params)...});
        }

        // performs genetic_optimization optimization, limited to time duration and max_tries
        // returns tuple(optimization_stats, optimization_map), keeping best max_history results in optimization_map
        // optimize can be called multiple times, it will continue from the previous state, unless cleared
        auto optimize(auto&& duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        // multithreaded version of the above, using threadpool (intended for slow target functions)
        // if target function is fast, then context switching overhead will make this function slower than single threaded function
        auto optimize(gb::yadro::async::threadpool<>& tp, auto&& duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        // clear the state
        void clear() { _visited.clear(); _opt_map.clear(); }

        // serialize the state in the archive
        auto serialize(this auto&& self, auto&& archive)
        {
            std::invoke(std::forward<decltype(archive)>(archive), std::forward<decltype(self)>(self)._visited,
                std::forward<decltype(self)>(self)._opt_map, std::forward<decltype(self)>(self)._weights);
        }

    private:
        Fn _target_fn;
        std::tuple<std::tuple<Types, Types>...> _min_max_params;
        std::array<double, sizeof...(Types)> _weights;

        using target_t = std::invoke_result_t<Fn, Types...>;

        std::mutex _m;
        std::unordered_set<std::size_t> _visited; // to avoid linear search through _opt_set
        std::multimap<target_t, std::tuple<Types...>, CompareFn> _opt_map; // target functions calculated

        auto insert_visited(std::size_t v)
        {
            std::lock_guard _(_m);
            return _visited.insert(v).second;
        }

        // emplace the new target and return true if it improved
        auto opt_map_emplace(auto target, auto&& params, std::size_t max_history)
        {
            std::lock_guard _(_m);
            auto improved = _opt_map.empty() || _opt_map.key_comp()(target, _opt_map.begin()->first);
            
            if (improved || _opt_map.size() < max_history || _opt_map.key_comp()(target, _opt_map.rend()->first))
            {
                _opt_map.emplace(target, params);
            }

            while (_opt_map.size() > max_history)
            {
                _opt_map.erase(--_opt_map.end());
            }

            return improved;
        }

        auto optimize_one(auto&& duration, std::size_t max_history, std::size_t max_tries);
    };

    template<class Fn, class ...Types>
    genetic_optimization_t(Fn, std::tuple<Types, Types> ...) -> genetic_optimization_t<Fn, std::less<>, Types...>;

    template<class Fn, class ...Types>
    genetic_optimization_t(const std::string&, Fn, std::tuple<Types, Types> ...) -> genetic_optimization_t<Fn, std::less<>, Types...>;


    // statistics for genetic_optimization_t
    struct stats
    {
        std::size_t genetic_count;
        std::size_t mutation_count;
        std::size_t improvement_count;
        std::size_t repetition_count;
        std::size_t loop_count;
        std::size_t unique_param_count;

        void add(const stats& other)
        {
            genetic_count += other.genetic_count;
            mutation_count += other.mutation_count;
            improvement_count += other.improvement_count;
            repetition_count += other.repetition_count;
            loop_count += other.loop_count;
            unique_param_count += other.unique_param_count;
        }

        friend auto& operator<< (std::ostream& os, const stats& s)
        {
            os << "loops: " << s.loop_count
                << ", improvements: " << s.improvement_count
                << ", genetics: " << s.genetic_count
                << ", mutations: " << s.mutation_count
                << ", unique: " << s.unique_param_count
                << ", repetitions: " << (s.loop_count != 0 ? 100. * s.repetition_count / s.loop_count : 0) << "%"
                << "\n";
            return os;
        }
    };

    template<class Fn, class CompareFn, class ...Types>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize_one(auto&& duration, std::size_t max_history, std::size_t max_tries)
    {
        stats s{};
        thread_local std::mt19937 gen(std::random_device{}());

        auto initial_params = [&]
            {
                return gb::yadro::util::tuple_transform([&](auto&& tup)
                    {
                        auto [min_value, max_value] = tup;
                        std::uniform_real_distribution<> dist((double)min_value, (double)max_value);
                        return static_cast<decltype(min_value)>(dist(gen));
                    }, _min_max_params);
            };

        // swap random fields from two parents
        std::normal_distribution<> genetic_distribution(0., 1.);
        std::uniform_real_distribution<> mutation_dist(0., 1.);

        auto next_genetic = [&](auto&& rand_params1, auto&& rand_params2) {
            return gb::yadro::util::tuple_transform(
                [&](auto&& param1, auto&& param2, auto&& min_max, auto weight)
                {
                    using type_t = std::common_type_t<std::remove_cvref_t<decltype(param1)>, std::remove_cvref_t<decltype(param2)>>;
                    if (std::abs(genetic_distribution(gen)) > weight)
                    {
                        ++s.genetic_count;
                        return static_cast<type_t>(param2);
                    }
                    else
                        return static_cast<type_t>(param1);
                }, rand_params1, rand_params2, _min_max_params, _weights);
            };

        auto next_mutation = [&](auto&& rand_params) {
            return gb::yadro::util::tuple_transform(
                [&](auto&& param, auto&& min_max, auto weight)
                {
                    using type_t = std::remove_cvref_t<decltype(param)>;
                    if (std::abs(genetic_distribution(gen)) > weight)
                    {
                        ++s.mutation_count;
                        return static_cast<type_t>(std::get<0>(min_max) + (std::get<1>(min_max) - std::get<0>(min_max)) * mutation_dist(gen));
                    }
                    else
                        return static_cast<type_t>(param);
                }, rand_params, _min_max_params, _weights);
            };

        for (auto [rand_params, start_time, last_target_update] =
            std::tuple(initial_params(), std::chrono::high_resolution_clock::now(), std::chrono::high_resolution_clock::now());
            max_tries != 0 && std::chrono::high_resolution_clock::now() - start_time < duration
            && std::chrono::high_resolution_clock::now() - last_target_update < duration / 2;
            --max_tries, ++s.loop_count
            )
        {
            if (insert_visited(gb::yadro::util::make_hash(rand_params)))
            {
                ++s.unique_param_count;
                auto new_target = std::apply(_target_fn, rand_params);

                if(opt_map_emplace(new_target, rand_params, max_history))
                {
                    last_target_update = std::chrono::high_resolution_clock::now();
                    ++s.improvement_count;
                }
            }
            else
            {
                ++s.repetition_count;
            }

            // make genetic move

            std::tuple<Types...> first_best{};
            std::tuple<Types...> second_best{};
            {
                std::lock_guard _(_m);
                first_best = _opt_map.begin()->second;
                second_best = _opt_map.size() > 1 ? (++_opt_map.begin())->second : first_best;
            }
            do
            {
                if(first_best != second_best)
                    rand_params = next_genetic(first_best, second_best);
                rand_params = next_mutation(rand_params);
            } while (rand_params == first_best && std::chrono::high_resolution_clock::now() - start_time < duration);
        }

        return s;
    }

    template<class Fn, class CompareFn, class ...Types>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(auto&& duration, std::size_t max_history, std::size_t max_tries)
    {
        return std::tuple(optimize_one(duration, max_history, max_tries), _opt_map);
    }

    template<class Fn, class CompareFn, class ...Types>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(gb::yadro::async::threadpool<>& tp, auto&& duration, std::size_t max_history, std::size_t max_tries)
    {
        std::vector<std::future<stats>> futures;

        for (std::size_t i = 0, thread_count = tp.max_thread_count(); i < thread_count; ++i)
        {
            futures.push_back(tp([&]
                {
                    return optimize_one(duration, max_history, max_tries);
                }));
        }

        stats s{};
        for (auto&& f : futures)
        {
            s.add(f.get());
        }

        return std::tuple(s, _opt_map);
    }
}
