#include <stdexcept>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string_view>
#include <set>
#include <array>

#include "gpu_jenkins_hash.hpp"
#include "input_file.hpp"
#include "uploaded_string.hpp"
#include "metrics.hpp"
#include "pattern.hpp"

#include "lookup3.hpp"

struct options_t {
private:
    std::vector<char*> _vals;

public:
    options_t(char** s, char** e) : _vals(s, e) {

    }

public:
    uint32_t get(std::string_view key, uint32_t def) {
        auto idx = std::find(_vals.begin(), _vals.end(), key);
        if (idx == _vals.end())
            return def;

        return std::atoi(*(++idx));
    }

    bool has(std::string_view key) {
        return std::find(_vals.begin(), _vals.end(), key) != _vals.end();
    }

    std::string_view getString(std::string_view  key) {
        auto idx = std::find(_vals.begin(), _vals.end(), key);
        if (idx == _vals.end())
            return "";

        return *(++idx);
    }

    template <typename R>
    auto get(std::string_view key, std::function<R(std::string_view, R)> t, R d) {
        std::string_view value = getString(key);
        if (value.size() == 0)
            return d;
        return t(value, d);
    }
};

JenkinsGpuHash app;

int main(int argc, char* argv[]) {
    options_t options(argv, argv + argc); //-V104

    // --frames denotes the amount of frames of data pushed to the GPU
    // while it is already calculating. This is similar to triple buffering in graphics.
    app = JenkinsGpuHash(options.get("--frames", 3));
    VkPhysicalDeviceLimits const& limits = app.getDeviceProperties().limits;

    std::atexit([]() {
        app.cleanup();
    });

    if (options.has("--help") || !options.has("--input")) {
        std::cout
            << "Arguments:" << std::endl;
        std::cout
            << "--input             The path to the input file. This parameter is mandatory.\n\n";
        std::cout
            << "--frames            This parameter is similar to buffering and allows the application\n"
            << "                    to enqueue work on the GPU without waiting for hash computations to finish.\n"
            << "                    The default value is 3.\n\n";
        std::cout
            << "--workgroupCount    This parameter defines the number of workgroups that can be dispatched at once.\n"
            << "                    The default value is '3,1,1'.\n\n"
            << "                    This value should not exceed '" << limits.maxComputeWorkGroupCount[0] << ","
                                                                    << limits.maxComputeWorkGroupCount[1] << ","
                                                                    << limits.maxComputeWorkGroupCount[2] << "' on your system.\n\n";
        std::cout
            << "--workgroupSize     This parameter defines the amount of work each workgroup can process.\n"
            << "                    The default value is '64,64,64', which is the bare minimum for any kind of performance benefit.\n\n"
            << "                    This value should not exceed '" << limits.maxComputeWorkGroupSize[0] << ","
                                                                    << limits.maxComputeWorkGroupSize[1] << ","
                                                                    << limits.maxComputeWorkGroupSize[2] << "' on your system.\n\n"
            << "                    These values multiplied should also not exceed " << limits.maxComputeWorkGroupInvocations << " on your system.\n\n";
        std::cout
            << "--validate          Performs checks of GPU-computed values against CPU-computed values. You generally do not want to run"
            << "                    with this flag, since it's going to kill your hash rate. This is a boolean flag, it doesn't require"
            << "                    a value.\n\n"
            << "                    Use for debugging only.\n\n";

        if (!options.has("--input")) {
            std::cout << "Press a key to exit" << std::endl;
            std::cin.get();
        }

        return EXIT_SUCCESS;
    }

    input_file input(options.getString("--input").data());

    std::function<std::array<uint32_t, 3>(std::string_view, std::array<std::uint32_t, 3>)> workgroupParser = [](std::string_view v, std::array<uint32_t, 3> def) -> std::array<uint32_t, 3> {
        std::array<uint32_t, 3> sizes;
        size_t ofs = 0;
        for (uint32_t i = 0; i < 3; ++i) {
            sizes[i] = std::atoi(v.data());

            size_t next = v.find(',');
            if (next == std::string_view::npos && i < 2)
                return def;
            v = v.substr(next + 1);
        }

        return sizes;
    };

    std::array<uint32_t, 3> workgroupSize = options.get("--workgroupSize", workgroupParser, { 64, 1, 1 });
    std::array<uint32_t, 3> workgroupCount = options.get("--workgroupCount", workgroupParser, { 3, 1, 1 });

    app.setWorkgroupSize(workgroupSize[0], workgroupSize[1], workgroupSize[2]);
    app.setWorkgroupCount(workgroupCount[0], workgroupCount[1], workgroupCount[2]);

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
        << limits.maxComputeWorkGroupCount[0] << ", "
        << limits.maxComputeWorkGroupCount[1] << ", "
        << limits.maxComputeWorkGroupCount[2] << " }" << std::endl;

    // The maximum total number of compute shader invocations in a single local workgroup.
    // The product of the X, Y, and Z sizes as specified by the LocalSize execution mode in shader modules and by the
    // object decorated by the WorkgroupSize decoration must be less than or equal to this limit.
    std::cout << "    maxComputeWorkGroupSize: { "
        << limits.maxComputeWorkGroupSize[0] << ", "
        << limits.maxComputeWorkGroupSize[1] << ", "
        << limits.maxComputeWorkGroupSize[2] << " }" << std::endl;

    // The maximum total number of compute shader invocations in a single local workgroup.
    // The product of the X, Y, and Z sizes as specified by the LocalSize execution mode in shader modules and by the object
    // decorated by the WorkgroupSize decoration must be less than or equal to this limit.
    std::cout << "    maxComputeWorkGroupInvocations: "
        << limits.maxComputeWorkGroupInvocations << std::endl;

    // Specifies support for timestamps on all graphics and compute queues.
    // If this limit is set to VK_TRUE, all queues that advertise the VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT in the
    // VkQueueFamilyProperties::queueFlags support VkQueueFamilyProperties::timestampValidBits of at least 36.
    std::cout << "    timestampComputeAndGraphics: " << (limits.timestampComputeAndGraphics ? "yes" : "no") << std::endl;

    std::cout << "\n\n";

    std::cout << "Hardware limits applied to user-defined configuration...\n";
    std::cout << "\n>> Workgroup count: { " << app.getParams().workgroupCount[0] << ", " << app.getParams().workgroupCount[1] << ", " << app.getParams().workgroupCount[2] << " }";
    std::cout << "\n>> Workgroup sizes: { " << app.getParams().workgroupSize[0] << ", " << app.getParams().workgroupSize[1] << ", " << app.getParams().workgroupSize[2] << " }";
    std::cout << "\n>> Number of lookahead frames: " << app.getFrameCount();

    std::cout << std::endl;

    app.setDataProvider([&input](uploaded_string* data, size_t capacity) -> size_t {
        size_t i = 0;

        memset(data, 0, sizeof(uploaded_string) * capacity);
        for (; i < capacity && input.hasNext(); ++i) {

            uploaded_string& element = *data;
            if (!input.next(element))
                break;

            ++data;
        }

        return i;
    });

    size_t output = 0;
    std::vector<std::string> failed_hashes;
    app.setOutputHandler([&output, &options, &failed_hashes](uploaded_string* data, size_t count) -> void {
        if (options.has("--validate"))
        {
            for (size_t i = 0; i < count; ++i)
            {
                uploaded_string& itr = data[i];

                uint32_t gpuHash = itr.get_hash();
                uint32_t cpuHash = itr.get_cpu_hash();
                if (gpuHash != cpuHash)
                    failed_hashes.push_back(std::string(itr.value()));
            }
        }

        output += count;
    });

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

	std::cout << ">> RESULTS:" << std::endl;

    if (failed_hashes.size() > 0 && options.has("--validate")) {
        std::cout << "Examples of failed hashes: " << std::endl;
        for (auto&& itr : failed_hashes)
            std::cout << "[] " << itr << std::endl;
        std::cout << std::endl;
    }

    std::cout << "Hash rate: "
        << std::dec << uint64_t(metrics::hashes_per_second()) << " hashes per second ("
        << metrics::total() << " hashes expected, "
        << output << " total, ";
    if (options.has("--validate"))
        std::cout << (output - failed_hashes.size()) << " correct, " << (failed_hashes.size()) << " wrong, ";

    std::cout << metrics::elapsed_time().c_str() << " s)" << std::endl;
    std::cout << "Done! Press a key to exit" << std::endl;

    std::cin.get();

    return EXIT_SUCCESS;
}
