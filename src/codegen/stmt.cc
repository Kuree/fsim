#include "stmt.hh"

#include "../ir/except.hh"

namespace fsim {

constexpr auto START_FORK = "START_FORK";
constexpr auto SCHEDULE_FORK = "SCHEDULE_FORK";
constexpr auto END_FORK_PROCESS = "END_FORK_PROCESS";
constexpr auto SCHEDULE_JOIN_ALL = "SCHEDULE_JOIN";
constexpr auto SCHEDULE_JOIN_ANY = "SCHEDULE_ANY";
constexpr auto SCHEDULE_JOIN_NONE = "SCHEDULE_NONE";

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
    // not interested in formal argument for now
    if (var.kind == slang::SymbolKind::FormalArgument) return;
    auto const &flags = var.flags;
    // we don't generate compiler generated vars
    if (flags.has(slang::VariableFlags::CompilerGenerated)) return;
    // output variable definition
    auto var_type_decl = get_var_decl(var);
    s << get_indent(indent_level) << var_type_decl;

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
    auto var_type_decl = get_var_decl(var);
    s << get_indent(indent_level) << var_type_decl << ";" << std::endl;

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

class TypePrinter {
public:
    TypePrinter(const slang::Symbol &sym, const CodeGenModuleInformation &module_info,
                const CXXCodeGenOptions &options, std::string_view name_prefix)
        : sym_(sym), module_info_(module_info), options_(options), name_prefix_(name_prefix) {
        auto const &t = sym.getDeclaredType()->getType();
        t.visit(*this);

        // CTAD doesn't work with logic::logic
        // need to bypass that
        if (t.kind == slang::SymbolKind::ScalarType) {
            s_ << "<>";
        }
    }

    // this is modeled after slang's TypePrinter
    void visit(const slang::ScalarType &type) {
        if (options_.use_4state) {
            auto four_state = type.isFourState;
            if (four_state) {
                s_ << (module_info_.var_tracked(sym_.name) ? "fsim::runtime::logic_t"
                                                           : "logic::logic");
            } else {
                s_ << (module_info_.var_tracked(sym_.name) ? "fsim::runtime::bit_t" : "logic::bit");
            }
        } else {
            // force the simulator to use two state even though the original type maybe 4-state
            s_ << (module_info_.var_tracked(sym_.name) ? "fsim::runtime::bit_t" : "logic::bit");
        }
    }

    void visit(const slang::PredefinedIntegerType &type) {
        // we use bits here to avoid unnecessary conversion
        auto width = type.getBitWidth();
        auto type_name =
            module_info_.var_tracked(sym_.name) ? "fsim::runtime::bit_t" : "logic::bit";
        s_ << type_name << '<' << width - 1 << ", 0, " << (type.isSigned ? "true" : "false") << ">";
    }
    void visit(const slang::FloatingType &) { handle_not_supported(); }
    void visit(const slang::EnumType &) { handle_not_supported(); }
    void visit(const slang::PackedArrayType &type) {
        std::vector<slang::ConstantRange> dims;
        const slang::PackedArrayType *curr = &type;
        while (true) {
            dims.emplace_back(curr->range);
            if (!curr->elementType.isPackedArray()) break;

            curr = &curr->elementType.getCanonicalType().as<slang::PackedArrayType>();
        }

        // only support one dim packed array so far
        if (dims.size() > 1) {
            throw NotSupportedException("Multi-dimension packed array not supported",
                                        type.location);
        }
        curr->elementType.visit(*this);
        auto const &dim = dims[0];
        s_ << "<" << dim.left << ", " << dim.right << ", " << (type.isSigned ? '1' : '0') << ">";
    }
    void visit(const slang::PackedStructType &) { handle_not_supported(); }
    void visit(const slang::PackedUnionType &) { handle_not_supported(); }
    void visit(const slang::FixedSizeUnpackedArrayType &type) {
        slang::Type const *t = &type;
        do {
            t = t->getArrayElementType();
        } while (t->isUnpackedArray());
        t->visit(*this);

        if (!var_name_printed_) {
            print_name();
            var_name_printed_ = true;
        }

        print_unpacked_array_dim(type);
    }

    void visit(const slang::DynamicArrayType &) { handle_not_supported(); }
    void visit(const slang::AssociativeArrayType &) { handle_not_supported(); }
    void visit(const slang::QueueType &) { handle_not_supported(); }
    void visit(const slang::UnpackedStructType &) { handle_not_supported(); }
    void visit(const slang::UnpackedUnionType &) { handle_not_supported(); }
    void visit(const slang::VoidType &) { handle_not_supported(); }
    void visit(const slang::NullType &) { handle_not_supported(); }
    void visit(const slang::CHandleType &) { handle_not_supported(); }
    void visit(const slang::StringType &) { handle_not_supported(); }
    void visit(const slang::EventType &) { handle_not_supported(); }
    void visit(const slang::UnboundedType &) { handle_not_supported(); }
    void visit(const slang::TypeRefType &) { handle_not_supported(); }
    void visit(const slang::UntypedType &) { handle_not_supported(); }
    void visit(const slang::SequenceType &) { handle_not_supported(); }
    void visit(const slang::PropertyType &) { handle_not_supported(); }
    void visit(const slang::VirtualInterfaceType &) { handle_not_supported(); }
    void visit(const slang::ClassType &) { handle_not_supported(); }
    void visit(const slang::CovergroupType &) { handle_not_supported(); }
    void visit(const slang::TypeAliasType &type) { type.targetType.getType().visit(*this); }
    void visit(const slang::ErrorType &) { handle_not_supported(); }

    template <typename T>
    void visit(const T &) {}

    std::string str() {
        if (!var_name_printed_) {
            print_name();
        }
        return s_.str();
    }

private:
    const slang::Symbol &sym_;
    const CodeGenModuleInformation &module_info_;
    const CXXCodeGenOptions &options_;
    std::string_view name_prefix_;

    std::stringstream s_;
    bool var_name_printed_ = false;

    void handle_not_supported() {
        throw NotSupportedException("Type not supported", sym_.location);
    }

    // NOLINTNEXTLINE
    void print_unpacked_array_dim(const slang::Type &type) {
        switch (type.kind) {
            case slang::SymbolKind::FixedSizeUnpackedArrayType: {
                auto &at = type.as<slang::FixedSizeUnpackedArrayType>();
                auto size = std::abs(at.range.left - at.range.right) + 1;
                s_ << "[" << size << "]";
                break;
            }
            case slang::SymbolKind::DynamicArrayType:
            case slang::SymbolKind::AssociativeArrayType:
            case slang::SymbolKind::QueueType: {
                throw NotSupportedException("Unsupported type", type.location);
            }
            default:
                return;
        }

        print_unpacked_array_dim(type.getArrayElementType()->getCanonicalType());
    }

    void print_name() { s_ << " " << name_prefix_ << sym_.name; }
};

std::string get_symbol_type(const slang::Symbol &sym, const CodeGenModuleInformation &module_info,
                            const CXXCodeGenOptions &options, std::string_view name_prefix) {
    TypePrinter p(sym, module_info, options, name_prefix);
    return p.str();
}

std::string VarDeclarationVisitor::get_var_decl(const slang::Symbol &sym) const {
    return get_symbol_type(sym, module_info, options);
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

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::BlockStatement &stmt) {
    // join
    switch (stmt.blockKind) {
        case slang::StatementBlockKind::JoinAll:
        case slang::StatementBlockKind::JoinAny:
        case slang::StatementBlockKind::JoinNone: {
            auto const &body = stmt.getStatements();
            // depends on the type
            std::vector<const slang::Statement *> stmts;
            auto join_name = module_info.get_new_name("fork");
            if (body.kind == slang::StatementKind::List) {
                auto const &list = body.as<slang::StatementList>();
                stmts = std::vector(list.list.begin(), list.list.end());
            } else {
                stmts = {&body};
            }
            auto fork_size = stmts.size();
            s << get_indent(indent_level) << START_FORK << "(" << join_name << ", " << fork_size
              << ");" << std::endl;
            for (auto const *st : stmts) {
                s << get_indent(indent_level) << "{" << std::endl;
                indent_level++;
                auto p = module_info.enter_process();

                s << get_indent(indent_level)
                  << fmt::format("auto {0} = {1}->create_fork_process();", p,
                                 module_info.scheduler_name())
                  << std::endl;

                s << get_indent(indent_level)
                  << fmt::format("{0}->func = [{0}, {1}, this]() {{", p,
                                 module_info.scheduler_name())
                  << std::endl;
                indent_level++;
                st->visit(*this);
                s << get_indent(indent_level) << END_FORK_PROCESS << "(" << p << ");" << std::endl;
                indent_level--;
                s << get_indent(indent_level) << "};" << std::endl;

                s << get_indent(indent_level) << SCHEDULE_FORK << "(" << join_name << ", " << p
                  << ");" << std::endl;

                module_info.exit_process();
                indent_level--;
                s << get_indent(indent_level) << "}" << std::endl;
            }
            // join
            std::string_view call_name;
            switch (stmt.blockKind) {
                case slang::StatementBlockKind::JoinAll:
                    call_name = SCHEDULE_JOIN_ALL;
                    break;
                case slang::StatementBlockKind::JoinNone:
                    call_name = SCHEDULE_JOIN_NONE;
                    break;
                case slang::StatementBlockKind::JoinAny:
                    call_name = SCHEDULE_JOIN_ANY;
                    break;
                default:
                    // should not reach here
                    break;
            }
            s << get_indent(indent_level) << call_name << "(" << join_name << ", "
              << module_info.scheduler_name() << ", " << module_info.current_process_name() << ");"
              << std::endl;
            break;
        }
        case slang::StatementBlockKind::Sequential: {
            // using the default visit method
            visitDefault(stmt);
            break;
        }
    }
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

void StmtCodeGenVisitor::handle(const slang::CaseStatement &stmt) {
    // we use if statement for other modifiers such as unique
    auto const &target = stmt.expr;
    if (stmt.condition != slang::CaseStatementCondition::Normal) {
        throw NotSupportedException("Only normal case condition supported",
                                    stmt.sourceRange.start());
    }
    auto const &cases = stmt.items;
    for (auto case_idx = 0u; case_idx < cases.size(); case_idx++) {
        if (case_idx == 0) {
            s << get_indent(indent_level) << "if (";
        } else {
            s << " else if (";
        }
        auto const &case_ = cases[case_idx];
        auto const &exprs = case_.expressions;
        for (auto expr_idx = 0u; expr_idx < exprs.size(); expr_idx++) {
            if (expr_idx != (exprs.size() - 1)) {
                s << " || ";
            }
            s << "(";
            target.visit(expr_v);
            s << " == ";
            exprs[expr_idx]->visit(expr_v);
            s << ")";
        }
        s << ") {" << std::endl;
        indent_level++;

        case_.stmt->visit(*this);

        indent_level--;
        s << std::endl << get_indent(indent_level) << "}";
    }

    if (stmt.defaultCase) {
        s << " else {";
        indent_level++;

        stmt.defaultCase->visit(*this);

        indent_level--;
        s << std::endl << get_indent(indent_level) << "}" << std::endl;
    }
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

[[maybe_unused]] void StmtCodeGenVisitor::handle(const slang::ReturnStatement &ret) {
    s << get_indent(indent_level) << "return";
    if (ret.expr) {
        s << " ";
        ret.expr->visit(expr_v);
    }
    s << ";" << std::endl;
}

}  // namespace fsim