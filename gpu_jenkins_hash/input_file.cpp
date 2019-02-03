#include "input_file.hpp"

#include <fstream>

input_file::input_file(const char* fpath) {
    std::ifstream fs(fpath);
    if (!fs.is_open())
        return;

    std::string line;
    while (std::getline(fs, line))
        values.emplace_back(line);
}