#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <array>

namespace Engine::Assets {
struct GlbDependencyContent {std::string json;uint32_t binLength=0;std::array<unsigned char,3> binTail{};bool hasBin=false;};
// glTF 2.0 section 4: stream past binary/unknown payloads, allocate only JSON.
inline GlbDependencyContent ReadGlbDependencyContent(const std::filesystem::path& path,
    const std::function<bool()>& cancelled={}) {
    const auto check=[&] {if(cancelled && cancelled()) throw std::runtime_error("GLB dependency inspection cancelled");};
    check();const auto size=std::filesystem::file_size(path);
    if(size<20 || size>512ull*1024*1024) throw std::runtime_error("GLB container exceeds 20-byte/512 MiB bounds");
    std::ifstream input(path,std::ios::binary);
    const auto word=[&]() {
        unsigned char bytes[4]{};
        if(!input.read(reinterpret_cast<char*>(bytes),4)) throw std::runtime_error("Truncated GLB header");
        return uint32_t(bytes[0]) | (uint32_t(bytes[1])<<8) | (uint32_t(bytes[2])<<16) | (uint32_t(bytes[3])<<24);
    };
    const auto magic=word(),version=word(),extent=word();
    if(magic!=0x46546c67 || version!=2 || extent!=size) throw std::runtime_error("Invalid GLB magic/version/extent");
    GlbDependencyContent content;uint64_t offset=12;size_t chunks=0;
    while(offset<size) {
        check();if(size-offset<8 || ++chunks>65536) throw std::runtime_error("Invalid GLB chunk header/count");
        const auto length=word(),type=word();offset+=8;
        if(length%4 || length>size-offset) throw std::runtime_error("Invalid GLB chunk alignment/extent");
        if(chunks==1 && type!=0x4e4f534a) throw std::runtime_error("GLB JSON must be the first chunk");
        if(type==0x4e4f534a) {
            if(chunks!=1 || !length || length>8*1024*1024) throw std::runtime_error("Invalid GLB JSON chunk/count/8 MiB bound");
            content.json.resize(length);
            if(!input.read(content.json.data(),length)) throw std::runtime_error("Truncated GLB JSON chunk");
        } else {
            if(type==0x004e4942) {
                if(chunks!=2 || content.hasBin) throw std::runtime_error("GLB BIN must occur once as second chunk");
                content.hasBin=true;content.binLength=length;
                if(length) {
                    input.seekg(static_cast<std::streamoff>(offset+length-3));
                    if(!input.read(reinterpret_cast<char*>(content.binTail.data()),3)) throw std::runtime_error("Truncated GLB BIN tail");
                }
            }
            // Unknown chunks are ignored as mandated by glTF 2.0.
            input.seekg(static_cast<std::streamoff>(offset+length));
            if(!input) throw std::runtime_error("Cannot skip GLB payload");
        }
        offset+=length;
    }
    // Seeking over BIN does not prove the final byte exists. Check the same stream.
    input.seekg(static_cast<std::streamoff>(size-1));
    if(input.get()==std::char_traits<char>::eof() || input.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("GLB changed or truncated during read");
    check();return content;
}
inline void AssertGlbZeroPadding(const GlbDependencyContent& content,uint32_t length) {
    if(!content.hasBin || length>content.binLength || content.binLength-length>3)
        throw std::runtime_error("GLB embedded buffer extent does not match BIN chunk");
    const auto padding=content.binLength-length;
    for(uint32_t index=3-padding;index<3;++index)
        if(content.binTail[index]!=0) throw std::runtime_error("Invalid GLB BIN zero padding");
}
}
