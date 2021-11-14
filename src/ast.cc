#include "ast.hh"

namespace xsim {

[[maybe_unused]] void ModuleDefinitionVisitor::handle(const slang::InstanceSymbol &symbol) {
    auto const &def = symbol.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        modules.emplace(def.name, &def);
        visitDefault(symbol);
    }
}

}  // namespace xsim