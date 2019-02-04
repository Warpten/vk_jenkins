#pragma once

#include <string>
#include <vector>
#include <fstream>

struct input_file
{
public:
    input_file(const char* fpath);

    std::string next() {
        std::string tmp = nextLine;
        std::getline(fs, nextLine);
        return tmp;
    }

    bool hasNext() {
        return nextLine.size() > 0 || !fs.eof();
    }

private:
    std::fstream fs;
    std::string nextLine;
};