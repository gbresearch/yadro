//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <vector>
#include <variant>
#include <utility>
#include <stack>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <optional>

namespace gb::yadro::container
{
    // compact graph representation: container of edges and container of nodes (vertexes)
    using index_t = std::uint64_t;
    constexpr index_t invalid_index = -1;

    namespace detail
    {
        //-------------------------------------------------------------------------
        inline std::ostream& dump_index(std::ostream& os, index_t i)
        {
            if (i == invalid_index)
                os << "x";
            else
                os << i;

            return os;
        }
    }

    //-------------------------------------------------------------------------
    template<class T, bool = std::is_class_v<T>>
    struct data_wrapper : T
    {
        template<class ... Args>
        data_wrapper(Args&& ... args) : T(std::forward<Args>(args)...) {}

        decltype(auto) get() const { return *this; }
        decltype(auto) get() { return *this; }

        template<class Arg>
        void set(Arg&& arg) { *this = std::forward<Arg>(arg); }
    };

    //-------------------------------------------------------------------------
    template<class T>
    struct data_wrapper<T, false>
    {
        T data;

        template<class ... Args>
        data_wrapper(Args&& ... args) : data(std::forward<Args>(args)...) {}

        decltype(auto) get() const { return data; }
        decltype(auto) get() { return data; }

        template<class Arg>
        void set(Arg&& arg) { data = std::forward<Arg>(arg); }
    };

    //-------------------------------------------------------------------------
    template<>
    struct data_wrapper<void, false>
    {
    };

    //-------------------------------------------------------------------------
    template<class T>
    struct edge : data_wrapper<T>
    {
        index_t from;
        index_t to;
        index_t in_sibling;
        index_t out_sibling;
        using data_t = T;

        edge() requires(std::is_default_constructible_v<T>) {}

        template<class ... Args>
        edge(index_t from, index_t to, index_t in_sibling, index_t out_sibling, Args&&... args)
            : data_wrapper<T>(std::forward<Args>(args)...), from(from), to(to), in_sibling(in_sibling), out_sibling(out_sibling)
        {}

        template<class Archive>
        void serialize(Archive&& a)
        {
            auto& data = data_wrapper<T>::get();
            a(data, from, to, in_sibling, out_sibling);
        }

        void dump(std::ostream& os) const
        {
            os << "{";
            detail::dump_index(os, from) << ',';
            detail::dump_index(os, to) << ',';
            detail::dump_index(os, in_sibling) << ',';
            detail::dump_index(os, out_sibling);
            os << "} -> " << data_wrapper<T>::get();
        }
    };

    //-------------------------------------------------------------------------
    template<class T>
    struct node : data_wrapper<T>
    {
        index_t in_edge;
        index_t out_edge;
        using data_t = T;

        node() requires(std::is_default_constructible_v<T>) {}

        template<class... Args>
        node(index_t in_edge, index_t out_edge, Args&&... args)
            : data_wrapper<T>(std::forward<Args>(args)...), in_edge(in_edge), out_edge(out_edge)
        {}

        template<class Archive>
        void serialize(Archive&& a)
        {
            auto& data = data_wrapper<T>::get();
            a(data, in_edge, out_edge);
        }

        void dump(std::ostream& os) const
        {
            os << "{";
            detail::dump_index(os, in_edge) << ',';
            detail::dump_index(os, out_edge);
            os << "}";
        }
    };

    //-------------------------------------------------------------------------
    template<class NodeT, class EdgeT>
    class graph
    {
        std::vector<edge<EdgeT>> edges;
        std::vector<node<NodeT>> nodes;
    public:
        template<class ...NodeArgs>
        explicit graph(std::size_t node_count, const NodeArgs&... init)
            : nodes(node_count, node<NodeT>(invalid_index, invalid_index, init...))
        {}

        void dump(std::ostream& os) const
        {
            os << "nodes " << nodes.size() << ":\n";

            auto index = index_t(0);

            for (auto&& n : nodes)
            {
                os << '[' << index << "]: ";
                n.dump(os);
                os << "\n";
                ++index;
            }

            os << "edges " << edges.size() << ":\n";
            index = 0;
            for (auto&& e : edges)
            {
                os << '[' << index << "]: ";
                e.dump(os);
                os << "\n";
                ++index;
            }
        }

        decltype(auto) get_edge(index_t edge) const { return edges[edge]; }
        decltype(auto) get_node(index_t node) const { return nodes[node]; }
        decltype(auto) get_edge_value(index_t edge) const { return edges[edge].get(); }
        decltype(auto) get_node_value(index_t node) const { return nodes[node].get(); }

        // add a disconnected node
        template<class ...NodeArgs>
        auto add_node(NodeArgs&&... args)
        {
            nodes.emplace_back(invalid_index, invalid_index, std::forward<NodeArgs>(args)...);
            return nodes.size() - 1;
        }

        // add a directional edge
        template<class... EdgeArgs>
        auto add_edge(index_t from, index_t to, EdgeArgs&&... args)
        {
            edges.emplace_back(from, to, nodes[to].in_edge, nodes[from].out_edge, std::forward<EdgeArgs>(args)...);
            auto edge_id = edges.size() - 1;
            nodes[from].out_edge = edge_id;
            nodes[to].in_edge = edge_id;

            return edge_id;
        }

        // add a bi-directional edge
        template<class... EdgeArgs>
        auto add_bd_edge(index_t from, index_t to, EdgeArgs&&... args)
        {
            add_edge(from, to, std::forward<EdgeArgs>(args)...);
            add_edge(to, from, std::forward<EdgeArgs>(args)...);
        }

        template<class Fn>
        auto foreach_in_edge(index_t node, Fn fn) const
        {
            for (auto edge = nodes[node].in_edge; edge != invalid_index; edge = edges[edge].in_sibling)
            {
                std::invoke(fn, edge);
            }
        }

        template<class Fn>
        auto foreach_out_edge(index_t node, Fn fn) const
        {
            for (auto edge = nodes[node].out_edge; edge != invalid_index; edge = edges[edge].out_sibling)
            {
                std::invoke(fn, edge);
            }
        }

        template<class Fn>
        auto foreach_edge(index_t node, Fn fn) const
        {
            foreach_in_edge(node, fn);
            foreach_out_edge(node, fn);
        }

        template<class Pred>
        auto find_depth_first(index_t from_node, Pred pred) const
        {

        }

        template<class Pred>
        auto find_breadth_first(index_t from_node, Pred pred) const
        {

        }

        // shortest path: from->to, using CostFn function to extract cost from the edge value
        template<class CostFn>
        auto dijkstra(index_t from, index_t to, CostFn fn) const
        {
            using cost_rec = std::pair<index_t, decltype(fn(0))>;
            using path_t = std::vector<cost_rec>;
            std::map<index_t, decltype(fn(0))> visited_nodes = { { from, 0 } };

            struct compare_path
            {
                auto operator()(const path_t& p1, const path_t& p2) const
                {
                    return p1.back().second > p2.back().second;
                }
            };

            std::priority_queue<path_t, std::vector<path_t>, compare_path> pqueue;
            pqueue.push(path_t{ { from, 0 } });

            while (!pqueue.empty())
            {
                auto path = pqueue.top();

                if (path.back().first == to)
                    return std::optional(path);

                pqueue.pop();
                foreach_edge(path.back().first, [&](auto edge)
                    {
                        auto x_node = edges[edge].to;
                        auto cost = path.back().second + fn(edge);

                        if (!visited_nodes.count(x_node) || visited_nodes[x_node] > cost)
                        {
                            auto x_path = path;
                            x_path.emplace_back(x_node, cost);
                            pqueue.push(x_path);
                            visited_nodes[x_node] = cost;
                        }
                    });
            };

            return std::optional<path_t>();
        }

        template<class Archive>
        void serialize(Archive&& a)
        {
            a(edges, nodes);
        }
    };
}
