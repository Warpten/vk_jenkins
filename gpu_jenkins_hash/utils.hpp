#pragma once

#include <locale>

struct pretty_number : public std::numpunct<char> {
protected:
    virtual char_type do_thousands_sep() const { return ','; }
    virtual std::string do_grouping() const { return "\03"; }
    virtual char_type do_decimal_point() const { return '.'; }
};

struct pretty_bytesize {

    pretty_bytesize(size_t size) : size_(size) { }

    std::ostream& operator()(std::ostream& o);

private:
    size_t size_;
};

std::ostream& operator << (std::ostream& o, pretty_bytesize pb);
