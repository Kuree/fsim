#ifndef XSIM_CODEGEN_DPI_HH
#define XSIM_CODEGEN_DPI_HH

#include "cxx.hh"
#include "util.hh"

namespace xsim {
void codegen_dpi_header(const Module *mod, std::ostream &s, int &indent_level);
}

#endif  // XSIM_CODEGEN_DPI_HH
