#include "input_file.hpp"

input_file::input_file(const char* fpath) : fs(fpath), current() {
    if (!fs.is_open())
        return;
}
