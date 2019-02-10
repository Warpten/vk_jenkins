#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iostream>

#include "pattern.hpp"

struct input_file
{
public:
    input_file(const char* fpath);

    bool next(uploaded_string& output) {
        while (!current.has_next()) {
            std::string line;
            if (!std::getline(fs, line))
                return false;

            current.load(line);
// #if _DEBUG
            std::cout << "Loaded pattern '" << line << "' (" << current.count() << " possible values).\n";
// #endif
        }
        return current.write(output);
    }

    bool hasNext() {
        return current.has_next() || !fs.eof();
    }

private:
    std::fstream fs;
    pattern_t current;
};