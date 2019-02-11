#include "utils.hpp"

std::ostream& pretty_bytesize::operator()(std::ostream& o)
{
    const char* labels[] = {
        "B", "KB", "MB", "GB", "TB"
    };

    uint32_t suffix = 0;
    double value = size_;
    while (value > 1024.0f && suffix < sizeof(labels) / sizeof(const char*)) {
        ++suffix;
        value /= 1024.0f;
    }

    o << value << ' ' << labels[suffix];

    return o;
}

std::ostream& operator<<(std::ostream& o, pretty_bytesize pb)
{
    return pb(o);
}
