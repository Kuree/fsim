#include "../../src/runtime/vpi.hh"
#include "gtest/gtest.h"
#include "vpi_user.h"

TEST(vpi, args) {  // NOLINT
    std::string arg1 = "aa";
    std::string arg2 = "bb";
    char *args[2] = {const_cast<char *>(arg1.c_str()), const_cast<char *>(arg2.c_str())};
    {
        auto *vpi = fsim::runtime::VPIController::get_vpi();
        vpi->set_args(2, args);
    }

    {
        s_vpi_vlog_info info;
        vpi_get_vlog_info(&info);
        EXPECT_EQ(info.argc, 2);
        EXPECT_EQ(std::string(info.argv[0]), arg1);
        EXPECT_EQ(std::string(info.argv[1]), arg2);
    }
}
