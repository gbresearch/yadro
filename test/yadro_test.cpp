#include "../util/gbtest.h"
#include "../util/gblog.h"
#include <thread>
#include <chrono>

int main()
{
    using namespace gb::yadro::util;
    tester::set_verbose(true);
    tester::set_logger("yadro-test.log", std::cout);
    //tester::disable_suites("suite2");
    //tester::disable_test("one", "mytest");
    //tester::set_policy(std::launch::async);
    auto success = tester::run();
    return success ? 0 : -1;
}
