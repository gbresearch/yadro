
#include "../include/tree.h"
#include <iostream>

using namespace gbr::container;

template<class Tree>
void calls(Tree& t)
{
    t.get_value(0) = typename Tree::data_t{};
    t.insert_child(0, t.get_value(0));
    t.insert_child(0);
    t.insert_after_sibling(1, t.get_value(0));
    t.detach_subtree(0);
    t.delete_subtree(1);
    t.copy_subtree(0, 1);
    t.copy_subtree_after_sibling(1, 10);
    t.copy_children(0, 1);
    t.move_subtree(0, 1);
    t.move_subtree_after_sibling(1, 20);
    t.move_children(12, 13);
    t.find_ancestor(100, [](auto n) { return n == 5; });
    t.find_child(100, [](auto n) { return n == 5; });
    t.find_sibling(100, [](auto n) { return n == 5; });
    t.find_depth_first(100, [](auto n) { return n == 5; });
    t.find_breadth_first(100, [](auto n) { return n == 5; });
    t.foreach_child(100, [](auto n) { return n == 5; });
    t.foreach_child(100, [&](auto n) { std::cout << n; });
    t.foreach_sibling(100, [](auto n) { return n == 5; });
    t.foreach_sibling(100, [&](auto n) { std::cout << n; });
    t.foreach_depth_first(100, [](auto n) { return n == 5; });
    t.foreach_depth_first(100, [&](auto n) { std::cout << n; });
    t.foreach_breadth_first(100, [](auto n) { return n == 5; });
    t.foreach_breadth_first(100, [&](auto n) { std::cout << n; });
}

void test()
{
    indexed_tree<int> tint;
    static_assert(tint.has_data);
    calls(tint);
    indexed_tree<void> tvoid;
    static_assert(!tvoid.has_data);
    indexed_tree<std::string> tstr;
    static_assert(tstr.has_data);
    static_assert(std::is_class_v<std::string>);
    calls(tstr);
}
