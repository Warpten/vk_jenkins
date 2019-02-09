#include "pattern.hpp"
#include "string_view_range.hpp"

#include <iostream>

auto find_delimiter(std::string_view const& view, char delimiter, size_t ofs = std::string::npos) -> size_t {

    size_t nextDelimiter = ofs;
    do
    {
        nextDelimiter = view.find(delimiter, nextDelimiter == std::string::npos ? 0 : nextDelimiter);
        if (nextDelimiter != std::string::npos)
        {
            if (nextDelimiter == 0 || view[nextDelimiter - 1] != '\\')
                break;
        }
    } while (nextDelimiter != std::string::npos);

    return nextDelimiter;
};

// non-varying characters /////////////////////////////////

std::string_view raw_range_t::parse(std::string_view view)
{
    size_t delim = find_delimiter(view, '(');
    if (delim == std::string::npos)
        delim = find_delimiter(view, '[');

    if (delim == 0)
        return view;

    val.push_back(std::string(view.substr(std::min(0u, delim), std::min(view.size(), delim))));
    return view.substr(val[0].size());
}

uint32_t raw_range_t::apply(std::vector<char>& storage, uint32_t offset) {
    storage.resize(storage.size() + val[0].size());
    memcpy(storage.data() + offset, val[0].data(), val[0].size());
    return offset + val[0].size();
}

// parse size decoration {x, y} or {x} /////////////////////

std::string_view size_specified_range_t::parse(std::string_view view)
{
    size_t delim = find_delimiter(view, '{');
    if (delim == std::string::npos)
    {
        max_count = min_count = 1;
        return view;
    }

    size_t end_delim = find_delimiter(view, '}', delim);

    std::string_view work_view = view.substr(delim + 1, end_delim - delim - 1);

    size_t comma = find_delimiter(work_view, ',');
    if (comma == std::string::npos)
    {
        uint32_t val = std::stoi(std::string(work_view));
        min_count = max_count = val;
    }
    else
    {
        min_count = std::stoi(std::string(work_view.substr(0, comma)));
        max_count = std::stoi(std::string(work_view.substr(comma + 1, work_view.size() - 1)));
    }

    return view.substr(end_delim + 1);
}

// parse an array of values (a|b|c|d) ////////////////////////

std::string_view array_range_t::parse(std::string_view view) {
    size_t delim = find_delimiter(view, '(');
    if (delim == std::string::npos)
        return view;

    size_t end_delim = find_delimiter(view, ')', delim);

    std::string_view work_view = view.substr(delim + 1, end_delim - delim - 1);

    size_t startOffset = 0;
    size_t splitterOfs = 0;
    for (;;) {
        splitterOfs = find_delimiter(work_view, '|', splitterOfs);
        if (splitterOfs != std::string::npos) {
            vals.emplace_back(work_view.substr(startOffset, splitterOfs));
            startOffset = splitterOfs + 1;
        }
        else {
            vals.emplace_back(work_view.substr(startOffset));
            break;
        }
    }

    reset();

    return size_specified_range_t::parse(view.substr(end_delim + 1));
}

void array_range_t::reset() {
    itr = vals.begin();
}

bool array_range_t::has_next() {
    return itr != vals.end();
}

uint32_t array_range_t::apply(std::vector<char>& storage, uint32_t offset) {
    storage.resize(storage.size() + itr->size());
    memcpy(storage.data() + offset, itr->data(), itr->size());
    return offset + itr->size();
}

void array_range_t::move_next() {
    ++itr;
}

// parse a range of values [a-z|0-1|alpha|alnum|hex|path] ////////////////////

std::string_view varying_range_t::parse(std::string_view view) {
    size_t delim = find_delimiter(view, '[');

    if (delim == std::string::npos)
        return view;

    size_t end_delim = find_delimiter(view, ']', delim);

    auto range_handler = [alphabet = &this->universe](std::string_view const& r) -> void {
        if (r == "hex") {
            constexpr const char hex_alphabet[] = "ABCDEF0123456789";
            alphabet->insert(hex_alphabet, hex_alphabet + sizeof(hex_alphabet) - 1);
        }
        else if (r == "alpha") {
            constexpr const char alpha_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
            alphabet->insert(alpha_alphabet, alpha_alphabet + sizeof(alpha_alphabet) - 1);
        }
        else if (r == "num") {
            constexpr const char num_alphabet[] = "0123456789";
            alphabet->insert(num_alphabet, num_alphabet + sizeof(num_alphabet) - 1);
        }
        else if (r == "alnum" || r == "alphanum") {
            constexpr const char num_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
            alphabet->insert(num_alphabet, num_alphabet + sizeof(num_alphabet) - 1);
        }
        else if (r == "path") {
            constexpr const char path_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. \\";
            alphabet->insert(path_alphabet, path_alphabet + sizeof(path_alphabet) - 1);
        }
        else {
            size_t splitPos = r.find('-');
            if (splitPos == std::string::npos) {
                throw std::runtime_error("invalid range");
            }
            else {
                char startCharacter = std::toupper(r[splitPos - 1]);
                char endCharacter = std::toupper(r[splitPos + 1]);

                for (; startCharacter <= endCharacter; ++startCharacter)
                    alphabet->insert(startCharacter);
            }
        }
    };

    std::string_view work_view = view.substr(delim + 1, end_delim - delim - 1);
    size_t startOffset = 0;
    size_t splitterOfs = 0;
    for (;;) {
        splitterOfs = find_delimiter(work_view, '|', splitterOfs);
        if (splitterOfs != std::string::npos) {
            range_handler(work_view.substr(startOffset, splitterOfs - startOffset));
        }
        else {
            range_handler(work_view.substr(startOffset));
            break;
        }
    }

    std::string_view retval = size_specified_range_t::parse(view.substr(end_delim + 1));

    generate_perms();

    return retval;
}

uint32_t varying_range_t::apply(std::vector<char>& storage, uint32_t offset) {
    storage.resize(storage.size() + itr->size());
    memcpy(storage.data() + offset, itr->data(), itr->size());
    return offset + itr->size();
}

void varying_range_t::reset() {
    itr = vals.begin();
}

bool varying_range_t::has_next() {
    return itr != vals.end();
}

void varying_range_t::move_next() {
    ++itr;
}

void varying_range_t::generate_perms() {
    vals.clear();
    vals = std::move(generate_perms(min_count, max_count));
    reset();
}

std::vector<std::string> varying_range_t::generate_perms(uint32_t minCount, uint32_t maxCount) {
    std::vector<std::string> results;

    if (minCount <= 1)
        for (char c : universe)
            results.emplace_back(1, c);

    if (maxCount == 1)
        return results;

    auto rec = generate_perms(std::min(0u, minCount - 1), maxCount - 1);

    for (auto c : universe)
        for (auto subset : rec)
            results.push_back(c + subset);

    return results;
}

template <typename T, typename... Ts>
struct chain_tester {
    using tester_t = T;
    using fallback_t = chain_tester<Ts...>;

    static bool test(std::string_view& view, node_t*& node) {
        node = new T();
        std::string_view next = node->parse(view);
        if (next.data() == view.data()) {
            delete node;
            return fallback_t::test(view, node);
        }

        view = next;
        return true;
    }
};

template <typename T>
struct chain_tester<T> {
    using tester_t = T;

    static bool test(std::string_view& view, node_t*& node) {
        node = new T();
        std::string_view next = node->parse(view);
        if (next.data() != view.data()) {
            view = next;

            return true;
        }

        delete node;
        return false;
    }
};

std::vector<string_view_range<char>> fold(std::vector<string_view_range<char>>& left, std::vector<std::string> const& right)
{
    std::vector<string_view_range<char>> bucket(left.size() * right.size());
    uint32_t i = 0;
    for (string_view_range<char> const& l : left) {
        for (std::string const& r : right) {
            for (auto&& n : l.elems)
                bucket[i].push_back(n);

            bucket[i].push_back(std::string_view(r.data(), r.size()));

            ++i;
        }
    }

    left.clear();

    return bucket;
}

pattern_t::pattern_t(std::string_view regex)
{
    using full_tester_t = chain_tester<raw_range_t, array_range_t, varying_range_t>;

    node_t* node = nullptr;
    while (regex.size() > 0 && full_tester_t::test(regex, node))
    {
        if (head == nullptr)
            head = node;

        if (tail != nullptr)
            tail->next = node;

        tail = node;
    }

    if (regex.size() > 0)
        throw std::runtime_error("Failed to parse pattern");

    std::vector<string_view_range<>> all_values;
    all_values.reserve(count());

    node_t* h = head;
    uint32_t idx = 0;
    vals.resize(h->values().size());
    for (std::string const& i : h->values())
        vals[idx++].push_back(std::string_view(i.data(), i.size()));

    h = h->next;
    while (h != nullptr) {
        vals = fold(vals, h->values());
        h = h->next;
    }

    itr = vals.begin();
}

uint32_t pattern_t::count()
{
    uint32_t count = 1;
    node_t* h = head;
    while (h != nullptr) {
        count *= h->count();
        h = h->next;
    }

    return count;
}

void pattern_t::collect(std::vector<uploaded_string>& bucket)
{
    uint32_t actualSize = 0;

    // Reclaim all the space
    bucket.resize(bucket.capacity());
    for (uploaded_string& str : bucket)
    {
        if (itr == vals.end())
            break;

        str.char_count = itr->full_length;
        str.hash = 0;

        memset(str.words, 0, sizeof(str.words));

        size_t ofs = 0;
        for (uint32_t i = 0; i < itr->elems.size(); ++i) {
            memcpy(reinterpret_cast<char*>(str.words) + ofs, itr->elems[i].data(), itr->elems[i].size());
            ofs += itr->elems[i].size();
        }

        ++actualSize;
        ++itr;
    }

    // Resize down so we don't advertise as having more data than we really have
    bucket.resize(actualSize);
}
