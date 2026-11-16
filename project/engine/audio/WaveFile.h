#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmreg.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>

namespace Engine::AudioSystem {
struct SoundData {
    WAVEFORMATEX wfex{};
    std::vector<BYTE> buffer;
    unsigned int bufferSize=0;
};

// Bounded PCM/IEEE-float RIFF decoding, independent of the audio device.
class WaveFile final {
public:
    static constexpr size_t kMaxBytes=32*1024*1024;
    static bool Load(const std::filesystem::path& path,SoundData& output,std::string& error) {
        try {
            if(path.empty() || path.native().find(L'\0')!=std::wstring::npos) throw std::invalid_argument("Invalid Wave path");
            const auto size=std::filesystem::file_size(path);
            if(size<12 || size>kMaxBytes) throw std::runtime_error("Wave file must be between 12 bytes and 32 MiB");
            std::ifstream file(path,std::ios::binary);std::vector<BYTE> bytes(static_cast<size_t>(size));
            if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)) || file.peek()!=EOF)
                throw std::runtime_error("Cannot read complete Wave file");
            const auto u16=[&](size_t at) {return static_cast<uint16_t>(bytes[at] | (static_cast<uint16_t>(bytes[at+1])<<8));};
            const auto u32=[&](size_t at) {return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at+1])<<8) | (static_cast<uint32_t>(bytes[at+2])<<16) | (static_cast<uint32_t>(bytes[at+3])<<24);};
            const auto tag=[&](size_t at,const char* expected) {return bytes[at]==expected[0] && bytes[at+1]==expected[1] && bytes[at+2]==expected[2] && bytes[at+3]==expected[3];};
            if(!tag(0,"RIFF") || !tag(8,"WAVE") || u32(4)!=bytes.size()-8) throw std::runtime_error("Invalid RIFF/WAVE extent");
            SoundData candidate;bool formatFound=false,dataFound=false;
            for(size_t at=12;at<bytes.size();) {
                if(bytes.size()-at<8) throw std::runtime_error("Truncated Wave chunk header");
                const auto length=static_cast<size_t>(u32(at+4));const auto body=at+8;
                if(length>bytes.size()-body || (length&1)>bytes.size()-body-length) throw std::runtime_error("Wave chunk exceeds RIFF extent");
                if(tag(at,"fmt ")) {
                    if(formatFound || (length!=16 && length!=18)) throw std::runtime_error("Unsupported or duplicate Wave format chunk");
                    auto& f=candidate.wfex;f.wFormatTag=u16(body);f.nChannels=u16(body+2);f.nSamplesPerSec=u32(body+4);
                    f.nAvgBytesPerSec=u32(body+8);f.nBlockAlign=u16(body+12);f.wBitsPerSample=u16(body+14);
                    if(length==18 && u16(body+16)!=0) throw std::runtime_error("Extended Wave formats are not supported");
                    if((f.wFormatTag!=WAVE_FORMAT_PCM && f.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT) || f.nChannels==0 || f.nChannels>8 || f.nSamplesPerSec==0 || f.nSamplesPerSec>192000)
                        throw std::runtime_error("Unsupported Wave encoding, channel count or sample rate");
                    if((f.wFormatTag==WAVE_FORMAT_PCM && f.wBitsPerSample!=8 && f.wBitsPerSample!=16 && f.wBitsPerSample!=24 && f.wBitsPerSample!=32) ||
                        (f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT && f.wBitsPerSample!=32)) throw std::runtime_error("Unsupported Wave sample width");
                    if(f.nBlockAlign!=f.nChannels*(f.wBitsPerSample/8) || f.nAvgBytesPerSec!=f.nSamplesPerSec*f.nBlockAlign)
                        throw std::runtime_error("Inconsistent Wave block alignment or byte rate");
                    formatFound=true;
                } else if(tag(at,"data")) {
                    if(dataFound || length==0) throw std::runtime_error("Empty or duplicate Wave data chunk");
                    candidate.buffer.assign(bytes.begin()+body,bytes.begin()+body+length);candidate.bufferSize=static_cast<unsigned int>(length);dataFound=true;
                }
                at=body+length+(length&1);
            }
            if(!formatFound || !dataFound || candidate.bufferSize%candidate.wfex.nBlockAlign!=0)
                throw std::runtime_error("Missing Wave chunks or incomplete sample frame");
            output=std::move(candidate);error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }
};
}
