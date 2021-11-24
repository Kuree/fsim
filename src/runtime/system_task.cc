#include "system_task.hh"

#include "module.hh"

namespace xsim::runtime {

std::string preprocess_display_fmt(const Module *module, std::string_view format) {
    // based on LRM 21.2
    std::string result;
    char pre[2] = {'\0', '\0'};
    uint64_t i = 0;
    while (i < format.size()) {
        auto c = format[i];
        // we only need to modify it make it consistent with the fmt lib string
        // also need to take into account of other weird things
        // for now we transform %m
        if (pre[0] == '%' && c == 'm') {
            // pop out the last %
            result.pop_back();
            // use the instance name
            result.append(module->hierarchy_name());
            c = '\0';
        }
        pre[0] = c;
        result.append(pre);
        i++;
    }

    return result;
}

std::pair<std::string_view, uint64_t> preprocess_display_fmt(std::string_view format) {
    auto total_size = format.size();
    while (true) {
        auto pos = format.find_first_of('%');
        if (pos == std::string::npos) {
            // no % symbol
            return std::make_pair("", total_size);
        } else if (pos < (format.size() - 1) && format[pos + 1] == '%') {
            // false positive, move to the next one
            format = format.substr(2);
            continue;
        }
        // find string format
        format = format.substr(pos + 1);
        auto end = format.find_first_not_of("0123456789");
        if (end == std::string::npos) {
            // this is an error!
            // we return the entire thing!
            return std::make_pair("", total_size);
        } else {
            auto fmt = format.substr(0, +1);
            return std::make_pair(fmt, total_size - end);
        }
    }
}

void display(const Module *module, std::string_view format) {
    // only display the format for now
    auto fmt = preprocess_display_fmt(module, format);
    std::cout << fmt << std::endl;
}

}  // namespace xsim::runtime