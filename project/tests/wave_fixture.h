#pragma once
#include <string>
#include <cstdint>
namespace WaveFixture {
inline void Put16(std::string& bytes,uint16_t value) {bytes+=static_cast<char>(value);bytes+=static_cast<char>(value>>8);}
inline void Put32(std::string& bytes,uint32_t value) {for(unsigned shift=0;shift<32;shift+=8) bytes+=static_cast<char>(value>>shift);}
inline void Set32(std::string& bytes,size_t at,uint32_t value) {for(unsigned index=0;index<4;++index) bytes.at(at+index)=static_cast<char>(value>>(index*8));}
inline std::string Wrap(const std::string& chunks) {std::string bytes="RIFF";Put32(bytes,static_cast<uint32_t>(chunks.size()+4));return bytes+"WAVE"+chunks;}
inline std::string Format(uint16_t tag=1,uint16_t bits=16) {
    std::string bytes="fmt ";Put32(bytes,16);Put16(bytes,tag);Put16(bytes,1);Put32(bytes,8000);Put32(bytes,8000*(bits/8));Put16(bytes,bits/8);Put16(bytes,bits);return bytes;
}
inline std::string Data(uint16_t first=0) {std::string bytes="data";Put32(bytes,4);Put16(bytes,first);Put16(bytes,0);return bytes;}
inline std::string Pcm(uint16_t first=0) {return Wrap(Format()+Data(first));}
}
