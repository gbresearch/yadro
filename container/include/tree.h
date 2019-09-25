//-----------------------------------------------------------------------------
//  GB Research, LLC (c) 2006-2019. All Rights Reserved
//  Author: Gene Bushuyev
//  No part of this file may be reproduced, stored in a retrieval system,
//  or transmitted, in any form, or by any means, electronic, mechanical,
//  photocopying, recording, or otherwise, without the prior written
//  permission.
//-----------------------------------------------------------------------------

#pragma once
#include <memory>
#include <vector>
#include <type_traits>

namespace gbr
{
    namespace container
    {
        template<class T, class index_t>
        struct tree_node final
        {
            index_t parent;
            index_t child;
            index_t sibling;
            T data;

            template<class ...Args>
            tree_node(index_t parent, index_t child, index_t sibling, Args&&...args)
                : parent(parent), child(child), sibling(sibling), data(std::forward<Args>(args)...)
            {}
        };

        template<class index_t>
        struct tree_node<void, index_t> final
        {
            index_t parent;
            index_t child;
            index_t sibling;

            tree_node(index_t parent, index_t child, index_t sibling)
                : parent(parent), child(child), sibling(sibling)
            {}
        };

        template<class T>
        using vector_storage = std::vector<T>;

        template<class T, template<class> class StorageT = vector_storage>
        class tree
        {
            using container_t = StorageT<tree_node<T, std::size_t>>;
            container_t _nodes;
        public:
            using index_t = typename container_t::size_type;
            enum : index_t { invalid_index = (index_t)(-1) };

            // test index
            bool is_valid_index(index_t index) const { return index < _nodes.size(); }
            bool is_hole(index_t index) const
            {
                return index < _nodes.size() && _nodes[index].parent == invalid_index;
            }

            // accessors
            template<class TT = T>
            auto get_value(index_t node)->
                std::enable_if_t< !std::is_same_v<void, std::decay_t<TT>>, std::add_lvalue_reference_t<TT>>
            {
                return _nodes[node].data;
            }

            template<class TT = T>
            auto get_value(index_t node) const ->
                std::enable_if_t< !std::is_same_v<void, std::decay_t<TT>>, std::add_lvalue_reference_t<std::add_const_t<TT>>>
            {
                return _nodes[node].data;
            }

            auto get_parent(index_t node) const { return _nodes[node].parent; }
            auto get_child(index_t node) const { return _nodes[node].child; }
            auto get_sibling(index_t node) const { return _nodes[node].sibling; }

            // insertions and deletions
            template<class...Args>
            auto insert_child(index_t parent, Args&&...);

            template<class...Args>
            auto insert_after_sibling(index_t sibling, Args&&...);

            auto detach_subtree(index_t node);
            auto attach_subtree(index_t to_parent, index_t node);
            auto attach_subtree_after_sibling(index_t sibling, index_t node);

            void delete_subtree(index_t node);

            // copies and moves (order is not preserved)
            auto copy_subtree(index_t to_parent, index_t node);
            auto copy_subtree_after_sibling(index_t sibling, index_t node);
            void copy_children(index_t from_parent, index_t to_parent);
            void move_subtree(index_t to_parent, index_t node);
            void move_subtree_after_sibling(index_t sibling, index_t node);
            void move_children(index_t from_parent, index_t to_parent);

            // searches
            template<class Predicate>
            index_t find_ancestor(index_t node, Predicate pred) const;

            template<class Predicate>
            index_t find_child(index_t parent, Predicate pred) const;

            template<class Predicate>
            index_t find_sibling(index_t node, Predicate pred) const;

            template<class Predicate>
            index_t find_depth_first(index_t node, Predicate pred) const;

            template<class Predicate>
            index_t find_breadth_first(index_t node, Predicate pred) const;

            // iterations
            template<class Function>
            index_t foreach_child(index_t parent, Function vis) const;

            template<class Function>
            index_t foreach_sibling(index_t node, Function vis) const;

            template<class Function>
            index_t foreach_depth_first(index_t parent, Function vis) const;

            template<class Function>
            index_t foreach_breadth_first(index_t parent, Function vis) const;

            // reordering
            void reverse_children(index_t parent);

            template<class Compare>
            void sort_children(index_t parent, Compare comp);

        private:
            void destroy_subtree(index_t node);
        };

        //---------------------------------------------------------------------
        // implementation
        //---------------------------------------------------------------------

        template<class T, template<class> class StorageT>
        template<class...Args>
        auto tree<T, StorageT>::insert_child(index_t parent, Args&&... args)
        {
            auto node_index = _nodes.size();
            _nodes.emplace_back(parent, invalid_index, _nodes[parent].child, std::forward<Args>(args)...);
            _nodes[parent].child = node_index;
            return node_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class...Args>
        auto tree<T, StorageT>::insert_after_sibling(index_t sibling, Args&&... args)
        {
            auto node_index = _nodes.size();
            _nodes.emplace_back(_nodes[sibling].parent, invalid_index, _nodes[sibling].sibling, std::forward<Args>(args)...);
            _nodes[sibling].sibling = node_index;
            return node_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto tree<T, StorageT>::detach_subtree(index_t node)
        {
            if (get_child(get_parent(node)) == node)
            {   // this node is the first child
                _nodes[get_parent(node)].child = get_sibling(node);
            }
            else
            {   // find previous sibling
                auto pred = find_child(_nodes[node].parent, [&](auto n) { return _nodes[n].sibling == node; });
                _nodes[pred].sibling = _nodes[node].sibling;
            }

            _nodes[node].parent = invalid_index;
            _nodes[node].sibling = invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto tree<T, StorageT>::attach_subtree(index_t to_parent, index_t node)
        {
            _nodes[node].parent = to_parent;
            _nodes[node].sibling = _nodes[to_parent].child;
            _nodes[to_parent].child = node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto tree<T, StorageT>::attach_subtree_after_sibling(index_t sibling, index_t node)
        {
            _nodes[node].parent = get_parent(sibling);
            _nodes[node].sibling = get_sibling(sibling);
            _nodes[sibling].sibling = node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void tree<T, StorageT>::destroy_subtree(index_t node)
        {
            _nodes[node].parent = invalid_index;
            if constexpr (!std::is_same_v<void, std::decay_t<T>>)
            {
                std::destroy_at(std::addressof(get_value(node)));
            }
            foreach_child(node, [&](auto n) { delete_subtree(n); });
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void tree<T, StorageT>::delete_subtree(index_t node)
        {
            detach_subtree(node);
            destroy_subtree(node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto tree<T, StorageT>::copy_subtree(index_t to_parent, index_t node)
        {
            index_t new_subtree = invalid_index;

            if constexpr (std::is_same_v<void, std::decay_t<T>>)
                new_subtree = insert_child(to_parent);
            else
                new_subtree = insert_child(to_parent, _nodes[node].data);

            foreach_child(node, [&](auto n) { copy_subtree(new_subtree, n); });

            return new_subtree;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto tree<T, StorageT>::copy_subtree_after_sibling(index_t sibling, index_t node)
        {
            index_t new_subtree = invalid_index;

            if constexpr (std::is_same_v<void, std::decay_t<T>>)
                new_subtree = insert_after_sibling(sibling);
            else
                new_subtree = insert_after_sibling(sibling, _nodes[node].data);

            foreach_child(node, [&](auto n) { copy_subtree(new_subtree, n); });

            return new_subtree;
        }

        //---------------------------------------------------------------------
        template<class T, template <class> class StorageT>
        void tree<T, StorageT>::copy_children(index_t from_parent, index_t to_parent)
        {
            foreach_child(from_parent, [&](auto n) { copy_subtree(to_parent, n); });
        }

        //---------------------------------------------------------------------
        template<class T, template <class>  class StorageT>
        inline void tree<T, StorageT>::move_subtree(index_t to_parent, index_t node)
        {
            detach_subtree(node);
            attach_subtree(to_parent, node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void tree<T, StorageT>::move_subtree_after_sibling(index_t sibling, index_t node)
        {
            detach_subtree(node);
            attach_subtree_after_sibling(sibling, node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void tree<T, StorageT>::move_children(index_t from_parent, index_t to_parent)
        {
            foreach_child(from_parent, [&](auto n) { move_subtree(to_parent, n); });
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::find_ancestor(index_t node, Predicate pred) const
        {
            for (node = get_parent(node); node != invalid_index; node = get_parent(node))
            {
                if (std::invoke(pred, node))
                    return node;
            }

            return invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::find_child(index_t parent, Predicate pred) const
        {
            for (auto node = get_child(parent); node != invalid_index; node = get_sibling(node))
            {
                if (std::invoke(pred, node))
                    return node;
            }

            return invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::find_sibling(index_t node, Predicate pred) const
        {
            for (; node != invalid_index; node = get_sibling(node))
            {
                if (std::invoke(pred, node))
                    return node;
            }

            return invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::find_depth_first(index_t node, Predicate pred) const
        {
            if (node == invalid_index || std::invoke(pred, node))
            {
                return node;
            }
            else for (node = get_child(node); node != invalid_index; node = get_sibling(node))
            {
                auto n = find_depth_first(node, pred);
                if (n != invalid_index)
                    return n;
            }

            return invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::find_breadth_first(index_t node, Predicate pred) const
        {
            auto search = [&](index_t node, std::vector<index_t>& nodes)->index_t
            {
                for (; node != invalid_index; node = get_sibling(node))
                {
                    if (std::invoke(pred, node))
                        return node;

                    for (auto child = get_child(node); child != invalid_index; child = get_sibling(child))
                        nodes.push_back(child);
                }

                return invalid_index;
            };

            std::vector<index_t> in_nodes(node, 1);
            std::vector<index_t> out_nodes;
            while (!in_nodes.empty())
            {
                out_nodes.clear();
                for (auto n : in_nodes)
                {
                    auto s = search(n, out_nodes);
                    if (s != invalid_index)
                        return s;
                }

                in_nodes.swap(out_nodes);
            }

            return invalid_index;
        }

        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        // iterations
        template<class T, template<class> class StorageT>
        template<class Function>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::foreach_child(index_t parent, Function vis) const
        {
            auto node = get_child(parent);

            if constexpr (std::is_convertible_v<decltype(std::invoke(vis, 0)), bool>)
            {
                for (auto keep_going = true; keep_going && is_valid_index(node); node = get_sibling(node))
                {
                    keep_going = std::invoke(vis, node);
                }
            }
            else
            {
                for (; is_valid_index(node); node = get_sibling(node))
                {
                    std::invoke(vis, node);
                }
            }

            return node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Function>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::foreach_sibling(index_t node, Function vis) const
        {
            if constexpr (std::is_convertible_v<decltype(std::invoke(vis, 0)), bool>)
            {
                for (auto keep_going = true; keep_going && is_valid_index(node); node = get_sibling(node))
                {
                    keep_going = std::invoke(vis, node);
                }
            }
            else
            {
                for (; is_valid_index(node); node = get_sibling(node))
                {
                    std::invoke(vis, node);
                }
            }

            return node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Function>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::foreach_depth_first(index_t parent, Function vis) const
        {
            if constexpr (std::is_convertible_v<decltype(std::invoke(vis, 0)), bool>)
            {
                return find_depth_first(parent, [&](auto n) { return !std::invoke(vis, n); });
            }
            else
            {
                return find_depth_first(parent, [&](auto n) { std::invoke(vis, n); return false; });
            }
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Function>
        typename tree<T, StorageT>::index_t tree<T, StorageT>::foreach_breadth_first(index_t parent, Function vis) const
        {
            if constexpr (std::is_convertible_v<decltype(std::invoke(vis, 0)), bool>)
            {
                return find_breadth_first(parent, [&](auto n) { return !std::invoke(vis, n); });
            }
            else
            {
                return find_breadth_first(parent, [&](auto n) { std::invoke(vis, n); return false; });
            }
        }

        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
        //---------------------------------------------------------------------
    }
}