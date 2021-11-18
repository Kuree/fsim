#ifndef XSIM_IR_HH
#define XSIM_IR_HH

#include <memory>

#include "slang/symbols/ASTVisitor.h"

namespace xsim {

struct Variable {
    const slang::VariableSymbol *sym;
};

struct Port {};

class Process {
public:
    slang::ProceduralBlockKind type;
    std::vector<const slang::Symbol *> stmts;

    explicit Process(slang::ProceduralBlockKind type) : type(type) {}
};

class CombProcess : public Process {
public:
    std::vector<const slang::Symbol *> stmts;

    explicit CombProcess() : Process(slang::ProceduralBlockKind::AlwaysComb) {}

    // dependencies. need to create it for the computation graph
    std::vector<const CombProcess*> edges_to;
    std::vector<const CombProcess*> edges_from;
};

class Module {
public:
    explicit Module(const slang::InstanceSymbol *def) : def_(def) {}
    std::string_view name;

    std::map<std::string_view, std::unique_ptr<Variable>> vars;
    std::vector<std::unique_ptr<CombProcess>> comb_processes;
    std::vector<std::unique_ptr<CombProcess>> comb_timing_process;
    std::vector<std::unique_ptr<Process>> ff_processes;
    std::vector<std::unique_ptr<Process>> init_processes;
    std::vector<std::unique_ptr<Process>> final_processes;

    std::string analyze();

private:
    const slang::InstanceSymbol *def_;

    std::string analyze_vars();
    std::string analyze_comb();
    std::string analyze_init();
    std::string analyze_ff();
    std::string analyze_final();
};

}  // namespace xsim

#endif  // XSIM_IR_HH
