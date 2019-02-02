#pragma once

#include <memory>
#include <type_traits>
#include <string>

class gpu_allocator_string : public std::allocator<char>
{

};

// Use an allocator aligned to the integer boundary for strings uploaded to the GPU as a sequence of integers.
class gpu_string : public std::basic_string<char, std::char_traits<char>, gpu_allocator_string>
{
    using base_t = std::basic_string<char, std::char_traits<char>, gpu_allocator_string>;

public:
    gpu_string() : base_t() { }
    gpu_string(const char* const str) : base_t(str) { }

    template <typename Alloc = std::allocator<int>>
    gpu_string(std::basic_string<char, std::char_traits<char>, Alloc> const& other) : base_t(other) { }

    template <typename Alloc = std::allocator<int>>
    gpu_string(std::basic_string<char, std::char_traits<char>, Alloc>&& other) : base_t(std::move(other)) { }

    gpu_string(gpu_string const& other) : base_t(other) { }
    gpu_string(gpu_string&& other) : base_t(std::move(other)) { }

    // Return the size of the string aligned to the allocator boundary
    inline size_type aligned_size() const noexcept {
        return (size() + sizeof(typename allocator_type::value_type) - 1) & ~(sizeof(typename allocator_type::value_type) - 1);
    }

    inline size_type size() const noexcept {
        return static_cast<const base_t*>(this)->size();
    }

    inline const char* data() const noexcept {
        return static_cast<const base_t*>(this)->data();
    }

    inline const uint32_t* integer_data() const noexcept {
        return reinterpret_cast<uint32_t const*>(data());
    }

    char operator[] (size_t i) const {
        return data()[i];
    }
};
