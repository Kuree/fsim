#ifndef XSIM_IR_HH
#define XSIM_IR_HH

#include <memory>
#include "slang/symbols/ASTVisitor.h"

namespace xsim {

struct Variable {
    const slang::VariableSymbol *sym;
};

struct Port {

};

class Process {
public:
    slang::ProceduralBlockKind type;

    std::vector<const slang::Symbol*> stmts;
};

class Module {
public:
    explicit Module(const slang::InstanceSymbol *def): def_(def) {}
    std::string_view name;

    std::map<std::string_view, std::unique_ptr<Variable>> vars;
    std::vector<std::unique_ptr<Process>> processes;

    std::string analyze();

private:
    const slang::InstanceSymbol *def_;
};

}

#endif  // XSIM_IR_HH
