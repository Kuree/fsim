#include "system_task.hh"

#include <fstream>

#include "module.hh"

namespace fsim::runtime {

cout_lock::cout_lock() { Module::cout_lock(); }

cout_lock::~cout_lock() { Module::cout_unlock(); }

void print_assert_error(std::string_view name, std::string_view loc) {
    if (!loc.empty()) {
        std::cout << '(' << loc << ") ";
    }
    std::cout << "Assertion failed: " << name << std::endl;
}

std::string preprocess_display_fmt(const Module *module, std::string_view format) {
    // based on LRM 21.2
    std::string result;
    char pre[2] = {'\0', '\0'};
    uint64_t i = 0;
    while (i < format.size()) {
        auto c = format[i];
        // need to take into account of other weird things
        // for now we transform %m
        if (pre[0] == '%' && c == 'm') {
            // pop out the last %
            result.pop_back();
            // use the instance name
            result.append(module->hierarchy_name());
            c = '\0';
        } else if (pre[0] == '%' && c == 't') {
            // we change it to %d
            result.append("d");
            c = '\0';
        }
        pre[0] = c;
        result.append(pre);
        i++;
    }

    return result;
}

std::pair<std::string_view, uint64_t> preprocess_display_fmt(std::string_view format,
                                                             std::stringstream &ss) {
    auto total_size = format.size();
    while (true) {
        auto pos = format.find_first_of('%');
        if (pos == std::string::npos) {
            // no % symbol
            ss << format;
            return std::make_pair("", total_size);
        } else if (pos < (format.size() - 1) && format[pos + 1] == '%') {
            // false positive, move to the next one
            format = format.substr(2);
            // output the one we just consumed
            ss << "%%";
            continue;
        }
        // consume the non-format string
        ss << format.substr(0, pos);
        // find string format
        format = format.substr(pos + 1);
        auto end = format.find_first_not_of("0123456789");
        if (end == std::string::npos) {
            // this is an error!
            // we return the entire thing!
            return std::make_pair("", total_size);
        } else {
            auto fmt = format.substr(0, end + 1);
            return std::make_pair(fmt, total_size - format.size() + end + 1);
        }
    }
}

void display(const Module *module, std::string_view format) {
    cout_lock lock;
    auto str = preprocess_display_fmt(module, format);
    std::cout << str << std::endl;
}

void write(const Module *module, std::string_view format) {
    cout_lock lock;
    auto str = preprocess_display_fmt(module, format);
    std::cout << str;
}

struct OpenFile {
    explicit OpenFile(std::unique_ptr<std::fstream> &&stream) : stream(std::move(stream)) {}
    std::unique_ptr<std::fstream> stream;
    std::mutex lock;
};

static std::mutex fd_lock;
static std::unordered_map<int, std::unique_ptr<OpenFile>> opened_files;
static uint32_t fd_count = 0;

int32_t fopen(std::string_view filename, std::string_view mode_str) {
    auto stream = std::make_unique<std::fstream>();
    std::ios_base::openmode mode = {};
    if (mode_str.find('r') != std::string::npos) {
        mode |= std::ios_base::in;
    }
    if (mode_str.find('w') != std::string::npos) {
        mode |= std::ios_base::out;
    }
    if (mode_str.find('a') != std::string::npos) {
        mode |= std::ios_base::app;
    }
    if (mode_str.find('+') != std::string::npos && mode_str.find('w') != std::string::npos) {
        mode |= std::ios_base::trunc;
    }

    stream->open(filename.data(), mode);

    if (stream->bad()) {
        return -1;
    }

    std::lock_guard guard(fd_lock);
    auto u_fd = fd_count++;
    u_fd |= (1u << 31);
    auto fd = static_cast<int32_t>(u_fd);
    auto opened = std::make_unique<OpenFile>(std::move(stream));
    opened_files.emplace(fd, std::move(opened));
    return fd;
}

void fclose(int32_t fd) {
    std::lock_guard guard(fd_lock);
    if (opened_files.find(fd) == opened_files.end()) {
        return;
    }
    auto &stream = opened_files.at(fd);
    std::lock_guard guard_stream(stream->lock);
    stream->stream->flush();
    stream->stream->close();
    opened_files.erase(fd);
}

void fwrite_(int fd, std::string_view str, bool new_line) {
    if (fd == 1) {
        std::cout << str;
    } else if (fd == 2) {
        std::cerr << str;
    } else {
        // per LRM 21.3.1
        // custom file opened has the highest bit set
        auto u_fd = static_cast<uint32_t>(fd);
        if (!(u_fd & (1u << 31))) {
            // ignore it so far, maybe use location future
            return;
        }
        if (opened_files.find(fd) == opened_files.end()) {
            return;
        }
        auto &f = opened_files.at(fd);
        std::lock_guard guard(f->lock);
        (*f->stream) << str;
        if (new_line) (*f->stream) << std::endl;
    }
}

void fdisplay_(int32_t fd, std::string_view str) { fwrite_(fd, str, true); }

void fdisplay(const Module *module, int fd, std::string_view format) {
    auto str = preprocess_display_fmt(module, format);
    fwrite_(fd, str, true);
}

void fwrite(const Module *module, int fd, std::string_view format) {
    auto str = preprocess_display_fmt(module, format);
    fwrite_(fd, str, false);
}

}  // namespace fsim::runtime