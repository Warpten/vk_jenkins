#include "metrics.hpp"

#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>

namespace metrics {
    static std::chrono::high_resolution_clock::time_point _start;
    static std::chrono::high_resolution_clock::time_point _end;
    static std::atomic<uint64_t> counter;

    void start() {
        _start = std::chrono::high_resolution_clock::now();
        counter.store(0, std::memory_order_relaxed);
    }

    double hashes_per_second() {
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(_end - _start).count();
        return counter.load(std::memory_order_acquire) / (duration / 1.0e9);
    }

    void stop() {
        using hrc = std::chrono::high_resolution_clock;

        _end = hrc::now();
    }

    std::string elapsed_time() {

        // get number of milliseconds for the current second
        // (remainder after division into seconds)
        auto c = std::chrono::duration_cast<std::chrono::nanoseconds>(_end - _start).count();

        std::ostringstream oss;
        oss << std::setfill('0')          // set field fill character to '0'
            << (c % 1000000000) / 1000000 // format seconds
            << "."
            << std::setw(3)               // set width of milliseconds field
            << (c % 1000000) / 1000;      // format milliseconds

        return oss.str();
    }

    void increment(uint64_t count) {
        counter += count;
    }

    uint64_t total() {
        return counter.load(std::memory_order_acquire);
    }
}
