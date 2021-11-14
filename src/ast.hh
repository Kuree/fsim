#ifndef XSIM_AST_HH
#define XSIM_AST_HH

#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"

namespace xsim {

// visit slang AST nodes
class ModuleDefinitionVisitor : public slang::ASTVisitor<ModuleDefinitionVisitor, true, true> {
public:
    ModuleDefinitionVisitor() = default;

    // only visit modules
    [[maybe_unused]] [[maybe_unused]] void handle(const slang::InstanceSymbol &symbol);

    std::unordered_map<std::string_view, const slang::Definition *> modules;
};

}  // namespace xsim

#endif  // XSIM_AST_HH
