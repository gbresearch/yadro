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
#include <ranges>
#include <algorithm>
#include <utility>
#include <optional>
#include <filesystem>

#include "../util/gbutil.h"
#include "../async/threadpool.h"
#include "../archive/archive.h"

namespace gb::yadro::algorithm
{
    //------------------------------------------------------------------------------------------
    // modified genetic optimization algorithm, minimizing target function
    // algorithm makes probablistic changes: parent swaps, mutations, gradient moves
    // probabilities of changes are defined in opt_param_t type
    // algorithm is greedy
    // parameters to be optimized are supplied in min_max tuples
    // parameters may be scalar types or random access sized ranges (e.g. vector)
    //------------------------------------------------------------------------------------------

    template<class Fn, class CompareFn, class ...Types>
    struct genetic_optimization_t
    {
        using target_t = std::invoke_result_t<Fn, Types...>;

        //------------------------------------------------------------------------------------------
        // constructor takes comparison function for target function
        //------------------------------------------------------------------------------------------
        genetic_optimization_t(Fn target_fn, CompareFn compare, std::tuple<Types, Types> ... min_max)
            requires ((std::invocable<Fn, Types...>
        && std::invocable<CompareFn, std::invoke_result_t< Fn, Types...>, std::invoke_result_t< Fn, Types...>>))
            : _target_fn(std::move(target_fn)), _min_max_params(std::move(min_max)...), _opt_map(compare)
        {
        }

        //------------------------------------------------------------------------------------------
        // constructor uses std::less<> for target function
        //------------------------------------------------------------------------------------------
        genetic_optimization_t(Fn target_fn, std::tuple<Types, Types> ... min_max)
            requires (std::invocable<Fn, Types...>)
        : _target_fn(std::move(target_fn)), _min_max_params(std::move(min_max)...), _opt_map(std::less<>{})
        {
        }

        //------------------------------------------------------------------------------------------
        // constructor takes comparison function and loads optimizer state from specified archive file
        //------------------------------------------------------------------------------------------
        genetic_optimization_t(const std::filesystem::path& archive_file, Fn target_fn, CompareFn compare,
            std::tuple<Types, Types> ... min_max)
            requires ((std::invocable<Fn, Types...>
        && std::invocable<CompareFn, std::invoke_result_t< Fn, Types...>, std::invoke_result_t< Fn, Types...>>))
            : _target_fn(std::move(target_fn)), _min_max_params(std::move(min_max)...), _opt_map(compare)
        {
            load(archive_file);
        }

        //------------------------------------------------------------------------------------------
        // constructor uses std::less<> as comparison and loads optimizer state from specified archive file
        //------------------------------------------------------------------------------------------
        genetic_optimization_t(const std::filesystem::path& archive_file, Fn target_fn,
            std::tuple<Types, Types> ... min_max)
            requires (std::invocable<Fn, Types...>)
        : _target_fn(std::move(target_fn)), _min_max_params(std::move(min_max)...), _opt_map(std::less<>{})
        {
            load(archive_file);
        }

        //------------------------------------------------------------------------------------------
        // save optimizer state to archive file
        //------------------------------------------------------------------------------------------
        void save(const std::filesystem::path& archive_file) const
        {
            std::ofstream ofs(archive_file, std::ios::binary);
            serialize(gb::yadro::archive::bin_archive(ofs));
        }

        //------------------------------------------------------------------------------------------
        // load optimizer state from archive file
        //------------------------------------------------------------------------------------------
        void load(const std::filesystem::path& archive_file)
        {
            std::ifstream ifs(archive_file, std::ios::binary);
            serialize(gb::yadro::archive::bin_archive(ifs));
        }

        //------------------------------------------------------------------------------------------
        // set probabilities of optimization steps
        //------------------------------------------------------------------------------------------
        void set_opt_parameters(double swap_probability, double mutation_probability, double gradient_probability)
        {
            _opt_param = opt_param_t{ swap_probability, mutation_probability, gradient_probability};
        }

        //------------------------------------------------------------------------------------------
        // adding a solution with a known target
        // adding a known good solution can speed up optimzation
        //------------------------------------------------------------------------------------------
        auto add_solution(std::convertible_to<target_t> auto&& target, auto&& ... params)
            requires (sizeof...(params) == sizeof...(Types))
        {
            _opt_map.emplace(std::forward<decltype(target)>(target), std::tuple{ std::forward<decltype(params)>(params)... });
        }

        //------------------------------------------------------------------------------------------
        // adding a solution with unknown target, which will be calculated (can be expensive)
        //------------------------------------------------------------------------------------------
        auto add_solution(auto&& ... params) requires (sizeof...(params) == sizeof...(Types))
        {
            // order of evalution of function parameters is unspecified, must calculate target first
            auto target = std::invoke(_target_fn, params ...);
            _opt_map.emplace(std::move(target), std::tuple{ std::forward<decltype(params)>(params)... });
        }

        //------------------------------------------------------------------------------------------
        // performs genetic_optimization optimization, limited to time duration and max_tries
        // returns tuple(optimization_stats, optimization_map), keeping best max_history results in optimization_map
        // optimize can be called multiple times, it will continue from the previous state, unless cleared
        //------------------------------------------------------------------------------------------
        template<class Rep, class Period>
        auto optimize(std::chrono::duration<Rep, Period> duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        //------------------------------------------------------------------------------------------
        // same as above, but also specifying acceptable target, after satisfying which the optimization may stop
        template<class Rep, class Period>
        auto optimize(target_t acceptable_target, std::chrono::duration<Rep, Period> duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        //------------------------------------------------------------------------------------------
        // multithreaded version of the above, using threadpool (intended for slow target functions)
        // if target function is fast, then context switching overhead will make this function slower than single threaded function
        //------------------------------------------------------------------------------------------
        template<class Rep, class Period>
        auto optimize(gb::yadro::async::threadpool<>& tp, std::chrono::duration<Rep, Period> duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        //------------------------------------------------------------------------------------------
        // same as above, but also specifying acceptable target, after satisfying which the optimization may stop
        template<class Rep, class Period>
        auto optimize(gb::yadro::async::threadpool<>& tp, target_t acceptable_target, std::chrono::duration<Rep, Period> duration, std::size_t max_history = -1, std::size_t max_tries = -1);

        //------------------------------------------------------------------------------------------
        // clear the state
        //------------------------------------------------------------------------------------------
        void clear() { _visited.clear(); _opt_map.clear(); }

        //------------------------------------------------------------------------------------------
        // serialize the state in the archive
        //------------------------------------------------------------------------------------------
        auto serialize(this auto&& self, auto&& archive)
        {
            std::invoke(std::forward<decltype(archive)>(archive), self._visited, self._opt_map, self._opt_param);
        }

    private:
        Fn _target_fn;
        std::tuple<std::tuple<Types, Types>...> _min_max_params;
        util::mutexer<std::mutex> _m;
        std::unordered_set<std::size_t> _visited; // hashes of already tried parameters
        std::multimap<target_t, std::tuple<Types...>, CompareFn> _opt_map; // target functions calculated

        //------------------------------------------------------------------------------------------
        // optimization parameters
        struct opt_param_t
        {
            double swap_probability = 0.3;
            double mutation_probability = 0.5;
            double gradient_probability = 0.3;

            auto serialize(this auto&& self, auto&& archive)
            {
                std::invoke(std::forward<decltype(archive)>(archive), self.swap_probability, 
                    self.mutation_probability, self.gradient_probability);
            }
        } _opt_param;

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

        //------------------------------------------------------------------------------------------
        // single thread optimization, end when acceptable_target, or timeout, or max_tries are reached
        auto optimize_one(std::optional<target_t> acceptable_target, auto&& duration, std::size_t max_history, std::size_t max_tries);
        auto optimize_in_pool(gb::yadro::async::threadpool<>& tp, std::optional<target_t> acceptable_target, auto&& duration, std::size_t max_history, std::size_t max_tries);

        //------------------------------------------------------------------------------------------
        // random functions
        static auto& random_generator() { thread_local std::mt19937 gen(std::random_device{}()); return gen; }
        
        static auto random_scalar(auto&& min_value, auto&& max_value)
        {
            using type_t = std::remove_cvref_t<decltype(min_value)>;
            if (min_value < max_value)
                return static_cast<type_t>(std::uniform_real_distribution<>{(double)min_value, (double)max_value}(random_generator()));
            else
                return static_cast<type_t>(std::uniform_real_distribution<>{(double)max_value, (double)min_value}(random_generator()));
        }
        
        //------------------------------------------------------------------------------------------
        // create a tuple of random numbers between min and max values in _min_max_params
        auto random_initializer() const 
        {
            auto random_param = [](auto&& min_max_tuple)
                {
                    const auto& [min_value, max_value] = min_max_tuple;
                    using type = std::remove_cvref_t<decltype(min_value)>;

                    if constexpr (std::ranges::random_access_range<type>)
                    {
                        util::gbassert(min_value.size() == max_value.size());
                        type result(min_value.size());

                        for (auto size = min_value.size(); size; --size)
                            result[size - 1] = random_scalar(min_value[size - 1], max_value[size - 1]);

                        return result;
                    }
                    else
                        return random_scalar(min_value, max_value);
                };
        
            return util::tuple_transform([&](auto&& min_max_tuple)
                { return random_param(min_max_tuple); }, _min_max_params);
        }

        //------------------------------------------------------------------------------------------
        // mutate random parameter
        auto mutate_parameters(auto&& param_tuple) const
        {
            auto mutate_value = [&](auto&& param, auto&& min_value, auto&& max_value)
                {
                    if (random_scalar(0., 1.) < _opt_param.mutation_probability)
                        return random_scalar(min_value, max_value);
                    else
                        return param;
                };
            auto mutate_param = [&](auto&& param, auto&& min_max_tuple)
                {
                    const auto& [min_value, max_value] = min_max_tuple;
                    using type = std::remove_cvref_t<decltype(param)>;

                    if constexpr (std::ranges::random_access_range<type>)
                    {
                        util::gbassert(min_value.size() == max_value.size());
                        type result(min_value.size());

                        for (auto size = min_value.size(); size; --size)
                            result[size - 1] = mutate_value(param[size - 1], min_value[size - 1], max_value[size - 1]);

                        return result;
                    }
                    else
                        return mutate_value(param, min_value, max_value);
                };

            return util::tuple_transform([&](auto&& param, auto&& min_max_tuple)
                { return mutate_param(param, min_max_tuple); }, param_tuple, _min_max_params);
        }

        //------------------------------------------------------------------------------------------
        // return tuple containing random swaps between parent tuples, tuple_latest is the best
        auto swap_parameters(auto&& tuple_latest, auto&& tuple_prev) const
        {
            auto swap_values = [&](auto&& param1, auto&& param2)
                {
                    return random_scalar(0., 1.) < _opt_param.swap_probability ? param2 : param1;
                };
            auto swap_param = [&](auto&& param1, auto&& param2)
                {
                    using type = std::common_type_t<std::remove_cvref_t<decltype(param1)>, std::remove_cvref_t<decltype(param2)>>;

                    if constexpr (std::ranges::random_access_range<type>)
                    {
                        util::gbassert(param1.size() == param2.size());
                        type result(param1.size());

                        for (auto size = param1.size(); size; --size)
                            result[size - 1] = swap_values(param1[size - 1], param2[size - 1]);

                        return result;
                    }
                    else
                        return swap_values(param1, param2);
                };

            return util::tuple_transform([&](auto&& v1, auto&& v2) { return swap_param(v1, v2); }, tuple_latest, tuple_prev);
        }

        //------------------------------------------------------------------------------------------
        // make a random gradient move, taking two tuples and returning a tuple with a gradient move
        auto gradient_move(auto&& tuple_latest, auto&& tuple_prev) const
        {
            auto grad_value = [&](auto&& v1, auto&& v2, auto&& min_value, auto&& max_value)
                {
                    if (random_scalar(0., 1.) < _opt_param.gradient_probability)
                        return std::max(min_value, std::min(max_value, random_scalar(v1, 2 * v1 - v2)));
                    else
                        return v1;
                };

            auto grad_change = [&](auto&& v1, auto&& v2, auto&& min_max_tuple)
                {
                    const auto& [min_value, max_value] = min_max_tuple;
                    using type = std::remove_cvref_t<decltype(v1)>;
                    static_assert(std::same_as < type, std::remove_cvref_t<decltype(v2)>>);

                    if constexpr (std::ranges::random_access_range<type>)
                    {
                        util::gbassert(v1.size() == v2.size());
                        type result(v1.size());

                        for (auto size = v1.size(); size; --size)
                            result[size - 1] = grad_value(v1[size - 1], v2[size - 1], min_value[size - 1], max_value[size - 1]);

                        return result;
                    }
                    else
                        return grad_value(v1, v2, min_value, max_value);
                };

            return util::tuple_transform([&](auto&& v1, auto&& v2, auto&& min_max_tuple) { return grad_change(v1, v2, min_max_tuple); },
                tuple_latest, tuple_prev, _min_max_params);
        }
    };

    //------------------------------------------------------------------------------------------
    // optimizer construction guides
    template<class Fn, class ...Types>
    genetic_optimization_t(Fn, std::tuple<Types, Types> ...) -> genetic_optimization_t<Fn, std::less<>, Types...>;

    template<class Fn, class ...Types>
    genetic_optimization_t(const std::string&, Fn, std::tuple<Types, Types> ...) -> genetic_optimization_t<Fn, std::less<>, Types...>;


    //------------------------------------------------------------------------------------------
    // statistics for genetic_optimization_t
    struct stats
    {
        std::size_t gradient_count;
        std::size_t genetic_count;
        std::size_t mutation_count;
        std::size_t improvement_count;
        std::size_t repetition_count;
        std::size_t loop_count;
        std::size_t unique_param_count;

        void add(const stats& other)
        {
            gradient_count += other.gradient_count;
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
                << ", gradients: " << s.gradient_count
                << ", genetics: " << s.genetic_count
                << ", mutations: " << s.mutation_count
                << ", unique: " << s.unique_param_count
                << ", repetitions: " << (s.loop_count != 0 ? 100. * s.repetition_count / s.loop_count : 0) << "%"
                << "\n";
            return os;
        }
    };

    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize_one(std::optional<target_t> acceptable_target, auto&& duration, std::size_t max_history, std::size_t max_tries)
    {
        auto rand_params = random_initializer();
        auto start_time = std::chrono::high_resolution_clock::now();
        stats s{};
        
        auto is_acceptable_target = [&]
            {
                if (acceptable_target)
                {
                    std::unique_lock _(_m);
                    return !_opt_map.empty() && CompareFn {}(_opt_map.begin()->first, acceptable_target.value());
                }
                return false;
            };

        for (auto last_target_update = start_time;
            max_tries != 0 && std::chrono::high_resolution_clock::now() - start_time < duration
            && std::chrono::high_resolution_clock::now() - last_target_update < duration / 2
            && !is_acceptable_target();
            --max_tries, ++s.loop_count)
        {
            if (insert_visited(gb::yadro::util::make_hash(rand_params)))
            {
                ++s.unique_param_count;
                auto new_target = std::apply(_target_fn, rand_params);

                if (opt_map_emplace(new_target, rand_params, max_history))
                {
                    last_target_update = std::chrono::high_resolution_clock::now();
                    ++s.improvement_count;
                }
            }
            else
            {
                ++s.repetition_count;
            }
            
            // try next parameter set, get copies of two best parents
            std::tuple<Types...> first_best{};
            std::tuple<Types...> second_best{};
            {
                std::lock_guard _(_m);
                first_best = _opt_map.begin()->second;
                second_best = _opt_map.size() > 1 ? (++_opt_map.begin())->second : first_best;
            }
            // get a new parameter set
            do
            {
                if (first_best != second_best)
                {
                    if ((s.loop_count & 1) == 0)
                    {
                        rand_params = swap_parameters(first_best, second_best);
                        if (rand_params != first_best)
                            ++s.genetic_count;
                    }
                    else
                    {
                        rand_params = gradient_move(first_best, second_best);
                        if (rand_params != first_best)
                            ++s.gradient_count;
                    }
                }
                
                if(auto mutated_params = mutate_parameters(rand_params); rand_params != mutated_params)
                {
                    rand_params = mutated_params;
                    ++s.mutation_count;
                }

            } while (rand_params == first_best && std::chrono::high_resolution_clock::now() - start_time < duration);
        }
        return s;
    }
    
    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    template<class Rep, class Period>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(std::chrono::duration<Rep, Period> duration, std::size_t max_history, std::size_t max_tries)
    {
        return std::tuple(optimize_one(std::nullopt, duration, max_history, max_tries), _opt_map);
    }

    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    template<class Rep, class Period>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(target_t acceptable_target, std::chrono::duration<Rep, Period> duration, std::size_t max_history, std::size_t max_tries)
    {
        return std::tuple(optimize_one(acceptable_target, duration, max_history, max_tries), _opt_map);
    }

    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize_in_pool(gb::yadro::async::threadpool<>& tp, std::optional<target_t> acceptable_target, 
        auto&& duration, std::size_t max_history, std::size_t max_tries)
    {
        std::vector<std::future<stats>> futures;

        for (std::size_t i = 0, thread_count = tp.max_thread_count(); i < thread_count; ++i)
        {
            futures.push_back(tp([&]
                {
                    return optimize_one(acceptable_target, duration, max_history, max_tries);
                }));
        }

        stats s{};
        for (auto&& f : futures)
        {
            s.add(f.get());
        }

        return std::tuple(s, _opt_map);
    }
    
    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    template<class Rep, class Period>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(gb::yadro::async::threadpool<>& tp, target_t acceptable_target, std::chrono::duration<Rep, Period> duration, 
        std::size_t max_history, std::size_t max_tries)
    {
        return optimize_in_pool(tp, acceptable_target, duration, max_history, max_tries);
    }
    //------------------------------------------------------------------------------------------
    template<class Fn, class CompareFn, class ...Types>
    template<class Rep, class Period>
    auto genetic_optimization_t<Fn, CompareFn, Types...>::optimize(gb::yadro::async::threadpool<>& tp, std::chrono::duration<Rep, Period> duration, std::size_t max_history, std::size_t max_tries)
    {
        return optimize_in_pool(tp, std::nullopt, duration, max_history, max_tries);
    }
}

