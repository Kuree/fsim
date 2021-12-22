#ifndef XSIM_IR_HH
#define XSIM_IR_HH

#include <memory>
#include <unordered_set>

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

    // may have event control inside the statements as well, e.g. @(posedge clk)
    std::vector<std::pair<const slang::ValueSymbol *, slang::EdgeKind>> edge_event_controls;
};

class CombProcess : public Process {
public:
    enum class CombKind {
        GeneralPurpose,  // LRM 9.2.2.1
        AlwaysComb,      // LRM 9.2.2.2
        Implicit,        // LRM 9.2.2.2.2
        Explicit,
        Latch  // LRM 9.2.2.3, The always_latch construct is identical to the always_comb construct
               // in terms of simulation
    };

    explicit CombProcess(CombKind kind = CombKind::AlwaysComb)
        : Process(slang::ProceduralBlockKind::AlwaysComb), kind(kind) {}

    std::vector<const slang::Symbol *> sensitive_list;

    CombKind kind;
};

class FFProcess : public Process {
public:
    FFProcess() : Process(slang::ProceduralBlockKind::AlwaysFF) {}
    std::vector<std::pair<slang::EdgeKind, const slang::ValueSymbol *>> edges;
};

class Module {
public:
    explicit Module(const slang::InstanceSymbol *def)
        : name(def->getDefinition().name), def_(def) {}
    std::string_view name;

    std::vector<std::unique_ptr<CombProcess>> comb_processes;
    std::vector<std::unique_ptr<FFProcess>> ff_processes;
    std::vector<std::unique_ptr<Process>> init_processes;
    std::vector<std::unique_ptr<Process>> final_processes;

    std::vector<std::pair<const slang::PortSymbol *, const slang::Expression *>> inputs;
    std::vector<std::pair<const slang::PortSymbol *, const slang::Expression *>> outputs;

    std::string analyze();

    // circular dependencies is not allowed in SV, so shared pointer is fine
    std::map<std::string, std::shared_ptr<Module>> child_instances;

    [[nodiscard]] const slang::InstanceSymbol *def() const { return def_; }

    [[nodiscard]] std::unordered_set<const Module *> get_defs() const;

    [[nodiscard]] std::unordered_set<std::string_view> get_tracked_vars() const;

private:
    const slang::InstanceSymbol *def_;

    std::string analyze_connections();
    std::string analyze_comb();
    std::string analyze_init();
    std::string analyze_ff();
    std::string analyze_final();

    std::string analyze_inst();
};

}  // namespace xsim

#endif  // XSIM_IR_HH
