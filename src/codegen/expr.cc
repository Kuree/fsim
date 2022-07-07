#include "expr.hh"

#include "../ir/except.hh"
#include "slang/syntax/AllSyntax.h"
#include "util.hh"

namespace fsim {

auto constexpr fsim_schedule_nba = "SCHEDULE_NBA";
auto constexpr fsim_next_time = "fsim_next_time";
auto constexpr fsim_schedule_delay = "SCHEDULE_DELAY";

const slang::Symbol *get_parent_symbol(const slang::Symbol *symbol,
                                       std::vector<std::string_view> &paths) {
    if (symbol->kind == slang::SymbolKind::Root) return nullptr;
    auto scope = symbol->getParentScope();
    const slang::Symbol *current;
    if (scope && symbol->kind == slang::SymbolKind::InstanceBody) {
        current = symbol->as<slang::InstanceBodySymbol>().parentInstance;
        scope = current->getParentScope();
        paths.emplace_back(current->name);
    }
    if (scope) {
        return &scope->asSymbol();
    } else {
        throw InvalidSyntaxException("Unable to determine parent symbol", symbol->location);
    }
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
    auto const *top_body =
        module_info_.current_module ? &module_info_.current_module->def()->body : parent;
    if (!parent || parent == top_body) {
        s << module_info_.get_identifier_name(sym.name);
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
        s << module_info_.get_identifier_name(sym.name);
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ConversionExpression &c) {
    auto const &target_type = *c.type;
    auto const target_bit_width = target_type.getBitWidth();
    auto const target_is_signed = target_type.isSigned();
    auto const &operand_type = *c.operand().type;

    if (operand_type.isSimpleType() && !target_type.isSimpleType()) {
        // convert operand to logic/bits
        if (target_type.isFourState()) {
            s << "logic::logic<";
        } else {
            s << "logic::bit<";
        }
        s << target_bit_width - 1 << ", 0, " << (target_is_signed ? "true" : "false") << ">(";
        c.operand().visit(*this);
        s << ")";
    } else if (!operand_type.isSimpleType() && target_type.isSimpleType() &&
               c.operand().kind != slang::ExpressionKind::IntegerLiteral) {
        // convert to num
        c.operand().visit(*this);
        s << ".to_num()";
    } else {
        s << "(";
        c.operand().visit(*this);
        s << ")";

        if (c.operand().type->getBitWidth() > target_bit_width) {
            // this is a slice
            s << ".slice<" << target_type.getFixedRange().left << ", "
              << target_type.getFixedRange().right << ">()";
        } else if (c.operand().type->getBitWidth() < target_bit_width) {
            // it's an extension
            auto size =
                std::abs(target_type.getFixedRange().left - target_type.getFixedRange().right) + 1;
            s << ".extend<" << size << ">()";
        }
        if (c.operand().type->isSigned() && !target_is_signed) {
            s << ".to_unsigned()";
        } else if (!c.operand().type->isSigned() && target_is_signed) {
            s << ".to_signed()";
        }
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::LValueReferenceExpression &expr) {
    if (!left_ptr)
        throw InvalidSyntaxException("Unable to determine LValue", expr.sourceRange.start());
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
                throw NotSupportedException(
                    fmt::format("Unsupported operator {0}", slang::toString(expr.op)),
                    expr.sourceRange.start());
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
        case slang::BinaryOperator::Divide:
            s << " / ";
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
            throw NotSupportedException(
                fmt::format("Unsupported operator {0}", slang::toString(expr.op)),
                expr.sourceRange.start());
    }
    right.visit(*this);
    if (closing_p) s << ")";
    s << ")";
}

void ExprCodeGenVisitor::handle(const slang::ConditionalExpression &expr) {
    expr.pred().visit(*this);
    s << "? (";
    expr.left().visit(*this);
    s << ") : (";
    expr.right().visit(*this);
    s << ")";
}

uint64_t get_constant_value(const slang::ConstantValue &constant,
                            const slang::SourceLocation &loc) {
    if (!constant.isInteger()) {
        throw NotSupportedException("Only integer selection is supported", loc);
    }
    return *constant.integer().as<uint64_t>();
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::ElementSelectExpression &sym) {
    auto const &value = sym.value();
    auto const &selector = sym.selector();

    auto const is_value_unpacked_array = (*value.type).isUnpackedArray();

    // depends on whether the selector is a constant or not
    std::optional<uint64_t> select_value;
    if (selector.constant) {
        auto const &v = *selector.constant;
        select_value = get_constant_value(v, selector.syntax->sourceRange().start());
    }
    value.visit(*this);
    if (select_value) {
        // if it's an array, we do array stuff
        if (is_value_unpacked_array) {
            s << '[' << *select_value << ']';
        } else {
            s << ".get<" << *select_value << ">()";
        }
    } else {
        if (is_value_unpacked_array) {
            s << '[';
            selector.visit(*this);
            s << ".to_num()";
            s << ']';
        } else {
            s << ".get(";
            selector.visit(*this);
            s << ")";
        }
    }
}

void ExprCodeGenVisitor::handle(const slang::RangeSelectExpression &expr) {
    if (!expr.left().constant)
        throw NotSupportedException("Only constant range select supported",
                                    expr.left().syntax->sourceRange().start());

    if (!expr.right().constant)
        throw NotSupportedException("Only constant range select supported",
                                    expr.right().syntax->sourceRange().start());

    s << "(";
    expr.value().visit(*this);
    s << ").slice<";
    s << get_constant_value(*expr.left().constant, expr.left().sourceRange.start());
    s << ", ";
    s << get_constant_value(*expr.right().constant, expr.right().sourceRange.start());
    s << ">()";
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
        auto func_name = fmt::format("fsim::runtime::{0}", name);
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
        // decide whether to insert location or not
        if (name == "finish" || name == "assert") {
            auto [filename, line] =
                get_loc(expr.sourceRange.start(), module_info_.get_compilation());
            if (!filename.empty()) {
                auto loc = fmt::format("{0}:{1}", filename, line);
                s << ", \"" << loc << "\"";
            }
        }
        s << ")";

        // special simulation control tasks
        if (name == "finish") {
            s << "; " << FSIM_END_PROCESS << "(" << module_info_.current_process_name()
              << "); return";
        }
    } else {
        const auto *function = std::get<0>(expr.subroutine);
        // DPI calls
        // for now we only support inputs
        s << module_info_.get_identifier_name(function->name) << "(";
        auto const &func_args = function->getArguments();
        auto const &call_args = expr.arguments();
        // for task, we need to pass in the current process;
        if (function->subroutineKind == slang::SubroutineKind::Task) {
            s << module_info_.current_process_name() << ", ";
            s << module_info_.scheduler_name();
            if (!func_args.empty()) s << ", ";
        }

        for (auto i = 0u; i < func_args.size(); i++) {
            auto const *func_arg = func_args[i];
            if (func_arg->direction != slang::ArgumentDirection::In) {
                throw NotSupportedException("Output direction in DPI not yet implemented",
                                            func_arg->location);
            }
            auto const *call_arg = call_args[i];
            // maybe need type conversion?
            // recursive code gen
            ExprCodeGenVisitor arg_expr(s, module_info_);
            call_arg->visit(arg_expr);
            if (i != (func_args.size() - 1)) s << ", ";
        }
        s << ")";
        return;
    }
}

[[maybe_unused]] void ExprCodeGenVisitor::handle(const slang::AssignmentExpression &expr) {
    auto const *timing = expr.timingControl;
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
            // need to take care of timing
            if (timing) {
                output_timing(*timing);
            }

            s << module_info_.current_process_name() << "->schedule_nba([this, wire]() { ";
            s << "wire.unpack(";
            auto const &concat = left.as<slang::ConcatenationExpression>();
            output_concat(concat);
            s << "); }); }";
        } else {
            std::string right_name;
            if (timing) {
                right_name = "wire";
                s << "{ auto " << right_name << " = ";
                right.visit(*this);
                s << ";";
                output_timing(*timing);
            }
            s << fsim_schedule_nba << "(";
            left.visit(*this);
            // put extra paraphrases to escape , in macro.
            // typically, what happens is slice<a, b> is treated as two arguments
            // since we're no declaring types here, it should be fine
            s << ", (";
            if (timing) {
                s << right_name;
            } else {
                right.visit(*this);
            }
            s << "), " << module_info_.current_process_name() << ")";
        }
    } else {
        auto const &left = expr.left();
        auto const &right = expr.right();
        std::string right_name;
        if (timing) {
            right_name = "wire";
            s << "{ auto " << right_name << " = ";
            right.visit(*this);
            s << ";";
            output_timing(*timing);
            s << " ";
        }
        // detect unpacking syntax
        if (left.kind == slang::ExpressionKind::Concatenation) {
            // this is unpack

            s << "(";
            if (timing) {
                s << right_name;
            } else {
                right.visit(*this);
            }
            s << ").unpack(";
            auto const &concat = left.as<slang::ConcatenationExpression>();
            output_concat(concat);
            s << ")";
        } else {
            // if it's an old fashion function return
            if (is_return_symbol(left)) [[unlikely]] {
                s << "return ";
            } else {
                left.visit(*this);
                left_ptr = &left;
                s << " = ";
            }

            if (timing) {
                s << right_name;
            } else {
                right.visit(*this);
            }
        }
        if (timing) s << "; }";
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

void ExprCodeGenVisitor::output_timing(const slang::TimingControl &timing) {
    TimingControlCodeGen t(s, module_info_, *this);
    t.handle(timing);
    s << " ";
}

bool ExprCodeGenVisitor::is_return_symbol(const slang::Expression &expr) const {
    if (!module_info_.current_function) return false;
    if (expr.kind != slang::ExpressionKind::NamedValue) return false;
    auto const &named = expr.as<slang::NamedValueExpression>();
    return named.symbol.name == module_info_.current_function->name;
}

TimingControlCodeGen::TimingControlCodeGen(std::ostream &s, CodeGenModuleInformation &module_info,
                                           ExprCodeGenVisitor &expr_v)
    : s(s), module_info_(module_info), expr_v(expr_v) {}

void TimingControlCodeGen::handle(const slang::TimingControl &timing) {
    switch (timing.kind) {
        case slang::TimingControlKind::Delay: {
            // we first release the current condition holds
            auto const &delay = timing.as<slang::DelayControl>();

            s << fmt::format("{0}({1}, (", fsim_schedule_delay,
                             module_info_.current_process_name());
            delay.expr.visit(expr_v);
            s << fmt::format(").to_uint64(), {0}, {1});", module_info_.scheduler_name(),
                             module_info_.get_new_name(fsim_next_time, false));
            break;
        }
        case slang::TimingControlKind::SignalEvent: {
            auto const &single_event = timing.as<slang::SignalEventControl>();
            s << fmt::format("SCHEDULE_EDGE({0}, ", module_info_.current_process_name());
            single_event.expr.visit(expr_v);
            s << ", fsim::runtime::Process::EdgeControlType::";
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
            break;
        }
        case slang::TimingControlKind::ImplicitEvent:
            // noop
            break;
        default: {
            throw NotSupportedException(
                fmt::format("Unsupported timing control {0}", slang::toString(timing.kind)),
                timing.syntax->sourceRange().start());
        }
    }
}

}  // namespace fsim