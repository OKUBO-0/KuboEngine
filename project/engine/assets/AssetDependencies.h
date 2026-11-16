#pragma once
#include "AssetRegistry.h"
#include "AssetGlb.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include "../base/AssetPath.h"
#include "externals/imgui_node_editor/crude_json.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <set>
#include <string_view>
#include <limits>

namespace Engine::Assets {
enum class AssetReferenceKind { OwnerRelative, ProjectPath, Guid };
struct AssetDependency {
    std::string ownerGuid, targetGuid, reference, targetRelative;
    bool missing = false;
    AssetReferenceKind kind = AssetReferenceKind::OwnerRelative;
};
struct AssetDependencyIssue { std::string ownerGuid, message; };
// Dependency admission only, not a renderer capability declaration. Specular
// textures use core texture -> image indices; IOR has no external references.
// The caller has already bounded UTF-8 JSON bytes/nesting/structure.
inline void InspectGltfMaterialExtensions(const crude_json::value& json,const std::function<void()>& check) {
    using Value=crude_json::value;
    std::set<std::string> used,required;
    const auto known=[](const std::string& name){return name=="KHR_materials_specular" || name=="KHR_materials_ior";};
    for(const auto* field:{"extensionsUsed","extensionsRequired"}) {
        if(!json.contains(field)) continue;
        if(!json[field].is_array()) throw std::runtime_error("Invalid glTF extension declaration");
        auto& names=std::string_view(field)=="extensionsUsed"?used:required;
        for(const auto& value:json[field].get<crude_json::array>()) {
            check();
            if(!value.is_string() || !known(value.get<crude_json::string>()))
                throw std::runtime_error("Unsupported glTF extension dependencies");
            if(!names.insert(value.get<crude_json::string>()).second)
                throw std::runtime_error("Duplicate glTF extension declaration");
        }
    }
    for(const auto& name:required) if(!used.contains(name))
        throw std::runtime_error("Required glTF extension not declared used");
    const auto number=[](const Value& v,double minimum,double maximum) {
        if(!v.is_number()) throw std::runtime_error("Invalid glTF material extension number");
        const auto n=v.get<crude_json::number>();
        if(!std::isfinite(n) || n<minimum || n>maximum) throw std::runtime_error("Invalid glTF material extension range");
        return n;
    };
    const auto index=[&](const Value& v,size_t count) {
        const auto n=number(v,0,static_cast<double>(count));
        if(std::floor(n)!=n || n>=static_cast<double>(count)) throw std::runtime_error("Invalid glTF extension texture/image index");
        return static_cast<size_t>(n);
    };
    const auto texture=[&](const Value& info) {
        if(!info.is_object() || !info.contains("index") || !json.contains("textures") || !json["textures"].is_array() ||
            !json.contains("images") || !json["images"].is_array()) throw std::runtime_error("Invalid glTF extension texture info");
        for(const auto& [key,value]:info.get<crude_json::object>()) {
            check();(void)value;
            if(key!="index" && key!="texCoord" && key!="extensions" && key!="extras")
                throw std::runtime_error("Uninspected glTF extension texture field");
        }
        if(info.contains("texCoord")) {
            const auto coordinate=number(info["texCoord"],0,2147483647);
            if(std::floor(coordinate)!=coordinate) throw std::runtime_error("Invalid glTF extension texCoord");
        }
        const auto& textures=json["textures"].get<crude_json::array>();
        const auto& item=textures[index(info["index"],textures.size())];
        if(!item.is_object() || !item.contains("source")) throw std::runtime_error("Invalid glTF extension texture source");
        const auto& images=json["images"].get<crude_json::array>();
        const auto& image=images[index(item["source"],images.size())];
        if(!image.is_object() || (!image.contains("uri") && !image.contains("bufferView")))
            throw std::runtime_error("Invalid glTF extension image source");
    };
    std::function<void(const Value&,bool,bool)> visit;
    visit=[&](const Value& node,bool root,bool material) {
        check();
        if(node.is_object()) {
            if(node.contains("extensions")) {
                if(!node["extensions"].is_object()) throw std::runtime_error("Invalid glTF extensions object");
                for(const auto& [name,payload]:node["extensions"].get<crude_json::object>()) {
                    check();
                    if(!material || !known(name) || !used.contains(name)) throw std::runtime_error("Unsupported/undeclared glTF extension payload location");
                    if(!payload.is_object()) throw std::runtime_error("Invalid glTF material extension payload");
                    for(const auto& [key,value]:payload.get<crude_json::object>()) {
                        check();
                        if(key=="extensions" || key=="extras") continue;
                        if(name=="KHR_materials_ior" && key=="ior") {
                            const auto n=number(value,0,(std::numeric_limits<double>::max)());
                            if(n!=0 && n<1) throw std::runtime_error("Invalid glTF material IOR");
                        } else if(name=="KHR_materials_specular" && key=="specularFactor") number(value,0,1);
                        else if(name=="KHR_materials_specular" && key=="specularColorFactor") {
                            if(!value.is_array() || value.get<crude_json::array>().size()!=3) throw std::runtime_error("Invalid glTF specular color");
                            for(const auto& channel:value.get<crude_json::array>()) number(channel,0,(std::numeric_limits<double>::max)());
                        } else if(name=="KHR_materials_specular" && (key=="specularTexture" || key=="specularColorTexture")) texture(value);
                        else throw std::runtime_error("Uninspected glTF material extension field");
                    }
                }
            }
            for(const auto& [key,value]:node.get<crude_json::object>()) {
                if(key=="extras") continue; // application metadata, not glTF references
                if(root && key=="materials") {
                    if(!value.is_array()) throw std::runtime_error("Invalid glTF materials collection");
                    for(const auto& entry:value.get<crude_json::array>()) {
                        if(!entry.is_object()) throw std::runtime_error("Invalid glTF material");
                        visit(entry,false,true);
                    }
                } else visit(value,false,false);
            }
        } else if(node.is_array()) for(const auto& entry:node.get<crude_json::array>()) visit(entry,false,false);
    };
    visit(json,true,false);
}
// A worker-owned snapshot of inspected relative file references, not an exhaustive
// importer graph. Unknown formats/extensions remain outside this parser's scope.
struct AssetDependencies {
    std::vector<AssetDependency> edges;
    std::vector<AssetDependencyIssue> issues;
    std::vector<const AssetDependency*> DependenciesOf(const std::string& guid) const {
        std::vector<const AssetDependency*> result;
        for (const auto& edge : edges) if (edge.ownerGuid == guid) result.push_back(&edge);
        return result;
    }
    std::vector<const AssetDependency*> DependentsOf(const std::string& guid) const {
        std::vector<const AssetDependency*> result;
        for (const auto& edge : edges) if (edge.targetGuid == guid) result.push_back(&edge);
        return result;
    }
    std::string MoveRisk(const AssetRegistry& registry, const std::string& guid, const std::string& destination) const {
        if (!issues.empty()) return "Dependency inspection is incomplete; fix the reported asset/Scene/Prefab diagnostics before moving assets";
        for(const auto& edge:edges)if(edge.targetGuid==guid && edge.kind!=AssetReferenceKind::Guid)
            return "Other inspected assets or Scenes reference this path; assign a GUID reference or rewrite the dependency before moving it";
        const auto* source = registry.FindByGuid(guid);
        if (source && Base::PathFromUtf8(source->relativePath).parent_path() != Base::PathFromUtf8(destination).parent_path() &&
            std::any_of(edges.begin(),edges.end(),[&](const auto& edge){return edge.ownerGuid==guid && edge.kind==AssetReferenceKind::OwnerRelative;})) return "This asset has relative dependencies; changing its folder requires dependency rewriting";
        return {};
    }
};
inline AssetDependencies InspectAssetDependencies(const AssetRegistry& registry, const std::function<bool()>& cancelled = {}) {
    AssetDependencies graph;
    const auto check = [&] { if (cancelled && cancelled()) throw std::runtime_error("Dependency inspection cancelled"); };
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value;
    };
    const auto tokens = [](const std::string& line) {
        std::vector<std::string> result;
        std::string token; bool quoted = false, present = false;
        for (unsigned char c : line) {
            if (c == '"') { quoted = !quoted; present = true; }
            else if (!quoted && c == '#') break;
            else if (!quoted && std::isspace(c)) { if (present) { result.push_back(token); token.clear(); present = false; } }
            else { token += static_cast<char>(c); present = true; }
        }
        if (quoted) throw std::runtime_error("Unclosed dependency filename quote");
        if (present) result.push_back(token);
        return result;
    };
    for (const auto& asset : registry.GetAssets()) {
        check();
        const auto path = Base::PathFromUtf8(asset.absolutePath);
        const auto extension = lower(Base::PathToUtf8(path.extension()));
        if (extension != ".obj" && extension != ".mtl" && extension != ".gltf" && extension != ".glb") continue;
        try {
            constexpr size_t maxBytes = 8 * 1024 * 1024;
            GlbDependencyContent glb;std::string bytes;
            if(extension==".glb") {glb=ReadGlbDependencyContent(path,cancelled);bytes=std::move(glb.json);}
            else {
                const auto size = std::filesystem::file_size(path);
                if (size > maxBytes) throw std::runtime_error("Dependency source exceeds 8 MiB inspection limit");
                std::ifstream input(path, std::ios::binary);bytes.resize(static_cast<size_t>(size));
                if (!input.read(bytes.data(), static_cast<std::streamsize>(size)) || input.peek() != std::char_traits<char>::eof())
                    throw std::runtime_error("Dependency source unreadable or changed during read");
                if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
            }
            const auto add = [&](const std::string& reference) {
                check();
                if (graph.edges.size() >= 65536) throw std::runtime_error("Dependency edge limit exceeded");
                const auto relative = Base::PathFromUtf8(reference);
                if (reference.empty() || reference.find('\0') != std::string::npos || relative.has_root_name() || relative.has_root_directory())
                    throw std::runtime_error("Dependency must be a non-empty relative file path");
                const auto target = (path.parent_path() / relative).lexically_normal();
                const auto inRoot = target.lexically_relative(registry.GetRoot());
                if (inRoot.empty() || *inRoot.begin() == "..") throw std::runtime_error("Dependency escapes the asset root");
                const auto* record = registry.FindByPath(target);
                const auto targetRelative = Base::PathToUtf8(inRoot);
                if (std::any_of(graph.edges.begin(), graph.edges.end(), [&](const auto& edge) {
                    return edge.ownerGuid == asset.guid && edge.targetRelative == targetRelative; })) return;
                graph.edges.push_back({asset.guid, record ? record->guid : std::string{}, reference, targetRelative, record == nullptr});
            };
            if (extension == ".gltf" || extension == ".glb") {
                if(bytes.find('\0')!=std::string::npos || (!bytes.empty() && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0)==0))
                    throw std::runtime_error("Invalid glTF dependency JSON UTF-8");
                // crude_json is recursive; bound JSON nesting/structure before
                // calling it so a malformed asset cannot overflow the worker stack.
                size_t depth = 0, structures = 0; bool inString = false, escaped = false;
                for (size_t i = 0; i < bytes.size(); ++i) {
                    if ((i & 4095) == 0) check();
                    const char c = bytes[i];
                    if (inString) {
                        if (escaped) escaped = false;
                        else if (c == '\\') escaped = true;
                        else if (c == '"') inString = false;
                    } else if (c == '"') inString = true;
                    else if (c == '{' || c == '[') {
                        if (++depth > 64 || ++structures > 65536) throw std::runtime_error("glTF dependency JSON structure limit exceeded");
                    } else if (c == '}' || c == ']') {
                        if (depth == 0) throw std::runtime_error("Invalid glTF JSON structure");
                        --depth;
                    }
                }
                const auto json = crude_json::value::parse(bytes);
                if (!json.is_object()) throw std::runtime_error("Invalid glTF JSON dependency source");
                if(extension==".glb") {
                    if(!json.contains("asset") || !json["asset"].is_object() || !json["asset"].contains("version") ||
                        !json["asset"]["version"].is_string() || json["asset"]["version"].get<crude_json::string>()!="2.0" ||
                        (json["asset"].contains("minVersion") && (!json["asset"]["minVersion"].is_string() || json["asset"]["minVersion"].get<crude_json::string>()!="2.0")))
                        throw std::runtime_error("Unsupported GLB JSON asset version");
                    if(json.contains("buffers") && json["buffers"].is_array()) {
                        const auto& buffers=json["buffers"].get<crude_json::array>();
                        for(size_t index=0;index<buffers.size();++index) if(buffers[index].is_object() && !buffers[index].contains("uri")) {
                            if(index!=0 || !buffers[index].contains("byteLength") || !buffers[index]["byteLength"].is_number())
                                throw std::runtime_error("Unsupported GLB embedded buffer declaration");
                            const auto length=buffers[index]["byteLength"].get<crude_json::number>();
                            if(!std::isfinite(length) || length<1 || length>512ull*1024*1024 || std::floor(length)!=length)
                                throw std::runtime_error("Invalid GLB embedded buffer length");
                            AssertGlbZeroPadding(glb,static_cast<uint32_t>(length));
                        }
                    }
                }
                InspectGltfMaterialExtensions(json,check);
                for (const auto* collection : {"buffers", "images"}) {
                    if (!json.contains(collection)) continue;
                    if (!json[collection].is_array()) throw std::runtime_error("Invalid glTF dependency collection");
                    for (const auto& item : json[collection].get<crude_json::array>()) {
                        check();
                        if (!item.is_object()) throw std::runtime_error("Invalid glTF dependency record");
                        if (!item.contains("uri")) continue; // Embedded image/buffer has no external reference.
                        if (!item["uri"].is_string()) throw std::runtime_error("Invalid glTF dependency URI");
                        auto uri = item["uri"].get<crude_json::string>();
                        if (uri.starts_with("data:")) continue;
                        if (uri.find_first_of(":?#\\") != std::string::npos) throw std::runtime_error("Unsupported glTF external URI");
                        std::string decoded;
                        const auto hex = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return -1;
                        };
                        for (size_t i = 0; i < uri.size(); ++i) {
                            if (uri[i] != '%') { decoded += uri[i]; continue; }
                            if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0) throw std::runtime_error("Invalid glTF URI escape");
                            decoded += static_cast<char>((hex(uri[i + 1]) << 4) | hex(uri[i + 2])); i += 2;
                        }
                        if (decoded.find_first_of(":?#\\") != std::string::npos) throw std::runtime_error("Unsupported decoded glTF URI");
                        add(decoded);
                    }
                }
            } else {
                std::istringstream lines(bytes); std::string line;
                while (std::getline(lines, line)) {
                    check();
                    std::istringstream commandLine(line); std::string command; commandLine >> command;
                    command = lower(command);
                    const bool obj = extension == ".obj" && command == "mtllib";
                    const bool mtl = extension == ".mtl" && (command.starts_with("map_") || command == "bump" || command == "disp" || command == "decal" || command == "refl" || command == "norm");
                    if (!obj && !mtl) continue;
                    const auto parts = tokens(line);
                    if (parts.size() < 2) throw std::runtime_error("Missing dependency filename");
                    if (mtl && (parts.size() != 2 || parts[1].starts_with('-'))) throw std::runtime_error("MTL options/unquoted multi-token filenames require importer inspection");
                    for (size_t i = 1; i < parts.size(); ++i) {
                        if (parts[i].ends_with('\\')) throw std::runtime_error("Dependency line continuation is not inspected yet");
                        add(parts[i]);
                    }
                }
            }
        } catch (const std::exception& error) {
            check();
            graph.issues.push_back({asset.guid, error.what()});
        }
    }
    check();
    return graph;
}
}
