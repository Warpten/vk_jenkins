#pragma once

#include "string_view_range.hpp"
#include "uploaded_string.hpp"
#include "rolling_iterator.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>
#include <set>
#include <string_view>
#include <cctype>
#include <functional>

struct node_t {
    virtual ~node_t() { }

    virtual size_t apply(char* storage, size_t offset) = 0;
    virtual std::string_view parse(std::string_view view) = 0;

    virtual void reset() = 0;
    virtual bool has_next() = 0;
    virtual void move_next() = 0;

    virtual uint64_t count() = 0;

    node_t* next = nullptr;
    node_t* prev = nullptr;

    bool locked() const {
        return l;
    }
    void lock() {
        l = true;
    }
    void unlock() {
        l = false;
    }

    size_t end_offset() const { return (prev == nullptr ? 0 : prev->end_offset()) + length(); }
    size_t start_offset() const { return prev == nullptr ? 0 : prev->end_offset(); }

    virtual std::string_view current() const = 0;

protected:
    virtual size_t length() const = 0;

    bool l = false;
};

// raw characters
struct raw_range_t final : public node_t {
    virtual ~raw_range_t() { }

private:
    std::vector<std::string> val;

public:
    size_t apply(char* storage, size_t offset) override;
    std::string_view parse(std::string_view view) override;

    void reset() override { }
    bool has_next() override { return false; }
    void move_next() override { }

	uint64_t count() override { return 1; }

    std::string_view current() const override { return std::string_view(val[0].data(), val[0].size()); }

    size_t length() const override { return val[0].size(); }
};

// size modifier {x, y} {x}
struct size_specified_range_t : public node_t {
    virtual ~size_specified_range_t() { }

protected:
    size_t min_count;
	size_t max_count;

public:
    std::string_view parse(std::string_view view) override;
};

// array (x|y|z)
struct array_range_t final : public size_specified_range_t {
    virtual ~array_range_t() { }

private:
    std::vector<std::string> vals;
    decltype(vals)::const_iterator itr;

public:
    size_t apply(char* storage, size_t offset) override;
    std::string_view parse(std::string_view view) override;

    void reset() override;
    bool has_next() override;
    void move_next() override;

	uint64_t count() override { return vals.size(); }

    std::string_view current() const override { return std::string_view(itr->data(), itr->size()); }
    size_t length() const override { return itr->size(); }
};

// ranges [a-z|alpha|alnum|num|hex|path]
struct varying_range_t : public size_specified_range_t {
    virtual ~varying_range_t() { }

private:
    std::set<char> universe;

    rolling_iterator<decltype(universe)::const_iterator> itr;

public:
    size_t apply(char* storage, size_t offset) override;
    std::string_view parse(std::string_view view) override;
    void reset() override;
    bool has_next() override;
    void move_next() override;

	uint64_t count() override;

    std::string_view current() const override { return std::string_view(itr.current(), itr.size()); }
    size_t length() const override { return itr.size(); }
};

struct pattern_t {
private:
    node_t* head = nullptr;
    node_t* tail = nullptr;

    rolling_iterator<std::vector<std::vector<char>>::const_iterator> ritr;
	uint64_t idx = 0;

public:
    pattern_t(std::string_view regex);

    void load(std::string_view regex);

    pattern_t() {
        head = nullptr;
        tail = nullptr;

    }

    pattern_t(pattern_t&& o) {
        head = o.head;
        tail = o.tail;

    }

    pattern_t(pattern_t const& o) {
        head = o.head;
        tail = o.tail;

    }

    pattern_t& operator = (pattern_t&& o) {
        reset();

        head = o.head;
        tail = o.tail;

        return *this;
    }

    ~pattern_t() {
        reset();
        tail = nullptr;
    }

    void reset()
    {
        //TODO: blowing up the stack here would be fun; wouldn't it?
        std::function<void(node_t*)> last_deleter;
        last_deleter = [&](node_t* node) -> void {
            if (node->next != nullptr) {
                last_deleter(node->next);
                node->next = nullptr;
            }
            delete node;
        };

        if (head == nullptr)
            return;

        last_deleter(head);
        head = tail = nullptr;
    }

	uint64_t count() const;

    bool has_next() const;

    bool write(uploaded_string& output);
};

