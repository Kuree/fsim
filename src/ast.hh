#ifndef XSIM_AST_HH
#define XSIM_AST_HH

#include <unordered_set>

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"

namespace xsim {

// visit slang AST nodes
class ModuleDefinitionVisitor : public slang::ASTVisitor<ModuleDefinitionVisitor, false, false> {
public:
    ModuleDefinitionVisitor() = default;

    // only visit modules
    [[maybe_unused]] void handle(const slang::InstanceSymbol &symbol);

    std::unordered_map<std::string_view, const slang::InstanceSymbol *> modules;
};

// compute complexity for each module definition. small module will be inlined into its parent
// module
class ModuleComplexityVisitor : public slang::ASTVisitor<ModuleComplexityVisitor, true, true> {
    // see
    // https://clang.llvm.org/extra/clang-tidy/checks/readability-function-cognitive-complexity.html
public:
    [[maybe_unused]] void handle(const slang::AssignmentExpression &stmt);
    [[maybe_unused]] void handle(const slang::ConditionalStatement &stmt);
    [[maybe_unused]] void handle(const slang::CaseStatement &stmt);
    [[maybe_unused]] void handle(const slang::ForLoopStatement &stmt);

    uint64_t complexity = 0;

private:
    uint64_t current_level_ = 1;
};

class VariableDefinitionVisitor
    : public slang::ASTVisitor<VariableDefinitionVisitor, false, false> {
public:
    VariableDefinitionVisitor() = default;

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt);

    std::vector<const slang::VariableSymbol *> vars;
};

class DependencyAnalysisVisitor : public slang::ASTVisitor<DependencyAnalysisVisitor, true, true> {
public:
    struct Node {
        explicit Node(const slang::Symbol &symbol) : symbol(symbol) {}
        // double linked graph
        std::unordered_set<const Node *> edges_to;
        std::unordered_set<const Node *> edges_from;

        const slang::Symbol &symbol;
    };

    struct Graph {
    public:
        std::vector<std::unique_ptr<Node>> nodes;
        std::unordered_map<std::string, Node *> node_mapping;

        Node *get_node(const slang::NamedValueExpression *name);
        Node *get_node(const std::string &name) const;
        Node *get_node(const slang::Symbol &symbol);

    private:
        uint64_t procedural_blk_count_ = 0;
        std::unordered_map<const slang::Symbol *, std::string> new_names_;
    };

    DependencyAnalysisVisitor() : DependencyAnalysisVisitor(nullptr) {}
    explicit DependencyAnalysisVisitor(const slang::Symbol *target);
    DependencyAnalysisVisitor(const slang::Symbol *target, Graph *graph)
        : graph(graph), target_(target) {}

    [[maybe_unused]] void handle(const slang::ContinuousAssignSymbol &stmt);
    [[maybe_unused]] void handle(const slang::ProceduralBlockSymbol &stmt);
    [[maybe_unused]] void handle(const slang::NetSymbol &sym);
    [[maybe_unused]] void handle(const slang::InstanceSymbol &symbol);

    Graph *graph;
    std::vector<const slang::ProceduralBlockSymbol*> timed_stmts;

    std::string error;

private:
    std::unique_ptr<Graph> graph_;
    const slang::Symbol *target_;
};

class ProcedureBlockVisitor : public slang::ASTVisitor<ProcedureBlockVisitor, false, false> {
public:
    ProcedureBlockVisitor(const slang::InstanceSymbol *target, slang::ProceduralBlockKind kind)
        : target_(target), kind_(kind) {}

    [[maybe_unused]] void handle(const slang::ProceduralBlockSymbol &stmt);
    [[maybe_unused]] void handle(const slang::InstanceSymbol &symbol);

    std::vector<const slang::ProceduralBlockSymbol *> stmts;

private:
    const slang::InstanceSymbol *target_;
    slang::ProceduralBlockKind kind_;
};

}  // namespace xsim

#endif  // XSIM_AST_HH
