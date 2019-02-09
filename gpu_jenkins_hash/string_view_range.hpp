#pragma once

#include <vector>
#include <string_view>
#include <algorithm>
#include <numeric>

template <typename CharT = char>
struct string_view_range
{
    std::vector<std::basic_string_view<CharT>> elems;
    size_t full_length = 0;

    void push_back(std::basic_string_view<CharT> elem) {
        full_length += elem.size();
        elems.push_back(elem);
    }

    void push_back(std::string const& str) {
        full_length += str.size();
        elems.push_back(std::string_view(str.data(), str.size()));
    }

    void pop() {
        full_length -= elems.rbegin()->size();
        elems.erase(elems.rbegin());
    }

    size_t size() const {
        return full_length;
    }

    std::string as_string() const
    {
        std::string str;
        str.reserve(full_length);

        for (auto&& itr : elems)
            str.append(itr);

        return str;
    }
};
