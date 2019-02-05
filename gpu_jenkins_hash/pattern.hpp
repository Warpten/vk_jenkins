#pragma once

#include "uploaded_string.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include <algorithm>
#include <set>
#include <string_view>
#include <cctype>

struct pattern_t {
private:
    struct node_t {
        node_t* next = nullptr;

        virtual uint32_t apply(std::vector<char>& storage, uint32_t offset) = 0;
        virtual void selectNextPermutation() = 0;
        virtual void addAlphabet(const char* first, const char* last) = 0;
        virtual void addAlphabet(const char c) = 0;

        virtual void setCount(uint32_t) = 0;
        virtual void setMinCount(uint32_t) = 0;
        virtual void setMaxCount(uint32_t) = 0;
        virtual void setPosition(uint32_t) = 0;
    };

    struct fixed_range_t : public node_t {
        std::string characters;

        fixed_range_t(std::string const& str) : characters(str) {}

        uint32_t apply(std::vector<char>& storage, uint32_t offset) override {
            memcpy(storage.data() + offset, characters.data(), characters.size());
            return offset + characters.size();
        }

        void selectNextPermutation() override {}
        void addAlphabet(const char* first, const char* last) override {}
        void addAlphabet(const char c) override {}

        void setCount(uint32_t) override {}
        void setMinCount(uint32_t) override {}
        void setMaxCount(uint32_t) override {}
        void setPosition(uint32_t) override {}
    };

    struct replacement_range_t : public node_t {
        uint32_t minCount = 0;
        uint32_t maxCount = 0;
        size_t position = 0;
        std::set<char> alphabet;

        uint32_t apply(std::vector<char>& storage, uint32_t offset) {
        }

        void selectNextPermutation() override {
        }

        void addAlphabet(const char* first, const char* last) override {
            alphabet.insert(first, last);
        }

        void addAlphabet(const char c) override {
            alphabet.insert(c);
        }

        void setCount(uint32_t c) override { minCount = maxCount = c; }
        void setMinCount(uint32_t c) override { minCount = c; }
        void setMaxCount(uint32_t c) override { maxCount = c; }
        void setPosition(uint32_t p) override { position = p; }
    };
    
    node_t* head = nullptr;
    node_t* tail = nullptr;

public:
    pattern_t(std::string_view regex) {
        // The supported regex expressions are trivial.
        // Count modifiers: {min, max} or {count}
        // Ranges: [alpha], [num], [hex], [alnum], [path], [a-z]
        //    To combine ranges: [alpha|num].

        size_t patternStartItr = regex.find('[', 0);
        size_t patternEndItr = regex.find(']', patternStartItr + 1);

        if (patternStartItr == 0)
            head = tail = new replacement_range_t();
        else
            head = tail = new fixed_range_t(std::string(regex.substr(0, patternStartItr)));

        size_t currentReplacementPosition = patternStartItr;
        while (patternStartItr != std::string::npos) {

            if (patternStartItr != std::string::npos) {
                patternEndItr = regex.find(']', patternStartItr + 1);

                auto rangesNames = regex.substr(patternStartItr + 1, patternEndItr - patternStartItr - 1);

                auto patternHandler = [tail = this->tail](std::string_view const& r) -> void {
                    if (r == "hex") {
                        constexpr const char hex_alphabet[] = "ABCDEF0123456789";
                        tail->addAlphabet(hex_alphabet, hex_alphabet + sizeof(hex_alphabet));
                    } else if (r == "alpha") {
                        constexpr const char alpha_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
                        tail->addAlphabet(alpha_alphabet, alpha_alphabet + sizeof(alpha_alphabet));
                    } else if (r == "num") {
                        constexpr const char num_alphabet[] = "0123456789";
                        tail->addAlphabet(num_alphabet, num_alphabet + sizeof(num_alphabet));
                    } else if (r == "alnum" || r == "alphanum") {
                        constexpr const char num_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
                        tail->addAlphabet(num_alphabet, num_alphabet + sizeof(num_alphabet));
                    } else if (r == "path") {
                        constexpr const char path_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. \\";
                        tail->addAlphabet(path_alphabet, path_alphabet + sizeof(path_alphabet));
                    } else {
                        size_t splitPos = r.find('-');
                        if (splitPos == std::string::npos) {
                            throw std::runtime_error("invalid range");
                        } else {
                            char startCharacter = std::toupper(r[splitPos - 1]);
                            char endCharacter = std::toupper(r[splitPos + 1]);

                            for (; startCharacter <= endCharacter; ++startCharacter)
                                tail->addAlphabet(startCharacter);
                        }
                    }
                };

                size_t rangeEndItr = rangesNames.find('|');
                size_t rangeStartItr = 0;
                while (rangeEndItr != std::string::npos) {
                    auto range = rangesNames.substr(rangeStartItr, rangeEndItr - rangeStartItr);
                    patternHandler(range);
                    rangeStartItr = rangeEndItr + 1;
                    rangeEndItr = rangesNames.find('|', rangeStartItr);
                }
                patternHandler(rangesNames.substr(rangeStartItr, rangesNames.size() - rangeStartItr));
            
                size_t endCountDelim = regex.find('}', patternEndItr + 2);

                if (regex[patternEndItr + 1] == '{') { // x to y, or x
                    size_t comma_pos = regex.find(',', patternEndItr + 2);
                    if (comma_pos == std::string::npos) { // x
                        tail->setCount(std::stoi(regex.substr(patternEndItr + 2, endCountDelim - patternEndItr - 2).data()));
                    }
                    else { // x to y
                        tail->setMinCount(std::stoi(regex.substr(patternEndItr + 2, comma_pos - patternEndItr - 2).data()));
                        tail->setMaxCount(std::stoi(regex.substr(comma_pos + 1, endCountDelim - comma_pos - 1).data()));
                    }
                }
                else
                    throw std::runtime_error("invalid pattern");
            
                tail->setPosition(currentReplacementPosition);
                currentReplacementPosition += 1;

                size_t oldStart = patternStartItr;
                patternStartItr = regex.find('[', patternStartItr + 1);
                if (patternStartItr != std::string::npos)
                    currentReplacementPosition = patternStartItr - (endCountDelim - oldStart);
            }
        }
    }

    ~pattern_t() {
        std::function<void(node_t*)> last_deleter;
        last_deleter = [&](node_t* node) -> void {
            if (node->next != nullptr) {
                last_deleter(node->next);
                node->next = nullptr;
            }
            delete node;
        };

        last_deleter(head);
        tail = nullptr;
    }
};