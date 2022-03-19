#include "vpi.hh"

#include "version.hh"
#include "vpi_user.h"

namespace xsim::runtime {

constexpr auto SIMULATOR_NAME = "xsim";

std::unique_ptr<VPIController> VPIController::vpi_ = nullptr;

VPIController *VPIController::get_vpi() {
    if (!vpi_) {
        // NOLINTNEXTLINE
        vpi_ = std::unique_ptr<VPIController>(new VPIController());
    }
    return vpi_.get();
}

void VPIController::set_args(int argc, char **argv) {
    args_.reserve(argc);
    for (auto i = 0; i < argc; i++) {
        args_.emplace_back(argv[i]);
    }
}

}  // namespace xsim::runtime

extern "C" {
// raw VPI functions
PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) {
    auto vpi = xsim::runtime::VPIController::get_vpi();
    auto const &args = vpi->get_args();

    vlog_info_p->argc = static_cast<int>(args.size());
    vlog_info_p->argv = const_cast<char**>(args.data());

    vlog_info_p->product = const_cast<char *>(xsim::runtime::SIMULATOR_NAME);
    vlog_info_p->version = const_cast<char *>(xsim::runtime::VERSION);

    return 0;
}
}