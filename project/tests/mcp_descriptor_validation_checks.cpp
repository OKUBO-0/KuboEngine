#include "../engine/services/McpDescriptorValidation.h"
#include <iostream>
#include <vector>
int main() {
    std::vector<std::pair<std::string, bool>> cases = {
        {R"({"pid":1,"format":"v2","creation":2})", true},
        {R"({"pid":1,"pid":1})", false},
        {R"({"process_creation_low":1,"process_creation_low":2})", false},
        {R"({"pid":1,"p\u0069d":2})", false},
        {R"({"p\u0069d":1,"pid":2})", false},
        {R"({"text":"colon:key : escaped \"quoted\"","pid":1})", true},
        {R"({"nested":{"pid":1},"pid":2})", true},
        {R"({"nested":{"pid":1},"nested":{"pid":2}})", false},
        {R"({"pid" : 1, "pid" : 2})", false},
        {R"({"broken":"unfinished})", false},
        {R"({"a":[[[[[[[[1]]]]]]]]})", false},
        {R"({"a":[[[[[[[1]]]]]]]})", true},
        {R"({"":1,"\u0000":2})", true},
        {R"({"":1,"":2})", false},
        {R"({"\u0000":1,"\u0000":2})", false},
        {R"({"a\\b":1,"a\u005cb":2})", false},
        {R"({"a\"b":1,"a\u0022b":2})", false}
    };
    if (Engine::Services::McpDescriptorValidation::Preflight(nullptr, 1) ||
        Engine::Services::McpDescriptorValidation::Preflight("", 0)) return 1;
    cases.emplace_back(std::string(4096, ' '), true);
    cases.emplace_back(std::string(4097, ' '), false);
    std::string atLimit = "{";
    for (size_t i = 0; i < 64; ++i) {
        if (i) atLimit += ',';
        atLimit += "\"k" + std::to_string(i) + "\":0";
    }
    cases.emplace_back(atLimit + "}", true);
    cases.emplace_back(atLimit + ",\"overflow\":0}", false);
    for (const auto& entry : cases) {
        if (Engine::Services::McpDescriptorValidation::Preflight(entry.first.data(), entry.first.size()) != entry.second) {
            std::cerr << "FAIL: " << entry.first << '\n'; return 1;
        }
    }
    std::cout << "PASS: production unique decoded top-level keys; literal/escaped duplicates, value-string isolation, nested keys, depth and unterminated strings\n";
}
