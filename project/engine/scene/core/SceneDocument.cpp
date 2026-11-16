#include "SceneDocument.h"
#include "engine/base/AtomicFile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Engine::Scene {
namespace {
constexpr size_t kMaxDocumentBytes = SceneDocument::MaxBytes;

void ValidateSceneText(std::string& text)
{
    if(text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);
    if(text.empty() || text.find('\0')!=std::string::npos ||
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0)
        throw std::runtime_error("Scene must contain valid UTF-8 JSON without raw NUL");
    // crude_json uses recursive parsing; bound nesting before entering it.
    size_t depth=0;bool quoted=false,escaped=false;
    for(const char character:text) {
        if(quoted) {
            if(escaped) escaped=false;
            else if(character=='\\') escaped=true;
            else if(character=='\"') quoted=false;
        } else if(character=='\"') quoted=true;
        else if(character=='{' || character=='[') {
            if(++depth>128) throw std::runtime_error("Scene JSON nesting exceeds 128 levels");
        } else if(character=='}' || character==']') {if(depth) --depth;}
    }
}

void Check(const SceneValue& value, const SceneValue& schema, const std::string& path)
{
    if (value.type() != schema.type()) {
        throw std::runtime_error(path + ": unexpected value type");
    }
    if (schema.is_object()) {
        for (const auto& [key, field] : schema.get<crude_json::object>()) {
            if (!value.contains(key)) { throw std::runtime_error(path + "." + key + ": missing field"); }
            Check(value[key], field, path + "." + key);
        }
        if (value.get<crude_json::object>().size() != schema.get<crude_json::object>().size()) {
            throw std::runtime_error(path + ": unknown field");
        }
    } else if (schema.is_array()) {
        const auto& a = value.get<crude_json::array>();
        const auto& b = schema.get<crude_json::array>();
        if (a.size() != b.size()) { throw std::runtime_error(path + ": incorrect array size"); }
        for (size_t i = 0; i < a.size(); ++i) { Check(a[i], b[i], path + "[" + std::to_string(i) + "]"); }
    } else if (schema.is_number()) {
        const double n = value.get<crude_json::number>();
        if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) {
            throw std::runtime_error(path + ": number is outside the finite float range");
        }
    } else if (schema.is_string()) {
        // Version 1 describes the fixed sample scene. Reject unsupported asset/ID changes.
        if (value.get<crude_json::string>() != schema.get<crude_json::string>()) {
            throw std::runtime_error(path + ": unsupported scene identifier or asset reference");
        }
    }
}
}

bool SceneDocument::Read(const std::filesystem::path& path,SceneValue& result,std::string& error)
{
    try {
        if(path.empty() || path.native().find(L'\0')!=std::wstring::npos)
            throw std::runtime_error("Scene path must be non-empty without NUL");
        const auto absolute=std::filesystem::absolute(path).lexically_normal();
        // Hold one handle; incompatible writers/replacement are denied during
        // the snapshot read. This is not an enduring multi-editor lock.
        struct FileHandle {
            HANDLE value;
            ~FileHandle() {if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
        } file{CreateFileW(absolute.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open Scene for reading (Windows error "+std::to_string(GetLastError())+")");
        LARGE_INTEGER size{};
        if(GetFileType(file.value)!=FILE_TYPE_DISK || !GetFileSizeEx(file.value,&size) || size.QuadPart<0)
            throw std::runtime_error("Scene must be a readable disk file");
        if(static_cast<unsigned long long>(size.QuadPart)>MaxBytes) throw std::runtime_error("Scene exceeds the 4 MiB size limit");
        std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0;
        if(!ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&count,nullptr) || count!=text.size())
            throw std::runtime_error("Cannot read complete Scene");
        char extra=0;DWORD trailing=0;
        if(!ReadFile(file.value,&extra,1,&trailing,nullptr) || trailing!=0) throw std::runtime_error("Scene changed while reading");
        ValidateSceneText(text);
        auto parsed=SceneValue::parse(text);
        if(!parsed.is_object()) throw std::runtime_error("Scene JSON root must be an object");
        result=std::move(parsed);error.clear();return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

bool SceneDocument::Validate(const SceneValue& value, const SceneValue& schema, std::string& error)
{
    try {
        Check(value, schema, "scene");
        if (!value.is_object() || !value.contains("version") || !value["version"].is_number() ||
            value["version"].get<crude_json::number>() != 1.0) {
            throw std::runtime_error("Unsupported scene version");
        }
        error.clear();
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool SceneDocument::Load(const std::filesystem::path& path, const SceneValue& schema,
    SceneValue& result, std::string& error)
{
    try {
        SceneValue parsed;
        if(!Read(path,parsed,error)) return false;
        if (!Validate(parsed, schema, error)) { return false; }
        result = std::move(parsed);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool SceneDocument::Save(const std::filesystem::path& path, const SceneValue& value,
    const SceneValue& schema, std::string& error, bool replaceExisting)
{
    if (!Validate(value, schema, error)) { return false; }
    return Write(path,value,error,replaceExisting);
}

bool SceneDocument::Write(const std::filesystem::path& path,const SceneValue& value,
    std::string& error,bool replaceExisting)
{
    try {
        auto text = value.dump(2);
        if(text.size()>kMaxDocumentBytes) throw std::runtime_error("Scene exceeds the 4 MiB size limit");
        ValidateSceneText(text);
        return Engine::Base::AtomicFile::Write(path,text,kMaxDocumentBytes,error,replaceExisting);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
}
