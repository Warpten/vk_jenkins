#include <stdexcept>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string_view>
#include <set>

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

    std::string getString(std::string_view  key) {
        auto idx = std::find(_vals.begin(), _vals.end(), key);
        if (idx == _vals.end())
            return "";

        return *(++idx);
    }
};

int main(int argc, char* argv[]) {
    pattern_t pattern("PUT/THE/MEMES/IN/THE/[a-c]{1, 7}/INTERFACE/[0-9]/BAG.MP3");
    std::cout << pattern.count() << " permutations." << std::endl;

    options_t options(argv, argv + argc); //-V104

    // --frames denotes the amount of frames of data pushed to the GPU
    // while it is already calculating. This is similar to triple buffering in graphics.
    JenkinsGpuHash app(options.get("--frames", 3));
    VkPhysicalDeviceLimits const& limits = app.getDeviceProperties().limits;

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
            << "                    The default value is 3.\n\n"
            << "                    This value should not exceed " << limits.maxComputeWorkGroupCount[0] << " on your system.\n\n";
        std::cout
            << "--workgroupSize     This parameter defines the amount of work each workgroup can process.\n"
            << "                    The default value is 64, which is the bare minimum for any kind of performance benefit.\n\n"
            //                                                        because workgroup size y and z are 1
            << "                    This value should not exceed " << std::min(limits.maxComputeWorkGroupSize[0], limits.maxComputeWorkGroupInvocations) << " on your system.\n\n";
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

    input_file input(options.getString("--input").c_str());

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

    std::cout << std::endl;

    std::cout << "Hardware limits applied to user-defined configuration..." << std::endl;
    std::cout << "Workgroup count: " << app.getParams().workgroupCount << std::endl;
    std::cout << "Workgroup size: " << app.getParams().workgroupSize << std::endl;

    app.setDataProvider([&input, &pattern](std::vector<uploaded_string>* data) -> void {
        size_t i = 0;

        // pattern.collect(*data);

        for (; i < data->capacity() && input.hasNext(); ++i) {
            uploaded_string& element = (*data)[i];
            element = input.next();
        }

        data->resize(i);
    });

    size_t output = 0;
    std::vector<std::string> failed_hashes;
    app.setOutputHandler([&output, &options, &failed_hashes](std::vector<uploaded_string>* data) -> void {
        // No-op for the first call of each frame
        if (data->size() == 0)
            return;

        if (options.has("--validate"))
        {
            for (uploaded_string const& itr : *data)
            {
                uint32_t gpuHash = itr.get_hash();
                uint32_t cpuHash = itr.get_cpu_hash();
                if (gpuHash != cpuHash)
                    failed_hashes.push_back(std::string(itr.value()));
            }
        }

        output += data->size();
        data->resize(0);
    });

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

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
