#include "expr.hh"

namespace xsim {

auto constexpr xsim_schedule_nba = "SCHEDULE_NBA";

const slang::Symbol *get_parent_symbol(const slang::Symbol *symbol,
                                       std::vector<std::string_view> &paths) {
    auto scope = symbol->getParentScope();
    auto current = symbol;
    if (scope && symbol->kind == slang::SymbolKind::InstanceBody) {
        auto parents =
            scope->getCompilation().getParentInstances(symbol->as<slang::InstanceBodySymbol>());
        if (parents.empty()) return nullptr;

        current = parents[0];
        scope = current->getParentScope();
        paths.emplace_back(current->name);
    }
    return &scope->asSymbol();
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::StringLiteral &str) {
    s << str.getRawValue();
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::IntegerLiteral &i) {
    auto v = i.getValue();
    auto uint_opt = v.as<int>();
    int value = uint_opt ? *uint_opt : 0;
    // depends on the width, we codegen differently
    if (i.getEffectiveWidth()) {
        auto bit_width = i.getValue().getBitWidth();
        if (bit_width != 32) {
            s << "logic::bit<" << (bit_width - 1) << ">(" << value << ")";
            return;
        }
    }
    s << value;
    s << "_bit";
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::NamedValueExpression &n) {
    handle(n.symbol);
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ValueSymbol &sym) {
    // if the current symbol is not null, we need to resolve the hierarchy
    auto const *parent = &sym.getParentScope()->asSymbol();
    auto const *top_body = &module_info_.current_module->def()->body;
    if (!parent || parent == top_body) {
        s << sym.name;
    } else {
        // different path, need to generate the path
        std::vector<std::string_view> paths;
        do {
            parent = get_parent_symbol(parent, paths);
        } while (parent && parent != top_body);
        std::reverse(paths.begin(), paths.end());
        for (auto const &p : paths) {
            s << p << "->";
        }
        s << sym.name;
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ConversionExpression &c) {
    auto const &t = *c.type;
    s << "(";
    c.operand().visit(*this);
    s << ")";
    if (c.operand().kind != slang::ExpressionKind::IntegerLiteral) {
        if (c.operand().type->getBitWidth() > t.getBitWidth()) {
            // this is a slice
            s << ".slice<" << t.getFixedRange().left << ", " << t.getFixedRange().right << ">()";
        } else if (c.operand().type->getBitWidth() < t.getBitWidth()) {
            // it's an extension
            auto size = std::abs(t.getFixedRange().left - t.getFixedRange().right) + 1;
            s << ".extend<" << size << ">()";
        }
        if (c.operand().type->isSigned() && !t.isSigned()) {
            s << ".to_unsigned()";
        } else if (!c.operand().type->isSigned() && t.isSigned()) {
            s << ".to_signed()";
        }
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::LValueReferenceExpression &) {
    if (!left_ptr) throw std::runtime_error("Unable to determine LValue");
    left_ptr->visit(*this);
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::UnaryExpression &expr) {
    auto const &op = expr.operand();
    s << "(";
    // simple ones that have C++ operator overloaded
    bool handled = true;
    switch (expr.op) {
        case slang::UnaryOperator::Minus:
            s << "-";
            op.visit(*this);
            break;
        case slang::UnaryOperator::Plus:
            s << "+";
            op.visit(*this);
            break;
        case slang::UnaryOperator::Predecrement:
            s << "--";
            op.visit(*this);
            break;
        case slang::UnaryOperator::Preincrement:
            s << "++";
            op.visit(*this);
            break;
        case slang::UnaryOperator::LogicalNot:
            s << "!";
            op.visit(*this);
            break;
        case slang::UnaryOperator::BitwiseNot:
            s << "~";
            op.visit(*this);
            break;
        case slang::UnaryOperator::Postdecrement:
            op.visit(*this);
            s << "--";
            break;
        case slang::UnaryOperator::Postincrement:
            op.visit(*this);
            s << "++";
            break;
        default:
            handled = false;
    }
    // complex one that uses special function calls
    switch (expr.op) {
        case slang::UnaryOperator::BitwiseAnd:
            op.visit(*this);
            s << ").r_and(";
            break;
        case slang::UnaryOperator::BitwiseOr:
            op.visit(*this);
            s << ").r_or(";
            break;
        case slang::UnaryOperator::BitwiseXor:
            op.visit(*this);
            s << ").r_xor(";
            break;
        case slang::UnaryOperator::BitwiseNor:
            op.visit(*this);
            s << ").r_nor(";
            break;
        case slang::UnaryOperator::BitwiseNand:
            op.visit(*this);
            s << ").nand(";
            break;
        case slang::UnaryOperator::BitwiseXnor:
            op.visit(*this);
            s << ").r_xnor(";
            break;
        default:
            if (!handled)
                throw std::runtime_error(
                    fmt::format("Unsupported operator {0}", slang::toString(expr.op)));
    }
    // the one with special function calls
    s << ")";
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::BinaryExpression &expr) {
    auto const &left = expr.left();
    auto const &right = expr.right();
    s << "(";
    left.visit(*this);
    bool closing_p = false;
    switch (expr.op) {
        case slang::BinaryOperator::Add:
            s << " + ";
            break;
        case slang::BinaryOperator::Subtract:
            s << " - ";
            break;
        case slang::BinaryOperator::Multiply:
            s << " * ";
            break;
        case slang::BinaryOperator::Mod:
            s << " % ";
            break;
        case slang::BinaryOperator::ArithmeticShiftLeft:
            s << ".ashl(";
            closing_p = true;
            break;
        case slang::BinaryOperator::ArithmeticShiftRight:
            s << ".ashr(";
            closing_p = true;
            break;
        case slang::BinaryOperator::BinaryAnd:
            s << " & ";
            break;
        case slang::BinaryOperator::BinaryOr:
            s << " | ";
            break;
        case slang::BinaryOperator::BinaryXor:
            s << " ^ ";
            break;
        case slang::BinaryOperator::Equality:
            s << " == ";
            break;
        case slang::BinaryOperator::Inequality:
            s << " != ";
            break;
        case slang::BinaryOperator::LogicalAnd:
            s << " && ";
            break;
        case slang::BinaryOperator::LogicalOr:
            s << " || ";
            break;
        case slang::BinaryOperator ::LessThan:
            s << " < ";
            break;
        case slang::BinaryOperator::LessThanEqual:
            s << " <= ";
            break;
        case slang::BinaryOperator::GreaterThan:
            s << " > ";
            break;
        case slang::BinaryOperator::GreaterThanEqual:
            s << " >= ";
            break;
        case slang::BinaryOperator::LogicalShiftLeft:
            s << " << ";
            break;
        case slang::BinaryOperator::LogicalShiftRight:
            s << " >> ";
            break;
        case slang::BinaryOperator::CaseEquality:
            s << ".match(";
            closing_p = true;
            break;
        case slang::BinaryOperator::CaseInequality:
            s << ".nmatch(";
            closing_p = true;
            break;
        default:
            throw std::runtime_error(
                fmt::format("Unsupported operator {0}", slang::toString(expr.op)));
    }
    right.visit(*this);
    if (closing_p) s << ")";
    s << ")";
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ElementSelectExpression &sym) {
    auto const &value = sym.value();
    auto const &selector = sym.selector();
    // TODO: detect if the selector is constant, then use templated implementation
    value.visit(*this);
    s << ".get(";
    selector.visit(*this);
    s << ")";
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ConcatenationExpression &sym) {
    s << "logic::concat(";
    auto const &operands = sym.operands();
    for (auto i = 0u; i < operands.size(); i++) {
        operands[i]->visit(*this);
        if (i != (operands.size() - 1)) {
            s << ", ";
        }
    }
    s << ")";
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::CallExpression &expr) {
    if (expr.subroutine.index() == 1) {
        auto const &info = std::get<1>(expr.subroutine);
        // remove the leading $
        auto name = info.subroutine->name.substr(1);
        auto func_name = fmt::format("xsim::runtime::{0}", name);
        s << func_name << "(";
        // depends on the context, we may or may not insert additional arguments
        if (name == "finish" || name == "time") {
            s << module_info_.scheduler_name();
        } else {
            s << "this";
        }

        auto const &arguments = expr.arguments();
        for (auto const &arg : arguments) {
            s << ", ";
            arg->visit(*this);
        }
        s << ")";
    } else {
        const auto &symbol = *std::get<0>(expr.subroutine);
        throw std::runtime_error(fmt::format("Not yet implemented for symbol {0}", symbol.name));
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::AssignmentExpression &expr) {
    if (expr.isNonBlocking()) {
        auto const &left = expr.left();
        auto const &right = expr.right();
        if (left.kind == slang::ExpressionKind::Concatenation) {
            // unpack
            // since this is very uncommon, we don't compare for update. also, inline these
            // two statements
            s << "{ auto wire = ";

            right.visit(*this);
            s << "; ";
            s << module_info_.current_process_name() << "->schedule_nba([this, wire]() { ";
            s << "wire.unpack(";
            auto const &concat = left.as<slang::ConcatenationExpression>();
            output_concat(concat);
            s << "); }); }";
        } else {
            s << xsim_schedule_nba << "(";
            left.visit(*this);
            // put extra paraphrases to escape , in macro.
            // typically, what happens is slice<a, b> is treated as two arguments
            // since we're no declaring types here, it should be fine
            s << ", (";
            right.visit(*this);
            s << "), " << module_info_.current_process_name() << ")";
        }
    } else {
        auto const &left = expr.left();
        auto const &right = expr.right();
        // detect unpacking syntax
        if (left.kind == slang::ExpressionKind::Concatenation) {
            // this is unpack
            s << "(";
            right.visit(*this);
            s << ").unpack(";
            auto const &concat = left.as<slang::ConcatenationExpression>();
            output_concat(concat);
            s << ")";
        } else {
            left.visit(*this);
            left_ptr = &left;
            s << " = ";
            right.visit(*this);
        }
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::output_concat(
    const slang::ConcatenationExpression &concat) {
    auto const &operands = concat.operands();
    for (auto i = 0u; i < operands.size(); i++) {
        operands[i]->visit(*this);
        if (i != (operands.size() - 1)) {
            s << ", ";
        }
    }
}
}  // namespace xsim