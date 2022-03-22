#include "vpi.hh"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>
#include <iostream>

#include "util.hh"
#include "version.hh"
#include "vpi_user.h"

namespace fsim::runtime {

constexpr auto SIMULATOR_NAME = "fsim";

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

void VPIController::start() {}

void VPIController::end() {}

union vpi_func {
    void (*func)();
    void *ptr;

    void run() const {
        if (!ptr) return;
#ifdef _WIN32
        auto win_func = (LPFNDLLFUNC1)(ptr);
        win_func();
#else
        func();
#endif
    }
};

void VPIController::load(std::string_view lib_path) {
    constexpr auto var_name = "vlog_startup_routines";
    platform::DLOpenHelper dl(lib_path.data(), RTLD_NOW);
    auto *r = dl.ptr;
    if (!r) [[unlikely]] {
        // print out error
        std::cerr << SIMULATOR_NAME << ": " << lib_path << " does not exists. " << std::endl;
        return;
    }
    auto *s = ::dlsym(r, var_name);
    if (!s) {
        std::cerr << SIMULATOR_NAME << ": " << lib_path << " is not a valid VPI library. "
                  << std::endl;
        return;
    }
    // read out the functions and call them
    auto *func_ptrs = reinterpret_cast<void **>(s);
    int i = 0;
    while (true) {
        auto *ptr = func_ptrs[i++];
        if (!ptr) break;
        vpi_func vpi = {};
        vpi.ptr = ptr;
        vpi.func();
    }
    vpi_->vpi_libs_.emplace(r);
}

VPIController::~VPIController() {
    for (auto *p : vpi_libs_) {
        dlclose(p);
    }
}

}  // namespace fsim::runtime

extern "C" {
// raw VPI functions
PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) {
    auto vpi = fsim::runtime::VPIController::get_vpi();
    auto const &args = vpi->get_args();

    vlog_info_p->argc = static_cast<int>(args.size());
    vlog_info_p->argv = const_cast<char **>(args.data());

    vlog_info_p->product = const_cast<char *>(fsim::runtime::SIMULATOR_NAME);
    vlog_info_p->version = const_cast<char *>(fsim::runtime::VERSION);

    return 0;
}
}