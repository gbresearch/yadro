#include "../util/gbtest.h"
#include "../util/gblog.h"
#include "../util/gbtimer.h"
#include "../util/misc.h"

#include <sstream>

namespace
{
    using namespace gb::yadro::util;

    GB_TEST(util, logtest)
    {
        std::ostringstream oss;
        logger log(oss);
        log.writeln("abc", 1, 2, tab{ 10, '.' }, "pi=", tab{ 15 }, 3.14);
        gbassert(oss.str() == "abc12.....pi=  3.14\n");
    }

    GB_TEST(util, misc)
    {
        std::int64_t dur{};
        std::uint64_t cnt{};
        {
            accumulating_timer<std::chrono::microseconds> t{
                [&](auto duration, auto count) { dur = duration.count(); cnt = count; }};

            std::mutex m1, m2;
            gbassert(locked_call([] { return 1; }, m1, m2) == 1);

            auto v1{ 0 }, v2{ 0 };
            {
                auto _1{ t.make_scope_timer() };
                raii r{ [&] { v1 = 1; }, [&] { v2 = 2; } };
                gbassert(v1 == 1);
            }
            gbassert(v2 == 2);
        }
        gbassert(dur < 10);
        gbassert(cnt == 1);
    }
}
