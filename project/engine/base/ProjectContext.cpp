#include "ProjectContext.h"
#include "AssetPath.h"
#include "AtomicFile.h"
#include <Windows.h>

#include "externals/imgui_node_editor/crude_json.h"

#include <fstream>
#include <stdexcept>

namespace Engine::Base {
namespace {

constexpr size_t kMaxManifestBytes = 1024 * 1024;

void ValidateManifestText(const std::string& text,const char* field,size_t limit,bool allowEmpty=false)
{
    if(text.empty() && allowEmpty) return;
    if(text.empty() || text.size()>limit || text.find('\0')!=std::string::npos ||
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)==0)
        throw std::runtime_error(std::string("Invalid UTF-8 manifest field: ")+field);
    for(unsigned char value:text) if(value<32) throw std::runtime_error(std::string("Control character in manifest field: ")+field);
}

std::filesystem::path NormalizeProjectRelative(const std::filesystem::path& path)
{
    if(path.has_root_name() || path.has_root_directory()) throw std::runtime_error("Project setting paths must be relative to the project root");
    const auto normalized=path.lexically_normal();
    for(const auto& component:normalized) {
        if(component==L"..") throw std::runtime_error("Project-relative path escapes the project root");
        const auto& text=component.native();
        if(text.find(L'\0')!=std::wstring::npos || (component!=L"." && !text.empty() && (text.back()==L'.' || text.back()==L' ')))
            throw std::runtime_error("Ambiguous trailing dot/space or NUL in project path component");
    }
    return normalized;
}

std::string NormalizeManifestPath(const std::string& text,const char* field,bool allowEmpty=false)
{
    ValidateManifestText(text,field,4096,allowEmpty);
    if(text.empty()) return text;
    if(text.find_first_of(":<>\"|?*")!=std::string::npos) throw std::runtime_error(std::string("Invalid filename characters in ")+field);
    const auto path=NormalizeProjectRelative(PathFromUtf8(text));
    if(path.empty() || path==L".") throw std::runtime_error(std::string("Manifest path must name a project entry: ")+field);
    return PathToUtf8(path);
}

bool HasPathPrefix(const std::filesystem::path& path,const std::filesystem::path& prefix)
{
    auto component=path.begin();
    for(const auto& expected:prefix) {
        if(component==path.end() || _wcsicmp(component->c_str(),expected.c_str())!=0) return false;
        ++component;
    }
    return true;
}

void ValidateProjectName(const std::string& name)
{
    if(name.empty() || name.size()>240 || name.find('\0')!=std::string::npos ||
        name.find_first_of("\\/:<>\"|?*")!=std::string::npos || name.back()=='.' || name.back()==' ' ||
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0)==0)
        throw std::runtime_error("Project name must be valid UTF-8 without reserved filename characters or trailing dot/space");
    for(unsigned char character:name) if(character<32) throw std::runtime_error("Project name contains a control character");
    auto base=name.substr(0,name.find('.'));
    for(char& character:base) if(character>='a' && character<='z') character-=32;
    if(base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" ||
        (base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='1' && base[3]<='9'))
        throw std::runtime_error("Project name uses a reserved device name");
}

void WriteNewProjectFile(const std::filesystem::path& path,const std::string& bytes,bool preserveExisting=false)
{
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {
        const auto code=GetLastError();
        if(preserveExisting && (code==ERROR_FILE_EXISTS || code==ERROR_ALREADY_EXISTS) && std::filesystem::is_regular_file(path)) return;
        throw std::runtime_error("Cannot exclusively create project file; an existing file is never overwritten");
    }
    DWORD written=0;
    const bool success=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)!=FALSE && written==bytes.size() && FlushFileBuffers(file)!=FALSE;
    CloseHandle(file);
    if(!success) throw std::runtime_error("Cannot write project file; inspect the new project directory before retrying");
}

std::string ReadString(const crude_json::value& object, const char* key,
    const std::string& fallback, bool required)
{
    if (!object.contains(key)) {
        if (required) {
            throw std::runtime_error(std::string("Project manifest is missing '") + key + "'");
        }
        return fallback;
    }
    if (!object[key].is_string()) {
        throw std::runtime_error(std::string("Project manifest field '") + key + "' must be a string");
    }
    return object[key].get<crude_json::string>();
}

std::filesystem::path NormalizeAbsolute(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

} // namespace

bool ProjectContext::Open(const std::filesystem::path& projectFile, std::string& error)
{
    try {
        const auto manifest = FindManifest(projectFile);
        if (manifest.empty()) {
            throw std::runtime_error("KuboEngine project manifest was not found");
        }
        return LoadManifest(manifest, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool ProjectContext::LoadManifest(const std::filesystem::path& projectFile, std::string& error)
{
    const auto normalized = NormalizeAbsolute(projectFile);
    const auto size = std::filesystem::file_size(normalized);
    if (size > kMaxManifestBytes) {
        throw std::runtime_error("KuboEngine project manifest exceeds the 1 MiB limit");
    }

    std::ifstream input(normalized, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open KuboEngine project manifest");
    }
    std::string text(static_cast<size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())) || input.peek()!=EOF) {
        throw std::runtime_error("Cannot read KuboEngine project manifest");
    }
    const auto originalBytes=text;
    if(text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);

    const auto parsed = crude_json::value::parse(text);
    if (!parsed.is_object()) {
        throw std::runtime_error("KuboEngine project manifest root must be an object");
    }

    Settings settings{};
    settings.name = ReadString(parsed, "name", {}, true);
    settings.engineVersion = ReadString(parsed, "engineVersion", settings.engineVersion, false);
    settings.startupScene = ReadString(parsed, "startupScene", settings.startupScene, false);
    settings.assetDirectory = ReadString(parsed, "assetDirectory", settings.assetDirectory, false);
    ValidateManifestText(settings.name,"name",240);
    ValidateManifestText(settings.engineVersion,"engineVersion",128);
    settings.startupScene=NormalizeManifestPath(settings.startupScene,"startupScene",true);
    settings.assetDirectory=NormalizeManifestPath(settings.assetDirectory,"assetDirectory");
    if(parsed.contains("gameOutput")) {
        const auto& output=parsed["gameOutput"];
        if(!output.is_object() || !output.contains("width") || !output.contains("height") || !output["width"].is_number() || !output["height"].is_number())
            throw std::runtime_error("gameOutput requires numeric width and height");
        const auto width=output["width"].get<crude_json::number>(),height=output["height"].get<crude_json::number>();
        if(!std::isfinite(width) || !std::isfinite(height) || width<1 || height<1 || width>4096 || height>4096 || std::floor(width)!=width || std::floor(height)!=height)
            throw std::runtime_error("gameOutput dimensions must be integers from 1 to 4096");
        settings.gameOutput={static_cast<uint32_t>(width),static_cast<uint32_t>(height)};
        if(!ValidGameOutputExtent(settings.gameOutput)) throw std::runtime_error("gameOutput exceeds its 8388608 pixel allocation limit");
    }

    if(parsed.contains("luaLimits")) {
        const auto& limits=parsed["luaLimits"];
        if(!limits.is_object())throw std::runtime_error("luaLimits must be an object");
        const auto read=[&](const char* field,double maximum){
            if(!limits.contains(field) || !limits[field].is_number())throw std::runtime_error(std::string("luaLimits requires numeric ")+field);
            const auto value=limits[field].get<crude_json::number>();
            if(!std::isfinite(value) || value<1 || value>maximum || std::floor(value)!=value)throw std::runtime_error(std::string("luaLimits has invalid ")+field);
            return static_cast<size_t>(value);
        };
        settings.luaLimits={read("memoryBytes",1024.0*1024*1024),read("states",4096),read("frameInstructions",100000000)};
    }

    projectFile_ = normalized;
    rootPath_ = normalized.parent_path();
    settings_ = std::move(settings);
    manifestBytes_ = originalBytes;
    error.clear();
    return true;
}

bool ProjectContext::SaveGameOutput(ViewExtent extent,std::string& error)
{
    return SaveSettings(&extent,nullptr,nullptr,error);
}

bool ProjectContext::SaveLaunchSettings(ViewExtent extent,const std::string& startupScene,std::string& error)
{
    return SaveSettings(&extent,&startupScene,nullptr,error);
}

bool ProjectContext::SaveLaunchSettings(ViewExtent extent,const std::string& startupScene,Scripting::LuaResourceLimits limits,std::string& error)
{
    return SaveSettings(&extent,&startupScene,&limits,error);
}
bool ProjectContext::SaveLuaLimits(Scripting::LuaResourceLimits limits,std::string& error)
{
    return SaveSettings(nullptr,nullptr,&limits,error);
}
bool ProjectContext::SaveSettings(const ViewExtent* extent,const std::string* startupScene,const Scripting::LuaResourceLimits* luaLimits,std::string& error)
{
    try {
        if(!IsOpen()) throw std::runtime_error("Open a project before saving settings");
        if(extent && !ValidGameOutputExtent(*extent)) throw std::runtime_error("Game output requires dimensions 1..4096 and at most 8388608 pixels");
        if(luaLimits && !ValidLuaLimits(*luaLimits))throw std::runtime_error("Lua limits require memoryBytes 1..1073741824, states 1..4096, frameInstructions 1..100000000");
        auto normalizedScene=startupScene ? NormalizeManifestPath(*startupScene,"startupScene",true) : settings_.startupScene;
        const auto size=std::filesystem::file_size(projectFile_);
        if(size>kMaxManifestBytes) throw std::runtime_error("Project manifest exceeds the 1 MiB limit");
        std::ifstream input(projectFile_,std::ios::binary);
        std::string current(static_cast<size_t>(size),'\0');
        if(!input || !input.read(current.data(),static_cast<std::streamsize>(current.size())) || input.peek()!=EOF)
            throw std::runtime_error("Cannot read project manifest before saving");
        input.close();
        if(current!=manifestBytes_) throw std::runtime_error("Project manifest changed externally; reload settings before saving");
        const bool bom=current.starts_with("\xEF\xBB\xBF");
        if(bom) current.erase(0,3);
        auto parsed=crude_json::value::parse(current);
        // Preserve extension fields within the existing gameOutput object too.
        if(extent){
            if(!parsed.contains("gameOutput")) parsed["gameOutput"]=crude_json::object{};
            parsed["gameOutput"]["width"]=double(extent->width);
            parsed["gameOutput"]["height"]=double(extent->height);
        }
        if(startupScene) parsed["startupScene"]=normalizedScene;
        if(luaLimits){
            if(!parsed.contains("luaLimits"))parsed["luaLimits"]=crude_json::object{};
            parsed["luaLimits"]["memoryBytes"]=double(luaLimits->memoryBytes);
            parsed["luaLimits"]["states"]=double(luaLimits->states);
            parsed["luaLimits"]["frameInstructions"]=double(luaLimits->frameInstructions);
        }
        auto replacement=(bom ? std::string("\xEF\xBB\xBF") : std::string{})+parsed.dump(2);
        if(!AtomicFile::Write(projectFile_,replacement,kMaxManifestBytes,error)) return false;
        manifestBytes_=std::move(replacement);
        if(extent)settings_.gameOutput=*extent;
        if(luaLimits)settings_.luaLimits=*luaLimits;
        settings_.startupScene=std::move(normalizedScene);
        error.clear();return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

bool ProjectContext::Create(const std::filesystem::path& projectRoot, const std::string& name,
    std::string& error)
{
    try {
        ValidateProjectName(name);
        const auto root = NormalizeAbsolute(projectRoot);
        const auto manifest = root / PathFromUtf8(name + ".kubo");
        if (std::filesystem::exists(manifest)) throw std::runtime_error("Project already exists; open it instead");
        if (!FindManifest(root).empty()) throw std::runtime_error("This directory already contains a project; choose another directory");
        std::filesystem::create_directories(root / "Assets" / "Scenes");
        std::filesystem::create_directories(root / "ProjectSettings");
        std::filesystem::create_directories(root / "UserSettings");
        std::filesystem::create_directories(root / "Library");
        std::filesystem::create_directories(root / "Build");

        Settings settings{};
        settings.name = name;
        const auto startupPath = root / settings.startupScene;
        WriteNewProjectFile(startupPath,"{\"version\":1,\"entities\":[]}",true);
        const crude_json::object object{
            {"name", settings.name},
            {"engineVersion", settings.engineVersion},
            {"startupScene", settings.startupScene},
            {"assetDirectory", settings.assetDirectory},
            {"gameOutput",crude_json::object{{"width",double(settings.gameOutput.width)},{"height",double(settings.gameOutput.height)}}},
        };
        WriteNewProjectFile(manifest,crude_json::value(object).dump(2));
        return LoadManifest(manifest, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::filesystem::path ProjectContext::ResolvePath(const std::filesystem::path& path) const
{
    if (path.empty() || path.is_absolute() || rootPath_.empty()) {
        return path;
    }
    return (rootPath_ / NormalizeProjectRelative(path)).lexically_normal();
}

std::filesystem::path ProjectContext::ResolveAssetPath(const std::filesystem::path& path) const
{
    if (path.empty() || path.is_absolute() || rootPath_.empty()) {
        return path;
    }
    const auto relative=NormalizeProjectRelative(path);
    const auto assetDirectory=PathFromUtf8(settings_.assetDirectory);
    // Match complete components: custom nested roots must not be doubled.
    if(HasPathPrefix(relative,L"Resources") || HasPathPrefix(relative,assetDirectory)) return ResolvePath(relative);
    return (rootPath_ / assetDirectory / relative).lexically_normal();
}

std::filesystem::path ProjectContext::FindManifest(const std::filesystem::path& path)
{
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error) && _wcsicmp(path.extension().c_str(),L".kubo")==0) {
        return path;
    }
    if (!std::filesystem::is_directory(path, error)) {
        return {};
    }
    std::filesystem::path manifest;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (!error && entry.is_regular_file(error) && _wcsicmp(entry.path().extension().c_str(),L".kubo")==0) {
            if(!manifest.empty()) throw std::runtime_error("Multiple project manifests found; select a specific .kubo file");
            manifest=entry.path();
        }
    }
    return manifest;
}

std::filesystem::path ProjectContext::FindManifestUpwards(const std::filesystem::path& start)
{
    std::error_code error;
    auto current = NormalizeAbsolute(start);
    if (std::filesystem::is_regular_file(current, error)) {
        current = current.parent_path();
    }
    while (!current.empty()) {
        if (const auto manifest = FindManifest(current); !manifest.empty()) {
            return manifest;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

} // namespace Engine::Base
