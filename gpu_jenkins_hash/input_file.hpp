#pragma once

#include <string>
#include <vector>

struct input_file
{
public:
    input_file(const char* fpath);

    std::string const& operator[] (size_t index) const {
        return values[index];
    }

    size_t const size() const { return values.size(); }

private:
    std::vector<std::string> values;
};