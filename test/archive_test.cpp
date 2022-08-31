#include "../util/gbtest.h"
#include "../archive/archive.h"
#include <sstream>

namespace
{
    using namespace gb::yadro::archive;
    using namespace gb::yadro::util;

    struct A
    {
        int i{};
        double d{};

        ~A() {}

        template<class Ar>
        auto serialize(Ar& a)
        {
            a(i, d);
        }

        template<class Ar>
        auto serialize(Ar& a) const
        {
            a(i, d);
        }
    };

    struct B { int i; double d; } b;

    template<class A, class C>
    auto serialize(A&& a, C&& b) requires(std::is_same_v<B, std::remove_cvref_t<C>>)
    { 
        std::invoke(std::forward<A>(a), std::forward<C>(b).i, std::forward<C>(b).d);
    }

    GB_TEST(yadro, serialization, std::launch::async)
    {
        A a(555, 1.23);
        B b{ 999, -1.99 };
        A aa;
        B bb;
        int i{123};
        double d{3.14};
        std::string s{"hello"};
        enum class enum_type { one = 1, two, three } e { enum_type::one };
        std::tuple t{ 10, 20.2, std::string("tuple") };
        std::vector v{ 10, 11, 12, 13, 14, 15 };
        std::array<int, 5> iarr{ 5,4,3,2,1 };
        std::queue<int> q;
        q.push(100);
        q.push(200);
        q.push(300);
        std::map<int, std::string> m{ {1, "one"}, {2, "two"}, {3, "three"} };
        std::unordered_map<int, std::string> um{ {1, "one"}, {2, "two"}, {3, "three"} };
        std::optional<int> opt1;
        std::optional<int> opt2{111};
        std::variant<int, char, std::string> v1;
        std::variant<int, char, std::string> v2{888};
        std::variant<int, char, std::string> v3{ 'A'};
        std::variant<int, char, std::string> v4{ "variant"};
        auto compare_arrays = [](const auto& arr1, const auto& arr2) { return std::equal(arr1.begin(), arr1.end(), arr2.begin(), arr2.end()); };

        // text_archive tests
        text_archive<std::ostringstream> oarch;
        oarch(123, a, b, std::string("hello"), 3.14, serialize_as<int>(e), t, std::array<int, 5>{ 5,4,3,2,1 });
        //std::cout << oarch.get_stream().str() << "\n";
        gbassert(oarch.get_stream().str() == "123\n555\n1.23\n999\n-1.99\n5\nhello\n3.14\n1\n10\n20.2\n5\ntuple\n5\n4\n3\n2\n1\n");

        text_archive<std::istringstream> iarch{ oarch.get_stream().str() };
        iarch(i, aa, bb, s, d, serialize_as<int>(e), t, iarr);
        gbassert(i == 123);
        gbassert(std::tie(aa.i, aa.d) == std::tie(a.i, a.d));
        gbassert(std::tie(bb.i, bb.d) == std::tie(b.i, b.d));
        gbassert(s == "hello");
        gbassert(d == 3.14);
        gbassert(e == enum_type::one);
        gbassert(t == std::tuple{ 10, 20.2, std::string("tuple") });
        gbassert(compare_arrays(iarr, std::array<int, 5>{ 5, 4, 3, 2, 1 }));

        // memory archives tests
        omem_archive ma;
        ma(123, a, b, std::string("Hello World"), 3.14, serialize_as<int>(enum_type::two), std::tuple{ 20, 40.4, std::string("tuple2") }, v, q, m, um, 
            opt1, opt2, v1, v2, v3, v4, std::array<int, 5>{ 5, 4, 3, 2, 1 });
        imem_archive ia(std::move(ma));
        std::queue<int> qq;
        ia(i, aa, bb, s, d, serialize_as<int>(e), t, v, qq, m, um, opt1, opt2, v1, v2, v3, v4, iarr);
        gbassert(i == 123);
        gbassert(std::tie(aa.i, aa.d) == std::tie(a.i, a.d));
        gbassert(std::tie(bb.i, bb.d) == std::tie(b.i, b.d));
        gbassert(s == "Hello World");
        gbassert(d == 3.14);
        gbassert(e == enum_type::two);
        gbassert(t == std::tuple{ 20, 40.4, std::string("tuple2") });
        gbassert(v == std::vector{ 10, 11, 12, 13, 14, 15 });
        gbassert(qq.front() == 100); qq.pop();
        gbassert(qq.front() == 200); qq.pop();
        gbassert(qq.front() == 300); qq.pop();
        gbassert(m == std::map<int, std::string>{ {1, "one"}, {2, "two"}, {3, "three"} });
        gbassert(um == std::unordered_map<int, std::string>{ {1, "one"}, { 2, "two" }, { 3, "three" } });
        gbassert(opt1 == std::optional<int>{});
        gbassert(opt2 == std::optional<int>{111});
        gbassert(v1 == std::variant<int, char, std::string>{});
        gbassert(v2 == std::variant<int, char, std::string>{888});
        gbassert(v3 == std::variant<int, char, std::string>{'A'});
        gbassert(v4 == std::variant<int, char, std::string>{"variant"});
        gbassert(compare_arrays(iarr, std::array<int, 5>{ 5, 4, 3, 2, 1 }));

        // binary file archive test
        bin_archive<std::ofstream> ofs("archive_test.eraseme", std::ios::binary);
        ofs(123, a, b, std::string("Hello World"), 3.14, serialize_as<int>(enum_type::three), std::tuple{ 30, 50.55, std::string("tuple three") }, 
            std::vector{ 20, 21, 22, 23, 24, 25 }, q, m, um, opt1, opt2, v1, v2, v3, v4, std::array<int, 5>{ 5, 4, 3, 2, 1 });
        ofs.get_stream().close();
        bin_archive<std::ifstream> ifs("archive_test.eraseme", std::ios::binary);
        ifs(i, aa, bb, s, d, serialize_as<int>(e), t, v, qq, m, um, opt1, opt2, v1, v2, v3, v4, iarr);
        ifs.get_stream().close();
        gbassert(i == 123);
        gbassert(std::tie(aa.i, aa.d) == std::tie(a.i, a.d));
        gbassert(std::tie(bb.i, bb.d) == std::tie(b.i, b.d));
        gbassert(s == "Hello World");
        gbassert(d == 3.14);
        gbassert(e == enum_type::three);
        gbassert(t == std::tuple{ 30, 50.55, std::string("tuple three") });
        gbassert(v == std::vector{ 20, 21, 22, 23, 24, 25 });
        gbassert(qq.front() == 100); qq.pop();
        gbassert(qq.front() == 200); qq.pop();
        gbassert(qq.front() == 300); qq.pop();
        gbassert(m == std::map<int, std::string>{ {1, "one"}, { 2, "two" }, { 3, "three" } });
        gbassert(um == std::unordered_map<int, std::string>{ {1, "one"}, { 2, "two" }, { 3, "three" } });
        gbassert(opt1 == std::optional<int>{});
        gbassert(opt2 == std::optional<int>{111});
        gbassert(v1 == std::variant<int, char, std::string>{});
        gbassert(v2 == std::variant<int, char, std::string>{888});
        gbassert(v3 == std::variant<int, char, std::string>{'A'});
        gbassert(v4 == std::variant<int, char, std::string>{"variant"});
        gbassert(compare_arrays(iarr, std::array<int, 5>{ 5, 4, 3, 2, 1 }));
        std::remove("archive_test.eraseme");
    }

}