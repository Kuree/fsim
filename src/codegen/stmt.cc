#include "stmt.hh"

#include "../ir/except.hh"

namespace fsim {

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
                const CXXCodeGenOptions &options)
        : sym_(sym), module_info_(module_info), options_(options) {
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
        s_ << "logic::bit<" << width - 1 << ", 0, " << (type.isSigned ? "true" : "false") << ">";
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
            s_ << " " << sym_.name;
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
            s_ << " " << sym_.name;
        }
        return s_.str();
    }

private:
    const slang::Symbol &sym_;
    const CodeGenModuleInformation &module_info_;
    const CXXCodeGenOptions &options_;

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
};

std::string VarDeclarationVisitor::get_var_decl(const slang::Symbol &sym) const {
    TypePrinter p(sym, module_info, options);
    return p.str();
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

}  // namespace fsim