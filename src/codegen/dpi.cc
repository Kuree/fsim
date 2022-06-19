#include "dpi.hh"

#include "slang/binding/CallExpression.h"

namespace fsim {
std::unordered_map<std::string_view, const slang::CallExpression *> get_all_dpi_calls(
    const Module *module);

std::string_view get_dpi_type(const slang::Type &type) {
    if (type.isVoid()) return "void";
    auto width = type.getBitWidth();
    if (type.isSigned()) {
        if (width <= 8) {
            return "int8_t";
        } else if (width <= 16) {
            return "int16_t";
        } else if (width <= 32) {
            return "int32_t";
        } else {
            return "int64_t";
        }
    } else {
        if (width <= 8) {
            return "uint8_t";
        } else if (width <= 16) {
            return "uint16_t";
        } else if (width <= 32) {
            return "uint32_t";
        } else {
            return "uint64_t";
        }
    }
}

void codegen_dpi_header(const Module *mod, std::ostream &s) {
    // for now, we dump all dpi calls into every module implementation file, and then let the
    // linker figure out what to link. this, of course, can be improved later on to conditionally
    // generate the
    auto calls = get_all_dpi_calls(mod);
    if (calls.empty()) return;

    s << "extern \"C\" {" << std::endl;

    for (auto const &[name, func_call] : calls) {
        auto sub = func_call->subroutine;
        if (sub.index() != 0) continue;
        auto const *dpi = std::get<0>(sub);
        auto return_type = get_dpi_type(dpi->getReturnType());

        s << return_type << " " << name << "(";
        auto const &args = dpi->getArguments();
        for (auto i = 0u; i < args.size(); i++) {
            auto const &arg = args[i];
            auto arg_type = get_dpi_type(arg->getType());
            auto ref = arg->direction != slang::ArgumentDirection::In;
            s << arg_type;
            if (ref) s << '*';
            s << ' ' << arg->name;
            if (i != (args.size() - 1)) s << ", ";
        }
        s << ");" << std::endl;
    }

    s << "}" << std::endl;
}

}  // namespace fsim
