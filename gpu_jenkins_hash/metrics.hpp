#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace metrics {
	double hashes_per_second();

	void increment(uint64_t);

	uint64_t total();

	std::string elapsed_time();

	void stop();
	void start();
}
