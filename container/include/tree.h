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
        //---------------------------------------------------------------------
        template<class T, class index_t, bool = std::is_class_v<T>> struct tree_node;

        template<class T, class index_t>
        struct tree_node<T, index_t, false> final
        {
            index_t parent;
            index_t child;
            index_t sibling;
            T data;

            template<class ...Args>
            tree_node(index_t parent, index_t child, index_t sibling, Args&&...args)
                : parent(parent), child(child), sibling(sibling), data(std::forward<Args>(args)...)
            {}

            decltype(auto) get_data() { return (data); }
            decltype(auto) get_data() const { return (data); }
        };

        template<class T, class index_t>
        struct tree_node<T, index_t, true> final : T
        {
            index_t parent;
            index_t child;
            index_t sibling;
            //T data;

            template<class ...Args>
            tree_node(index_t parent, index_t child, index_t sibling, Args&& ...args)
                : T(std::forward<Args>(args)...), parent(parent), child(child), sibling(sibling)
            {}

            decltype(auto) get_data() { return *static_cast<T*>(this); }
            decltype(auto) get_data() const { return *static_cast<const T*>(this); }
        };

        template<class index_t>
        struct tree_node<void, index_t, false> final
        {
            index_t parent;
            index_t child;
            index_t sibling;

            tree_node(index_t parent, index_t child, index_t sibling)
                : parent(parent), child(child), sibling(sibling)
            {}
        };

        //---------------------------------------------------------------------
        template<class StorageT>
        struct storage_traits final
        {
            constexpr static auto size(const StorageT& storage) { return storage.size(); }

            template<class index_t>
            constexpr static decltype(auto) get(const StorageT& storage, index_t index) { return (storage[index]); }
            
            template<class index_t>
            constexpr static decltype(auto) get(StorageT& storage, index_t index) { return (storage[index]); }

            template<class...Args>
            constexpr static void emplace_back(StorageT& storage, Args&& ... args) { storage.emplace_back(std::forward<Args>(args)...); }
        };

        //---------------------------------------------------------------------
        template<class T>
        using vector_storage = std::vector<T>;

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT = vector_storage>
        struct indexed_tree final
        {
            static_assert(!std::is_reference_v<T>);

            using container_t = StorageT<tree_node<T, std::size_t>>;

            using data_t = T;

            template<class U> using rebind = indexed_tree<U, StorageT>;

            constexpr static bool has_data = !std::is_same_v<std::remove_cv_t<T>, void>;

            using index_t = typename container_t::size_type;

            enum : index_t { invalid_index = (index_t)(-1) };

            // test index
            auto is_valid_index(index_t index) const { return index < nodes_size(); }
            auto is_hole(index_t index) const
            {
                return index < nodes_size() && get_node(index).parent == invalid_index;
            }

            // accessors
            template<class TT = T, bool = rebind<TT>::has_data>
            decltype(auto) get_value(index_t node)
            {
                return (get_node(node).get_data());
            }

            template<class TT = T>
            decltype(auto) get_value(index_t node, bool = rebind<TT>::has_data) const
            {
                return (get_node(node).get_data());
            }
            
            auto get_parent(index_t node) const { return get_node(node).parent; }
            auto get_child(index_t node) const { return get_node(node).child; }
            auto get_sibling(index_t node) const { return get_node(node).sibling; }

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
            auto foreach_sibling(index_t node, Function vis) const;

            template<class Function>
            auto foreach_depth_first(index_t parent, Function vis) const;

            template<class Function>
            auto foreach_breadth_first(index_t parent, Function vis) const;

            // reordering
            void reverse_children(index_t parent);

            template<class Compare>
            void sort_children(index_t parent, Compare comp);

        private:
            container_t _nodes;

            void destroy_subtree(index_t node);

            decltype(auto) get_node(index_t index) { return storage_traits< container_t>::template get(_nodes, index); }
            decltype(auto) get_node(index_t index) const { return storage_traits<container_t>::template get(_nodes, index); }

            template<class...Args>
            void emplace_back(Args&& ...args) { storage_traits<container_t>::template emplace_back(_nodes, std::forward<Args>(args)...); }

            constexpr auto nodes_size() const { return storage_traits<container_t>::size(_nodes); }
        };

        //---------------------------------------------------------------------
        // implementation
        //---------------------------------------------------------------------

        template<class T, template<class> class StorageT>
        template<class...Args>
        auto indexed_tree<T, StorageT>::insert_child(index_t parent, Args&&... args)
        {
            auto node_index = nodes_size();
            emplace_back(parent, invalid_index, get_node(parent).child, std::forward<Args>(args)...);
            get_node(parent).child = node_index;
            return node_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class...Args>
        auto indexed_tree<T, StorageT>::insert_after_sibling(index_t sibling, Args&&... args)
        {
            auto node_index = nodes_size();
            emplace_back(get_node(sibling).parent, invalid_index, get_node(sibling).sibling, std::forward<Args>(args)...);
            get_node(sibling).sibling = node_index;
            return node_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto indexed_tree<T, StorageT>::detach_subtree(index_t node)
        {
            if (get_child(get_parent(node)) == node)
            {   // this node is the first child
                get_node(get_parent(node)).child = get_sibling(node);
            }
            else
            {   // find previous sibling
                auto pred = find_child(get_node(node).parent, [&](auto n) { return this->get_node(n).sibling == node; });
                get_node(pred).sibling = get_node(node).sibling;
            }

            get_node(node).parent = invalid_index;
            get_node(node).sibling = invalid_index;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto indexed_tree<T, StorageT>::attach_subtree(index_t to_parent, index_t node)
        {
            get_node(node).parent = to_parent;
            get_node(node).sibling = get_node(to_parent).child;
            get_node(to_parent).child = node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto indexed_tree<T, StorageT>::attach_subtree_after_sibling(index_t sibling, index_t node)
        {
            get_node(node).parent = get_parent(sibling);
            get_node(node).sibling = get_sibling(sibling);
            get_node(sibling).sibling = node;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void indexed_tree<T, StorageT>::destroy_subtree(index_t node)
        {
            get_node(node).parent = invalid_index;
            if constexpr(has_data)
            {
                std::destroy_at(std::addressof(get_value(node)));
            }
            foreach_child(node, [&](auto n) { delete_subtree(n); });
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void indexed_tree<T, StorageT>::delete_subtree(index_t node)
        {
            detach_subtree(node);
            destroy_subtree(node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto indexed_tree<T, StorageT>::copy_subtree(index_t to_parent, index_t node)
        {
            index_t new_subtree = invalid_index;

            if constexpr (std::is_same_v<void, std::remove_cv_t<T>>)
                new_subtree = insert_child(to_parent);
            else
                new_subtree = insert_child(to_parent, get_node(node).get_data());

            foreach_child(node, [&](auto n) { copy_subtree(new_subtree, n); });

            return new_subtree;
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        auto indexed_tree<T, StorageT>::copy_subtree_after_sibling(index_t sibling, index_t node)
        {
            index_t new_subtree = invalid_index;

            if constexpr (std::is_same_v<void, std::remove_cv_t<T>>)
                new_subtree = insert_after_sibling(sibling);
            else
                new_subtree = insert_after_sibling(sibling, get_node(node).get_data());

            foreach_child(node, [&](auto n) { copy_subtree(new_subtree, n); });

            return new_subtree;
        }

        //---------------------------------------------------------------------
        template<class T, template <class> class StorageT>
        void indexed_tree<T, StorageT>::copy_children(index_t from_parent, index_t to_parent)
        {
            foreach_child(from_parent, [&](auto n) { copy_subtree(to_parent, n); });
        }

        //---------------------------------------------------------------------
        template<class T, template <class>  class StorageT>
        inline void indexed_tree<T, StorageT>::move_subtree(index_t to_parent, index_t node)
        {
            detach_subtree(node);
            attach_subtree(to_parent, node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void indexed_tree<T, StorageT>::move_subtree_after_sibling(index_t sibling, index_t node)
        {
            detach_subtree(node);
            attach_subtree_after_sibling(sibling, node);
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        void indexed_tree<T, StorageT>::move_children(index_t from_parent, index_t to_parent)
        {
            foreach_child(from_parent, [&](auto n) { move_subtree(to_parent, n); });
        }

        //---------------------------------------------------------------------
        template<class T, template<class> class StorageT>
        template<class Predicate>
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::find_ancestor(index_t node, Predicate pred) const
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
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::find_child(index_t parent, Predicate pred) const
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
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::find_sibling(index_t node, Predicate pred) const
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
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::find_depth_first(index_t node, Predicate pred) const
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
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::find_breadth_first(index_t node, Predicate pred) const
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
        typename indexed_tree<T, StorageT>::index_t indexed_tree<T, StorageT>::foreach_child(index_t parent, Function vis) const
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
        auto indexed_tree<T, StorageT>::foreach_sibling(index_t node, Function vis) const
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
        auto indexed_tree<T, StorageT>::foreach_depth_first(index_t parent, Function vis) const
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
        auto indexed_tree<T, StorageT>::foreach_breadth_first(index_t parent, Function vis) const
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