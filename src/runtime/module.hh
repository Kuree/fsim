#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <string_view>
#include <string>

namespace xsim::runtime {
class Scheduler;
class Module {
public:
    Module() = delete;
    explicit Module(std::string_view def_name) : def_name(def_name), inst_name(def_name) {}
    explicit Module(std::string_view def_name, std::string_view inst_name)
        : def_name(def_name), inst_name(inst_name) {}
    virtual void init(Scheduler *) {}
    virtual void comb(Scheduler *) {}
    virtual void nba(Scheduler *) {}
    virtual void final(Scheduler *) {}

    std::string_view def_name;
    std::string_view inst_name;

    Module *parent = nullptr;

    [[nodiscard]] std::string hierarchy_name() const;
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
