#pragma once

#include <cstdint>
#include <string>
#include <algorithm>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

#include "utils.hpp"
#include "lookup3.hpp"

#pragma pack(push, 1)
struct pattern_t;

struct uploaded_string {
private:
    friend struct pattern_t;

    int32_t char_count;
    uint32_t hash;

    uint32_t words[32 * 3];

public:
    uint32_t get_hash() const {
        return hash;
    }

    uint32_t get_cpu_hash() const {
        return hashlittle((const void*)words, char_count, 0);
    }

    std::string_view value() const {
        return std::string_view(reinterpret_cast<const char*>(words), static_cast<size_t>(char_count)); //-V206
    }

    uploaded_string() {
        memset(words, 0, sizeof(words));
        char_count = 0;
        hash = 0;
    }

    uploaded_string& operator = (std::string const& sv) {
        hash = 0;
        char_count = int32_t(sv.size());
        memset(words, 0, sizeof(words));
        memcpy(words, sv.data(), sv.size());
        return *this;
    }

    void reset()
    {
        memset(words, 0, sizeof(words));
        char_count = 0;
    }

    void append(std::string_view const& sv) {
        memcpy(reinterpret_cast<char*>(words) + char_count, sv.data(), sv.size());
        char_count += int32_t(sv.size());
    }
};
#pragma pack(pop)
