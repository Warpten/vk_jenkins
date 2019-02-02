#pragma once

#include "gpu_string.hpp"

#include <vector>

struct input_file
{
public:
    input_file(const char* fpath);

    gpu_string const& operator[] (size_t index) const {
        return values[index];
    }

    size_t const size() const { return values.size(); }

private:
    std::vector<gpu_string> values;
};