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

#include "../util/gbtest.h"
#include "../util/misc.h"
#include "../container/tensor.h"
#include "../container/matrix.h"
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

    GB_TEST(yadro, tensor_test)
    {
        using namespace tensor_operators;
        static_assert(tensor_c< tensor<int, 2, 2>>);
        static_assert(tensor_c< tensor<int>>);

        tensor<int, 2, 2> t0{ 1, 2, 3, 4 };
        gbassert(t0(0, 0) == 1);
        gbassert(t0(1, 0) == 2);
        gbassert(t0(0, 1) == 3);
        gbassert(t0(1, 1) == 4);

        tensor<int, 1, 2, 3> t123{};
        tensor<int> t(1, 2, 3);
        gbassert(t123.index_of(0, 1, 2) == t.index_of(0, 1, 2));
        t(0, 1, 2) = 1;
        t(0, 0, 0) = 2;
        tensor<int> t2(t123);
        gbassert(t2 == t123);
        t2(0, 0, 0) = 3;
        t123 = t2;
        gbassert(t2 == t123);
        tensor<int, 1, 2, 3> t3;
        t3 = t2;
        gbassert(t2 == t3);
        gbassert(t123 == t3);

        // test serialization
        omem_archive ma;
        ma(t0, t);
        imem_archive im(std::move(ma));
        auto [t01, t1] = deserialize< tensor<int, 2, 2>, tensor<int>>(im);
        gbassert(t0 == t01);
        gbassert(t == t1);
    }

    GB_TEST(yadro, matrix_test)
    {
        using namespace tensor_operators;
        static_assert(tensor_c< tensor<int, 2, 2>>);
        static_assert(tensor_c< tensor<int>>);

        matrix<double, 2, 3> m{};
        m(0, 0) = 0;
        m(1, 0) = 1;
        matrix<double> m23(2, 3);
        m23(0, 0) = 0;
        m23(1, 0) = 1;

        gbassert(m.index_of(0, 0) == m23.index_of(0, 0));
        gbassert(m.index_of(1, 0) == m23.index_of(1, 0));
        gbassert(m.index_of(0, 1) == m23.index_of(0, 1));
        gbassert(m.index_of(1, 1) == m23.index_of(1, 1));
        gbassert(m == m23);
        m23(1, 1) = 11;
        m = m23;
        gbassert(m == m23);

        matrix<double> m1(m);
        gbassert(m == m1);
        matrix<double, 2, 3> m2(m);
        gbassert(m == m2);

        matrix<double, 2, 2> m3;
        m3(0, 0) = 1;
        m3(0, 1) = 1;
        m3(1, 0) = 2;
        m3(1, 1) = 5;
        gbassert(minor_view(m3, 0, 0)(0,0) == 5);
        auto mv = minor_view(m3, 0, 0);
        static_assert(matrix_c<decltype(mv)>);
        gbassert(determinant(m3) == 3);

        auto solution = solve(m3, matrix<double, 2, 1>{2., 7.});
        gbassert(solution == matrix<double, 2, 1>{1., 1.});

        auto inverted = invert(m3);
        gbassert(inverted == matrix<double, 2, 2>{1., 0., -0.2, 0.2});
        gbassert(m3 * inverted == identity_matrix<double>(2));
    }
}
