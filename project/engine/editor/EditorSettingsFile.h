#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/base/AtomicFile.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace Engine::Editor {
// Native Windows paths and memory-backed ImGui IO avoid ANSI filename conversion.
class EditorSettingsFile final {
public:
    static constexpr size_t Limit = 1024 * 1024;
    static bool Read(const std::filesystem::path& path, const std::filesystem::path& legacy,
        std::string& text, std::string& error) {
        try {
            auto source=path;
            if(!std::filesystem::exists(source)) source=legacy;
            if(source.empty() || !std::filesystem::exists(source)) {text.clear();error.clear();return true;}
            const auto size=std::filesystem::file_size(source);
            if(size>Limit) throw std::runtime_error("Editor settings exceed 1 MiB");
            std::ifstream input(source,std::ios::binary);
            std::string candidate(static_cast<size_t>(size),'\0');
            if(!input.read(candidate.data(),static_cast<std::streamsize>(size)) || input.peek()!=EOF)
                throw std::runtime_error("Cannot read editor settings");
            Validate(candidate);
            text=std::move(candidate);error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }
    static bool Write(const std::filesystem::path& path,const std::string& text,std::string& error) {
        try {
            Validate(text);
            return Engine::Base::AtomicFile::Write(path,text,Limit,error);
        } catch(const std::exception& exception) {
            error=exception.what();return false;
        }
    }
private:
    static void Validate(const std::string& text) {
        if(text.size()>Limit || text.find('\0')!=std::string::npos || (!text.empty() &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0))
            throw std::runtime_error("Invalid UTF-8 editor settings");
    }
};
}
