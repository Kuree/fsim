#ifndef XSIM_MACRO_HH
#define XSIM_MACRO_HH

// make the codegen more readable
#define XSIM_CHECK_CHANGED(m, var, res, T)       \
    do {                                         \
        auto *module = reinterpret_cast<T *>(m); \
        res = res || module->var.changed;        \
        module->var.changed = false;             \
    } while (0)

#endif  // XSIM_MACRO_HH
