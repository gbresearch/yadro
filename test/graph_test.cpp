#include "../util/gbtest.h"
#include "../util/misc.h"
#include "../container/graph.h"
#include "../container/static_string.h"
#include "../container/static_vector.h"
#include "../container/tree.h"
#include "../archive/archive.h"
#include <vector>

namespace
{
    using namespace gb::yadro::container;
    using namespace gb::yadro::util;
    using namespace gb::yadro::archive;

    GB_TEST(yadro, static_string_test)
    {
        static_string<1> s1;
        gbassert(s1.size() == 0);
        const static_string<10> s2(5, 'a');
        gbassert(s2 != s1);
        static_string<11> s3("aaaaa");
        gbassert(s2 == s3);
        gbassert(s2 == "aaaaa");
        gbassert("aaaaa" == s2);
        gbassert("aaaaaB" != s2);
        gbassert("aaaaaB" > s2);
        gbassert(s2 < "aaaaaB");
        s3 += 'B';
        gbassert("aaaaaB" == s3);
        s3 += "BCD";
        gbassert(s3 == "aaaaaBBCD");
        s3 = "AbCdE";
        gbassert(s3 == "AbCdE");
        s3 += s2;
        gbassert(s3 == "AbCdEaaaaa");
        s3 = s2;
        s3 += "AbCdE";
        gbassert(s3 == "aaaaaAbCdE");
        std::fill_n(s3.begin(), 5, 'x');
        gbassert(s3 == "xxxxxAbCdE");
        s3.clear();
        gbassert(!s3);
        s3 = "12345";
        gbassert(s3 + s2 == "12345aaaaa");
        gbassert(s3 + "aaaaa" == "12345aaaaa");
        gbassert(s3 + 'X' == "12345X");
        gbassert(s3 + std::string("-x") == "12345-x");

        std::ostringstream oss;
        oss << s2;
        gbassert(oss.str() == "aaaaa");

        omem_archive ma;
        ma(s2, static_string<11>("123"));
        imem_archive im(std::move(ma));
        static_string<12> s4;
        im(s3, s4);
        gbassert(s2 == s3);
        gbassert(s4 == static_string<11>("123"));
    }

    GB_TEST(yadro, static_vector_test)
    {
        static_vector<int, 1> v1;
        gbassert(v1.empty());
        std::vector v{ 1,2,3,4,5 };
        static_vector<int, 10> v2(v.begin(), v.end());
        gbassert(std::mismatch(v2.begin(), v2.end(), v.begin(), v.end()) == std::pair(v2.end(), v.end()));
        gbassert(v1 < v2);
        gbassert(v2 == v);
        v2.clear();
        gbassert(v1 == v2);
        static_vector<int, 12> v3(4, 5);
        gbassert(v3 == std::vector{5, 5, 5, 5});
        auto v4 = v3;
        gbassert(v3 == v4);
        v4[3] = 10;
        gbassert(v4 == std::vector{ 5,5,5,10 });
        v4.swap(v3);
        gbassert(v3 == std::vector{ 5, 5, 5, 10 });
        gbassert(v4 == std::vector{ 5, 5, 5, 5 });
        v4.assign(10, 7);
        gbassert(v4 == std::vector{ 7,7,7,7,7,7,7,7,7,7 });
        v4.assign(v.begin(), v.end());
        gbassert(v4 == v);
        v4.assign(3, 2);
        gbassert(v4 == std::vector{ 2,2,2 });
        v4.push_back(9);
        gbassert(v4 == std::vector{ 2,2,2,9 });
        v4.emplace_back(7);
        gbassert(v4 == std::vector{ 2,2,2,9,7 });
        v4.pop_back();
        gbassert(v4 == std::vector{ 2,2,2,9 });
        v4.insert(v4.begin() + 2, 111);
        gbassert(v4 == std::vector{ 2,2,111,2,9 });
        v4.insert(v4.begin() + 3, 3, 11);
        gbassert(v4 == std::vector{ 2,2,111,11,11,11,2,9 });
        v4.insert(v4.begin(), v.begin(), v.begin() + 3);
        gbassert(v4 == std::vector{ 1,2,3,2,2,111,11,11,11,2,9 });
        gbassert(v4.capacity() == 12);
        gbassert(!v4.empty());
        gbassert(!v4.full());
        v4.erase(v4.begin() + 2);
        gbassert(v4 == std::vector{ 1,2,2,2,111,11,11,11,2,9 });
        v4.erase(v4.begin() + 1, v4.begin() + 4);
        gbassert(v4 == std::vector{ 1,111,11,11,11,2,9 });
        v4.resize(9);
        gbassert(v4 == std::vector{ 1,111,11,11,11,2,9,0,0 });
        v4.resize(5);
        gbassert(v4 == std::vector{ 1,111,11,11,11 });

        // serialization
        omem_archive ma;
        ma(v, v1, v2, v3, v4);
        v.clear();
        v1.clear();
        v2.clear();
        v3.clear();
        v4.clear();
        imem_archive ima(std::move(ma));
        ima(v, v1, v2, v3, v4);
        gbassert(v == std::vector{ 1,2,3,4,5 });
        gbassert(v1.empty());
        gbassert(v2.empty());
        gbassert(v3 == std::vector{ 5, 5, 5, 10 });
        gbassert(v4 == std::vector{ 1,111,11,11,11 });
    }

    GB_TEST(yadro, tree_test)
    {
        indexed_tree<int> tree1;
        gbassert(tree1.get_value(0) == 0);
        auto c1 = tree1.insert_child(0, 1);
        gbassert(tree1.get_value(c1) == 1);
        auto c2 = tree1.insert_child(0, 2);
        gbassert(tree1.get_value(c2) == 2);
        gbassert(tree1.get_sibling(c2) == c1);
        auto c3 = tree1.insert_after_sibling(c2, 3);
        gbassert(tree1.get_value(c3) == 3);
        gbassert(tree1.get_sibling(c2) == c3);
        tree1.reverse_children(0);
        gbassert(tree1.get_sibling(c1) == c3);
        gbassert(tree1.get_sibling(c2) == tree1.invalid_index);
        gbassert(tree1.get_sibling(c3) == c2);
        tree1.clear();
        gbassert(tree1.empty());

        indexed_tree<int> tree2(123);
        gbassert(tree2.get_value(0) == 123);
        tree2.insert_child(0, 1231);
        tree2.insert_child(0, 1232);
        tree2.insert_child(0, 1233);
        tree2.foreach_child(0, [&](auto id)
            {
                gbassert(id > 0 && id < 4);
                switch (id)
                {
                case 1: gbassert(tree2.get_value(id) == 1231); break;
                case 2: gbassert(tree2.get_value(id) == 1232); break;
                case 3: gbassert(tree2.get_value(id) == 1233); break;
                }
            });

        omem_archive ma;
        ma(tree2);
        imem_archive ima(std::move(ma));
        ima(tree1);
        tree1.foreach_child(0, [&](auto id)
            {
                gbassert(id > 0 && id < 4);
                switch (id)
                {
                case 1: gbassert(tree2.get_value(id) == 1231); break;
                case 2: gbassert(tree2.get_value(id) == 1232); break;
                case 3: gbassert(tree2.get_value(id) == 1233); break;
                }
            });
    }

    GB_TEST(yadro, graph_test)
    {

    }
}