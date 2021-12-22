#include "stmt.hh"

namespace xsim {

VarDeclarationVisitor::VarDeclarationVisitor(std::ostream &s, int &indent_level,
                                             const CXXCodeGenOptions &options,
                                             CodeGenModuleInformation &module_info,
                                             ExprCodeGenVisitor &expr_v)
    : s(s),
      indent_level(indent_level),
      options(options),
      module_info(module_info),
      expr_v(expr_v) {}

void VarDeclarationVisitor::handle(const slang::VariableSymbol &var) {
    // output variable definition
    auto const &t = var.getDeclaredType()->getType();
    auto type_name = get_var_type(t, var.name);
    auto range = t.getFixedRange();
    s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right << "> "
      << var.name;

    auto *init = var.getInitializer();
    if (init) {
        s << " = ";
        init->visit(expr_v);
    }

    s << ";" << std::endl;
    // add it to tracked names
    module_info.add_used_names(var.name);
}

[[maybe_unused]] void VarDeclarationVisitor::handle(const slang::NetSymbol &var) {
    // output variable definition
    auto const &t = var.getDeclaredType()->getType();
    auto type_name = get_var_type(t, var.name);
    auto range = t.getFixedRange();
    s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right << "> "
      << var.name << ";" << std::endl;
    module_info.add_used_names(var.name);
}

[[maybe_unused]] void VarDeclarationVisitor::handle(const slang::VariableDeclStatement &stmt) {
    s << std::endl;
    auto const &v = stmt.symbol;
    handle(v);
}

[[maybe_unused]] void VarDeclarationVisitor::handle(const slang::InstanceSymbol &inst) {
    auto const &def = inst.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        if (!inst_) {
            inst_ = &inst;
            this->template visitDefault(inst);
        } else {
            // don't go deeper
            return;
        }
    }
}

std::string_view VarDeclarationVisitor::get_var_type(const slang::Type &t,
                                                     std::string_view name) const {
    if (options.use_4state) {
        auto four_state = t.isFourState();
        if (four_state) {
            return module_info.var_tracked(name) ? "xsim::runtime::logic_t" : "logic::logic";
        } else {
            return module_info.var_tracked(name) ? "xsim::runtime::bit_t" : "logic::bit";
        }
    } else {
        // force the simulator to use two state even though the original type maybe 4-state
        return module_info.var_tracked(name) ? "xsim::runtime::bit_t" : "logic::bit";
    }
}

StmtCodeGenVisitor::StmtCodeGenVisitor(std::ostream &s, int &indent_level,
                                       const CXXCodeGenOptions &options,
                                       CodeGenModuleInformation &module_info)
    : s(s),
      indent_level(indent_level),
      module_info(module_info),
      expr_v(s, module_info),
      decl_v(s, indent_level, options, module_info, expr_v) {}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::VariableSymbol &var) {
    var.visit(decl_v);
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::NetSymbol &var) { var.visit(decl_v); }

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::VariableDeclStatement &stmt) {
    stmt.visit(decl_v);
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::TimedStatement &stmt) {
    s << std::endl;
    auto const &timing = stmt.timing;
    TimingControlCodeGen timing_codegen(s, indent_level, module_info, expr_v);
    timing_codegen.handle(timing);
    stmt.stmt.visit(*this);
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::StatementBlockSymbol &) {
    // we ignore this one for now
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::StatementList &list) {
    // entering a scope
    s << get_indent(indent_level) << "{";
    indent_level++;
    this->template visitDefault(list);
    indent_level--;
    s << std::endl << get_indent(indent_level) << "}" << std::endl;
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ExpressionStatement &stmt) {
    s << std::endl << get_indent(indent_level);
    stmt.expr.visit(expr_v);
    s << ";";
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ConditionalStatement &stmt) {
    s << std::endl << get_indent(indent_level);
    auto const &cond = stmt.cond;
    s << "if (";
    cond.visit(expr_v);
    s << ")";
    stmt.ifTrue.visit(*this);
    if (stmt.ifFalse) {
        s << get_indent(indent_level) << "else ";
        stmt.ifFalse->visit(*this);
    }
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ContinuousAssignSymbol &sym) {
    s << get_indent(indent_level);
    sym.visitExprs(expr_v);
    s << ";" << std::endl;
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ForLoopStatement &loop) {
    s << get_indent(indent_level) << "for (";
    for (uint64_t i = 0; i < loop.initializers.size(); i++) {
        auto const *expr = loop.initializers[i];
        expr->visit(expr_v);
        if (i != (loop.initializers.size() - 1)) {
            s << ", ";
        }
    }
    s << "; ";
    loop.stopExpr->visit(expr_v);
    s << "; ";
    for (uint64_t i = 0; i < loop.steps.size(); i++) {
        auto const *expr = loop.steps[i];
        expr->visit(expr_v);
        if (i != (loop.steps.size() - 1)) {
            s << ", ";
        }
    }
    s << ")";

    loop.body.visit(*this);
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::RepeatLoopStatement &repeat) {
    // we use repeat as a variable since it won't appear in the code
    s << std::endl << get_indent(indent_level) << "for (auto repeat = 0";
    if (repeat.count.type->isFourState()) {
        s << "_logic";
    } else {
        s << "_bit";
    }
    s << "; repeat < ";
    repeat.count.visit(expr_v);
    s << "; repeat++) {";
    indent_level++;

    repeat.body.visit(*this);

    indent_level--;
    s << std::endl << get_indent(indent_level) << "}";
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ForeverLoopStatement &forever) {
    s << std::endl << get_indent(indent_level) << "while (true) {" << std::endl;
    indent_level++;

    forever.body.visit(*this);

    indent_level--;
    s << std::endl << get_indent(indent_level) << "}";
}

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::InstanceSymbol &inst) {
    auto const &def = inst.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        if (!inst_) {
            inst_ = &inst;
            this->template visitDefault(inst);
        } else {
            // don't go deeper
            return;
        }
    }
}

}  // namespace xsim