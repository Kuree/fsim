#ifndef XSIM_MACRO_HH
#define XSIM_MACRO_HH

// make the codegen more readable
#define XSIM_CHECK_CHANGED(module, var, res) \
    do {                                     \
        res = res || module->var.changed;    \
        module->var.changed = false;         \
    } while (0)

#endif  // XSIM_MACRO_HH
