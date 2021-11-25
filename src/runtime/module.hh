#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <memory>
#include <string>
#include <string_view>

#include "logic/logic.hh"

namespace marl {
template <typename T>
class DAG;
}

namespace xsim::runtime {
class Scheduler;
class CombProcess;
class Module {
public:
    Module() = delete;
    explicit Module(std::string_view def_name) : def_name(def_name), inst_name(def_name) {}
    explicit Module(std::string_view def_name, std::string_view inst_name)
        : def_name(def_name), inst_name(inst_name) {}
    virtual void init(Scheduler *) {}
    virtual void comb(Scheduler *) {}
    virtual void always_comb(Scheduler *) {}
    virtual void nba(Scheduler *) {}
    virtual void final(Scheduler *) {}

    std::string_view def_name;
    std::string_view inst_name;

    Module *parent = nullptr;

    [[nodiscard]] std::string hierarchy_name() const;

    // active region
    void active();

protected:
    std::vector<CombProcess *> comb_processes_;

private:
    std::shared_ptr<marl::DAG<void>> comb_dag_;
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
