#include "string_processing.h"

std::vector<std::string_view> SplitIntoWords(std::string_view text) {
    std::vector<std::string_view> result;
    const size_t pos_end = text.npos;
    size_t space = text.find(' ');

    while (space != pos_end) {
        result.push_back(text.substr(0, space));
        text.remove_prefix(space + 1);
        space = text.find(' ');
    }
    result.push_back(text.substr(0, space));
    return result;
}