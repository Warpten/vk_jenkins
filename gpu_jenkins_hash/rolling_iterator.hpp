#pragma once

#include <vector>
#include <memory>

template <typename Iterator, typename ValueType = typename Iterator::value_type>
class rolling_iterator
{
public:
    using value_type = std::add_pointer_t<std::add_const_t<ValueType>>;


    rolling_iterator(Iterator begin, Iterator end) : begin(begin), end(end)
    {

    }

    rolling_iterator(Iterator begin, Iterator end, std::vector<Iterator>&& iterators) : itrs(std::move(iterators)), end(end), begin(begin) {
        values.resize(itrs.size() + 1);
        for (size_t i = 0; i < itrs.size(); ++i)
            values[i] = *(itrs[i]);

        *values.rbegin() = ValueType{};

        if (itrs.size() > 0) {

            controllers.resize(itrs.size());
            for (auto&& itr : controllers)
                itr = false;

            *controllers.rbegin() = true;
        }
    }

    explicit rolling_iterator(rolling_iterator const& o) : itrs(o.itrs), end(o.end), begin(o.begin),
        values(o.values), controllers(o.controllers), done(o.done) {

    }

    explicit rolling_iterator(rolling_iterator&& o) : itrs(std::move(o.itrs)), end(std::move(o.end)), begin(std::move(o.begin)),
        values(std::move(o.values)), controllers(std::move(o.controllers)), done(o.done) {

    }

    rolling_iterator() {
        // Should never be called.
    }

    rolling_iterator& operator = (rolling_iterator const& o) {
        itrs = o.itrs;
        begin = o.begin;
        end = o.end;
        values = o.values;
        controllers = o.controllers;
        done = o.done;

		return *this;
    }

    rolling_iterator& operator = (rolling_iterator const&& o) {
        itrs = std::move(o.itrs);
        begin = std::move(o.begin);
        end = std::move(o.end);
        values = std::move(o.values);
        controllers = std::move(o.controllers);
        done = o.done;

        return *this;
    }

    // expands the collection of iterators by the given amount (default 1)
    // also resets the state.
    void expand(size_t count = 1) {
        values.resize(values.size() + count);
        itrs.resize(itrs.size() + count);
        controllers.resize(controllers.size() + count);

        reset();
    }

    void reset() {
        for (size_t i = 0; i < itrs.size(); ++i) {
            itrs[i] = begin;
            values[i] = *begin;
            controllers[i] = (i + 1) == itrs.size();
        }

        done = false;
    }

    void shrink_to(size_t count) {
        values.resize(count);
        itrs.resize(count);
        controllers.resize(count);

        reset();
    }

    size_t size() const { return itrs.size(); }

    value_type current() const { return values.data(); }

    void move_next() {
        // This is (in hindsight) very simple
        // we iterate every item from the end to the start
        // when a character is done, it resets, the previous character advances once, and then
        //  the last character iterates again
        // when the first character is done, that's our exit condition

		// yoda condition, when i reaches 0 it underflows to size_t::max.
        for (size_t i = itrs.size() - 1; i < itrs.size(); --i)
        {
            Iterator& itr = itrs[i];
            if (controllers[i])
                ++itr;

            // if end
            if (itr == end)
            {
                if (i > 0) {
                    // unlock previous
                    controllers[i - 1] = true;

                    // lock current
                    controllers[i] = false;

                    // reset current
                    itr = begin;
                }
            }
            else
            {
                // if not last and not locked
                if (i + 1 < itrs.size() && controllers[i])
                {
                    // lock current
                    controllers[i] = false;
                    // unlock last
                    controllers[itrs.size() - 1] = true;
                }
            }

            // if value then set otherwise done
            if (itr != end)
                values[i] = *itr;
            else
                done = true;
        }
    }

    bool all_done() const {
        return done;
    }

    std::vector<Iterator> itrs;   // the actual iterators
    std::vector<ValueType> values;// values for each iterator
    std::vector<bool> controllers;// controlling booleans, a boolean set to true means the corresponding iterator can advance
    bool done = false;            // set when the last iterator is done looping (the first one)

    Iterator end;                 // marker value to check that a given iterator is done
    Iterator begin;               // default value for a given iterator to cycle.
};

template <typename Iterator, typename ValueType = typename std::add_pointer<typename Iterator::value_type>::type>
inline bool operator == (rolling_iterator<Iterator, ValueType> const& lhs, rolling_iterator<Iterator, ValueType> const& rhs) {
    if (lhs.all_done() == rhs.all_done())
        return true;

    if (lhs.itrs.size() != rhs.itrs.size())
        return false;

    if (lhs.end != rhs.end)
        return false;

    for (size_t i = 0; i < lhs.itrs.size(); ++i)
        if (lhs.itrs[i] != rhs.itrs[i])
            return false;

    return true;
}

template <typename Iterator, typename ValueType = typename std::add_pointer<typename Iterator::value_type>::type>
inline bool operator != (rolling_iterator<Iterator, ValueType> const& lhs, rolling_iterator<Iterator, ValueType> const& rhs) {
    return !(lhs == rhs);
}
