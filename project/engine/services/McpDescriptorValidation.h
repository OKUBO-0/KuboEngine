#pragma once
// Bounded descriptor preflight; full JSON grammar/type validation follows this.
#include "../../externals/imgui_node_editor/crude_json.h"
#include <array>
#include <string>
#include <cstddef>
namespace Engine::Services::McpDescriptorValidation {
inline bool Preflight(const char* text, size_t size) {
    if (!text || size == 0 || size > 4096) return false;
    std::array<std::string, 64> keys;
    size_t count = 0, depth = 0, begin = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (quoted) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if (c != '"') continue;
            quoted = false;
            if (depth != 1) continue;
            size_t next = i + 1;
            while (next < size && (text[next] == ' ' || text[next] == '\t' || text[next] == '\r' || text[next] == '\n')) ++next;
            if (next == size || text[next] != ':') continue;
            if (count == keys.size()) return false;
            const auto key = crude_json::value::parse(std::string(text + begin, i - begin + 1));
            if (!key.is_string()) return false;
            const auto& decoded = key.get<crude_json::string>();
            for (size_t j = 0; j < count; ++j) if (keys[j] == decoded) return false;
            keys[count++] = decoded;
        } else if (c == '"') { begin = i; quoted = true; }
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    // Full grammar/type validation remains the existing bounded JSON parser's job.
    return !quoted && depth == 0;
}
}
