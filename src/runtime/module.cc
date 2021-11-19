#include "module.hh"

#include "fmt/format.h"

namespace xsim::runtime {
std::string Module::hierarchy_name() const {
    std::string result = std::string(inst_name);
    auto const *module = this->parent;
    while (module) {
        result = fmt::format("{0}.{1}", module->inst_name, result);
        module = module->parent;
    }

    return result;
}
}  // namespace xsim::runtime