#include <stdexcept>
#include <iostream>
#include <vector>

#include "gpu_jenkins_hash.hpp"
#include "input_file.hpp"
#include "uploaded_string.hpp"

int main() {
    input_file input("shaders/input.txt");

    JenkinsGpuHash app(3);

    uint32_t inputIndex = 0;

    app.setDataProvider([&input, &inputIndex](std::array<uploaded_string, 64>* data) -> uint32_t {
        uint32_t i = 0;
        for (;  i < data->size() && inputIndex < input.size(); ++i) {
            uploaded_string& element = (*data)[i];

            auto item = input[inputIndex++];
            element.word_count = item.aligned_size() / 4;
            memcpy(element.words, item.integer_data(), item.aligned_size());
        }

        return i;
    });

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
