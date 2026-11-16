#pragma once
#include <string>
inline std::string ApplicationCheckMode(const char* arguments) {
    const std::string raw=arguments ? arguments : "";
    const auto first=raw.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    auto mode=raw.substr(first,raw.find_last_not_of(" \t\r\n")-first+1);
    if(mode.size()>=2 && mode.front()=='"' && mode.back()=='"') mode=mode.substr(1,mode.size()-2);
    return mode;
}
