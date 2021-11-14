#ifndef XSIM_AST_HH
#define XSIM_AST_HH

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"

namespace xsim {

// visit slang AST nodes
class ModuleDefinitionVisitor : public slang::ASTVisitor<ModuleDefinitionVisitor, false, false> {
public:
    ModuleDefinitionVisitor() = default;

    // only visit modules
    [[maybe_unused]] void handle(const slang::InstanceSymbol &symbol);

    std::unordered_map<std::string_view, const slang::Definition *> modules;
};

// compute complexity for each module definition. small module will be inlined into its parent
// module
class ModuleComplexityVisitor : public slang::ASTVisitor<ModuleComplexityVisitor, true, true> {
    // see
    // https://clang.llvm.org/extra/clang-tidy/checks/readability-function-cognitive-complexity.html
};

class DependencyAnalysisVisitor : public slang::ASTVisitor<DependencyAnalysisVisitor, true, true> {
public:
    struct Node {
        // double linked graph
        std::vector<const Node *> edges_to;
        std::vector<const Node *> edges_from;

        std::string_view name;
    };

    struct Graph {
        std::vector<std::unique_ptr<Node>> nodes;
        std::unordered_map<std::string_view, Node *> node_mapping;

        Node *get_node(const slang::NamedValueExpression *name);

        Node *get_node(std::string_view name) const;
    };

    DependencyAnalysisVisitor();
    explicit DependencyAnalysisVisitor(Graph *graph) : graph(graph) {}

    [[maybe_unused]] void handle(const slang::ContinuousAssignSymbol &stmt);
    [[maybe_unused]] void handle(const slang::ProceduralBlockSymbol &stmt);

    Graph *graph;

    std::string error;

private:
    std::unique_ptr<Graph> graph_;
};

}  // namespace xsim

#endif  // XSIM_AST_HH
