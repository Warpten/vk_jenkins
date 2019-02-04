#include <stdexcept>
#include <iostream>
#include <vector>
#include <iomanip>
#include <set>

#include "gpu_jenkins_hash.hpp"
#include "input_file.hpp"
#include "uploaded_string.hpp"
#include "metrics.hpp"

struct options_t {
private:
	std::vector<char*> _vals;

public:
	options_t(char** s, char** e) : _vals(s, e) {

	}

public:
	uint32_t get(std::string const& key, uint32_t def) {
		auto idx = std::find(_vals.begin(), _vals.end(), key);
		if (idx == _vals.end())
			return def;

		return std::atoi(*(++idx));
	}

	std::string const& get(std::string const& key, std::string const& def) {
		auto idx = std::find(_vals.begin(), _vals.end(), key);
		if (idx == _vals.end())
			return def;

		return *(++idx);
	}

	uint32_t get(std::string const& key, std::string const& shorthand, uint32_t def) {
		auto idx = std::find(_vals.begin(), _vals.end(), key);
		if (idx == _vals.end())
			idx = std::find(_vals.begin(), _vals.end(), shorthand);

		if (idx == _vals.end())
			return def;

		return std::atoi(*(++idx));
	}

	std::string const& get(std::string const& key, std::string const& shorthand, std::string const& def) {
		auto idx = std::find(_vals.begin(), _vals.end(), key);
		if (idx == _vals.end())
			idx = std::find(_vals.begin(), _vals.end(), shorthand);

		if (idx == _vals.end())
			return def;

		return *(++idx);
	}
};

int main(int argc, char* argv[]) {
    input_file input("shaders/input.txt");

	options_t options(argv, argv + argc);

	// --frames denotes the amount of frames of data pushed to the GPU
	// while it is already calculating. This is similar to triple buffering in graphics.
    JenkinsGpuHash app(options.get("--frames", 1));
	// The number of workgroups. Each workgroup already processes 64 hashes.
	// and this is the number of groups we send out.
	app.setWorkgroupCount(options.get("--groupCount", 3));

	std::cout << "Running on: " << app.getDeviceProperties().deviceName << std::endl;
	std::cout << "Workgroup count: " << app.getParams().workgroupCount << std::endl;

    size_t inputIndex = 0;

    app.setDataProvider([&input, &inputIndex](std::vector<uploaded_string>* data) -> uint32_t {
        size_t i = 0;

        for (; i < data->capacity() && inputIndex < input.size(); ++i) {
            uploaded_string& element = (*data)[i];

            auto item = input[inputIndex++];
            element.word_count = (uint32_t)item.size();
            memcpy(element.words, item.data(), item.size());
        }

		data->resize(i);

        return uint32_t(i);
    });

	std::vector<uploaded_string> output;
	app.setOutputHandler([&output](std::vector<uploaded_string>* data, uint32_t actualCount) -> void {
		// No-op for the first call of each frame
		if (actualCount == 0)
			return;

		std::move(data->begin(), data->begin() + actualCount, std::back_inserter(output));
		data->resize(0);
	});

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

	std::set<uint32_t> map;
	for (uploaded_string const& str : output) {

		if (map.find(str.hash) != map.end())
			throw std::runtime_error("duplicate values returned by hasher!");

		map.insert(str.hash);
		// std::cout << "0x" << std::setfill('0') << std::setw(8) << std::hex << str.hash << " = '" << str.value() << "';\n";
	}

	std::cout << "Hash rate: "
		<< metrics::hashes_per_second() << " hashes per second ("
		<< std::dec << metrics::total() << " hashes expected, "
		<< map.size() << " total, "
		<< metrics::elapsed_time().c_str() << " s)" << std::endl;
	std::cout << "Done! Press a key to exit" << std::endl;

    int x;
    std::cin >> x;

    return EXIT_SUCCESS;
}
