#include "archive_traits.h"
#include "archive.h"

#include <iostream>
#include <vector>
#include <sstream>

namespace
{
    using namespace gb::yadro::archive;

    struct A
    {
        int i;
        double d;

        ~A() {}

        template<class Ar>
        auto serialize(Ar& a)
        {
            a(i, d);
        }

        template<class Ar>
        auto serialize(Ar& a) const
        {
            a(i, 3.14);
        }
    };

    struct B { int i; double d; } b;

    template<class A>
    auto serialize(A& a, B& b) { a(b.i, b.d); }

    //template<class A>
    //auto serialize(A& a, const B& b) { a(b.i, b.d); }

    void test()
    {
        static_assert(is_readable_v<std::istream>);

        //archive aout{ std::cout };
        //aout(125);
        //int i{ 0 };

        //archive ain(std::cin);
        //archive{ std::cin }(i);

        //A aa{ 1, 2. };
        //aa.serialize(aout);
        //aa.serialize(ain);
        //aout(A{ 1, 1 });
        //aout(aa);
        //ain(aa);

        //aout(b);
        //ain(b);

        //std::vector<A> v{ { 1, 2.0 }, { 2, 15.1 } };
        //aout(v);
        //ain(v);

        //std::istringstream iss;
        //archive aiss(iss);
        //aiss(aa);

        //std::ostringstream oss;
        //archive aoss(oss);
        //aoss(aa);
    }
}
