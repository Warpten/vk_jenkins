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

            nextDelimiter += 1;
        }
        else
            break;
    } while (true);

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

    std::string s(view.substr(std::min(size_t(0), delim), std::min(view.size(), delim)));
    // Remove escape sequences
    s.erase(std::remove(s.begin(), s.end(), '\\'), s.end());

    for (char& c : s)
    {
        if (c == '/')
            c = '\\';
        else
            c = std::toupper(c);
    }

    val.push_back(s);
    return view.substr(s.size());
}

size_t raw_range_t::apply(char* storage, size_t offset) {
    memcpy(storage + offset, val[0].data(), val[0].size());
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
        splitterOfs = find_delimiter(work_view, '|', splitterOfs + 1);
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

size_t array_range_t::apply(char* storage, size_t offset) {
    memcpy(storage + offset, itr->data(), itr->size());
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
        splitterOfs = find_delimiter(work_view, '|', splitterOfs + 1);
        if (splitterOfs != std::string::npos) {
            range_handler(work_view.substr(startOffset, splitterOfs - startOffset));
        }
        else {
            range_handler(work_view.substr(startOffset));
            break;
        }
    }

    std::string_view retval = size_specified_range_t::parse(view.substr(end_delim + 1));


    itr = rolling_iterator<decltype(universe)::const_iterator>(universe.begin(), universe.end());
    itr.expand(min_count);

    return retval;
}


size_t varying_range_t::count() {
    if (min_count == max_count) {
        return size_t(std::pow(universe.size(), min_count));
    }

    size_t u = universe.size();

    if (min_count == 1) {
        return size_t((std::pow(u, max_count) - 1) * u) / (u - 1);
    }

    return size_t(std::pow(u, max_count - 1) - std::pow(u, min_count)) / (u - 1);
}

size_t varying_range_t::apply(char* storage, size_t offset) {
    size_t size = itr.size();

    memcpy(storage + offset, itr.current(), size);

    return offset + size;
}

void varying_range_t::reset() {
    itr.shrink_to(min_count);
}

bool varying_range_t::has_next() {
    // if done on the gen we have work if not last gen
    if (itr.all_done())
        return itr.size() < max_count;

    // otherwise more work if gen not dont
    return !itr.all_done();
}

void varying_range_t::move_next() {
    itr.move_next();
    // if done on this level but there's more, expand
    if (itr.all_done())
        if (itr.size() < max_count)
            itr.expand();
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

pattern_t::pattern_t(std::string_view regex)
{
    load(regex);
}

void pattern_t::load(std::string_view regex)
{
    reset();

    using full_tester_t = chain_tester<raw_range_t, array_range_t, varying_range_t>;

    node_t* node = nullptr;
    while (regex.size() > 0 && full_tester_t::test(regex, node))
    {
        if (head == nullptr)
            head = node;

        if (tail != nullptr)
        {
            node->prev = tail;
            tail->next = node;
        }

        // lock all nodes...
        node->lock();

        tail = node;
    }

    // ... except the last one
    tail->unlock();

    if (regex.size() > 0)
        throw std::runtime_error("Failed to parse pattern");

    idx = count();
}

size_t pattern_t::count() const
{
    size_t count = 1;
    node_t* h = head;
    while (h != nullptr) {
        count *= h->count();
        h = h->next;
    }

    return count;
}

bool pattern_t::has_next() const
{
    return idx > 0;
}

bool pattern_t::write(uploaded_string& output) {
    if (!has_next())
        return false;

    output.reset();

    // TODO: Figure out a way to not have to iterate twice

    node_t* h = head;
    size_t offset = 0;

    h = tail;
    while (h != nullptr) {
        offset = std::max(offset, h->apply(reinterpret_cast<char*>(output.words), h->start_offset()));

        if (!h->locked())
            h->move_next();

        node_t* prev = h->prev;

        if (!h->has_next()) {

            if (prev != nullptr) {
                prev->unlock();
                h->lock();

                if (prev->has_next())
                    h->reset();
            }
        }
        else {
            if (h->next != nullptr && !h->locked()) {
                h->lock();
                tail->unlock();
            }
        }

        h = prev;
    }

    output.char_count = int32_t(offset);
    --idx;
    return true;
}
