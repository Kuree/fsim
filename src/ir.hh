#ifndef XSIM_IR_HH
#define XSIM_IR_HH

#include <memory>
#include "slang/symbols/Symbol.h"

namespace xsim {

struct Variable {

};

struct Port {

};

class Process {

};

class Module {
public:
    std::string_view name;

    std::unordered_map<std::string, std::unique_ptr<Variable>> vars;
    std::vector<std::unique_ptr<Process>> processes;
};

}

#endif  // XSIM_IR_HH
