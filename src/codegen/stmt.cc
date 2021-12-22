#include "stmt.hh"

namespace xsim {
std::string_view get_indent(int indent_level) {
    static std::unordered_map<int, std::string> cache;
    if (cache.find(indent_level) == cache.end()) {
        std::stringstream ss;
        for (auto i = 0; i < indent_level; i++) ss << "    ";
        cache.emplace(indent_level, ss.str());
    }
    return cache.at(indent_level);
}

CodeGenVisitor::CodeGenVisitor(std::ostream &s, int &indent_level, const CXXCodeGenOptions &options,
                               CodeGenModuleInformation &module_info)
    : s(s),
      indent_level(indent_level),
      options(options),
      module_info(module_info),
      expr_v(s, module_info) {}

void CodeGenVisitor::handle(const slang::VariableSymbol &var) {
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

void CodeGenVisitor::handle(const slang::NetSymbol &var) {
    // output variable definition
    auto const &t = var.getDeclaredType()->getType();
    auto type_name = get_var_type(t, var.name);
    auto range = t.getFixedRange();
    s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right << "> "
      << var.name << ";" << std::endl;
    module_info.add_used_names(var.name);
}

void CodeGenVisitor::handle(const slang::VariableDeclStatement &stmt) {
    s << std::endl;
    auto const &v = stmt.symbol;
    handle(v);
}

void CodeGenVisitor::handle(const slang::TimedStatement &stmt) {
    s << std::endl;
    auto const &timing = stmt.timing;
    switch (timing.kind) {
        case slang::TimingControlKind::Delay: {
            // we first release the current condition holds
            auto const &delay = timing.as<slang::DelayControl>();

            s << get_indent(indent_level)
              << fmt::format("{0}({1}, (", xsim_schedule_delay, module_info.current_process_name());
            delay.expr.visit(expr_v);
            s << fmt::format(").to_uint64(), {0}, {1});", module_info.scheduler_name(),
                             module_info.get_new_name(xsim_next_time, false));

            stmt.stmt.visit(*this);
            break;
        }
        case slang::TimingControlKind::SignalEvent: {
            auto const &single_event = timing.as<slang::SignalEventControl>();
            s << get_indent(indent_level)
              << fmt::format("SCHEDULE_EDGE({0}, ", module_info.current_process_name());
            single_event.expr.visit(expr_v);
            s << ", xsim::runtime::Process::EdgeControlType::";
            switch (single_event.edge) {
                case slang::EdgeKind::PosEdge:
                    s << "posedge";
                    break;
                case slang::EdgeKind::NegEdge:
                    s << "negedge";
                    break;
                case slang::EdgeKind::None:
                case slang::EdgeKind::BothEdges:
                    s << "both";
                    break;
            }
            s << ");";
            stmt.stmt.visit(*this);
            break;
        }
        default: {
            throw std::runtime_error(
                fmt::format("Unsupported timing control {0}", slang::toString(timing.kind)));
        }
    }
}

void CodeGenVisitor::handle(const slang::StatementBlockSymbol &) {
    // we ignore this one for now
}

void CodeGenVisitor::handle(const slang::StatementList &list) {
    // entering a scope
    s << get_indent(indent_level) << "{";
    indent_level++;
    this->template visitDefault(list);
    indent_level--;
    s << std::endl << get_indent(indent_level) << "}" << std::endl;
}

void CodeGenVisitor::handle(const slang::ExpressionStatement &stmt) {
    s << std::endl << get_indent(indent_level);
    stmt.expr.visit(expr_v);
    s << ";";
}

void CodeGenVisitor::handle(const slang::ConditionalStatement &stmt) {
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

void CodeGenVisitor::handle(const slang::ContinuousAssignSymbol &sym) {
    s << get_indent(indent_level);
    sym.visitExprs(expr_v);
    s << ";" << std::endl;
}

void CodeGenVisitor::handle(const slang::ForLoopStatement &loop) {
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

void CodeGenVisitor::handle(const slang::RepeatLoopStatement &repeat) {
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

void CodeGenVisitor::handle(const slang::ForeverLoopStatement &forever) {
    s << std::endl << get_indent(indent_level) << "while (true) {" << std::endl;
    indent_level++;

    forever.body.visit(*this);

    indent_level--;
    s << std::endl << get_indent(indent_level) << "}";
}

void CodeGenVisitor::handle(const slang::InstanceSymbol &inst) {
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

std::string_view CodeGenVisitor::get_var_type(const slang::Type &t, std::string_view name) const {
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

}  // namespace xsim