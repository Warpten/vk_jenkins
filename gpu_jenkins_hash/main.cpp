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

    bool has(std::string const& key) {
        return std::find(_vals.begin(), _vals.end(), key) != _vals.end();
    }

	std::string getString(std::string const& key) {
		auto idx = std::find(_vals.begin(), _vals.end(), key);
		if (idx == _vals.end())
            return "";

		return *(++idx);
	}
};

int main(int argc, char* argv[]) {
	options_t options(argv, argv + argc);

    if (options.has("--help") || !options.has("--input")) {
        std::cout << "Arguments:" << std::endl;
        std::cout << "--input             The path to the input file. Defaults to 'input.txt'\n";
        std::cout << "--frames            This parameter is similar to buffering and allows the application\n"
            << "                    to enqueue work on the GPU without waiting for hash computations to finish.\n"
            << "                    The default value is 3.\n\n";
        std::cout << "--workgroupCount    This parameter defines the number of workgroups that can be dispatched at once.\n"
            << "                    The default value is 3.\n\n";
        std::cout << "--workgroupSize     This parameter defines the amount of work each workgroup can process.\n"
            << "                    The default value is 64, which is the bare minimum for any kind of performance benefit.\n\n";

        return EXIT_SUCCESS;
    }

    input_file input(options.getString("--input").c_str());

	// --frames denotes the amount of frames of data pushed to the GPU
	// while it is already calculating. This is similar to triple buffering in graphics.
    JenkinsGpuHash app(options.get("--frames", 3));

    // The number of workgroups.
    app.setWorkgroupCount(options.get("--workgroupCount", 3));

    // The size of an individual work group.
    app.setWorkgroupSize(options.get("--workgroupSize", 64));

    std::cout << "Running on: " << app.getDeviceProperties().deviceName << " (API Version "
        << VK_VERSION_MAJOR(app.getDeviceProperties().apiVersion) << "."
        << VK_VERSION_MINOR(app.getDeviceProperties().apiVersion) << "."
        << VK_VERSION_PATCH(app.getDeviceProperties().apiVersion) << ") (Driver Version "
        << VK_VERSION_MAJOR(app.getDeviceProperties().driverVersion) << "."
        << VK_VERSION_MINOR(app.getDeviceProperties().driverVersion) << "."
        << VK_VERSION_PATCH(app.getDeviceProperties().driverVersion) << ")" << std::endl;

    // The maximum number of local workgroups that can be dispatched by a single dispatch command.
    // These three values represent the maximum number of local workgroups for the X, Y, and Z dimensions, respectively.
    // The workgroup count parameters to the dispatch commands must be less than or equal to the corresponding limit.
    std::cout << "    maxComputeWorkGroupCount: { "
        << app.getDeviceProperties().limits.maxComputeWorkGroupCount[0] << ", "
        << app.getDeviceProperties().limits.maxComputeWorkGroupCount[1] << ", "
        << app.getDeviceProperties().limits.maxComputeWorkGroupCount[2] << " }" << std::endl;

    // The maximum total number of compute shader invocations in a single local workgroup.
    // The product of the X, Y, and Z sizes as specified by the LocalSize execution mode in shader modules and by the
    // object decorated by the WorkgroupSize decoration must be less than or equal to this limit.
    std::cout << "    maxComputeWorkGroupSize: { "
        << app.getDeviceProperties().limits.maxComputeWorkGroupSize[0] << ", "
        << app.getDeviceProperties().limits.maxComputeWorkGroupSize[1] << ", "
        << app.getDeviceProperties().limits.maxComputeWorkGroupSize[2] << " }" << std::endl;

    // The maximum total number of compute shader invocations in a single local workgroup.
    // The product of the X, Y, and Z sizes as specified by the LocalSize execution mode in shader modules and by the object
    // decorated by the WorkgroupSize decoration must be less than or equal to this limit.
    std::cout << "    maxComputeWorkGroupInvocations: "
        << app.getDeviceProperties().limits.maxComputeWorkGroupInvocations << std::endl;

    // Specifies support for timestamps on all graphics and compute queues.
    // If this limit is set to VK_TRUE, all queues that advertise the VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT in the
    // VkQueueFamilyProperties::queueFlags support VkQueueFamilyProperties::timestampValidBits of at least 36.
    std::cout << "    timestampComputeAndGraphics: " << (app.getDeviceProperties().limits.timestampComputeAndGraphics ? "yes" : "no") << std::endl;

    std::cout << std::endl;

    std::cout << "Hardware limits applied to user-defined configuration..." << std::endl;
	std::cout << "Workgroup count: " << app.getParams().workgroupCount << std::endl;
    std::cout << "Workgroup size: " << app.getParams().workgroupSize << std::endl;

    size_t inputIndex = 0;

    app.setDataProvider([&input, &inputIndex](std::vector<uploaded_string>* data) -> void {
        size_t i = 0;

        for (; i < data->capacity() && input.hasNext(); ++i) {
            uploaded_string& element = (*data)[i];

            auto item = input.next();
            element.word_count = (uint32_t)item.size();
            memcpy(element.words, item.data(), item.size());
        }

		data->resize(i);
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
			throw std::runtime_error(str.value() + ": duplicate values returned by hasher!");

		map.insert(str.hash);
		// std::cout << "0x" << std::setfill('0') << std::setw(8) << std::hex << str.hash << " = '" << str.value() << "';\n";
	}

	std::cout << "Hash rate: "
		<< metrics::hashes_per_second() << " hashes per second ("
		<< std::dec << metrics::total() << " hashes expected, "
		<< map.size() << " total, "
		<< metrics::elapsed_time().c_str() << " s)" << std::endl;
	std::cout << "Done! Press a key to exit" << std::endl;

    std::cin.get();

    return EXIT_SUCCESS;
}
