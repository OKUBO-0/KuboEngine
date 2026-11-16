#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace KuboHub::PackageDetail {
struct Cancelled final : std::exception {
    const char* what() const noexcept override {return "Package verification cancelled";}
};
inline void CheckCancel(const std::atomic_bool* cancel) {
    if(cancel && cancel->load(std::memory_order_relaxed))throw Cancelled{};
}
// Hash an already-held ordinary file, never reopen a path. Cooperative cancellation
// is checked between reads; it is not a hard deadline for Windows I/O.
inline std::string Digest(HANDLE file,uint64_t maximum,const std::atomic_bool* cancel,
                          std::atomic<uint64_t>* progress=nullptr) {
    CheckCancel(cancel);
    struct State {
        std::vector<unsigned char> object;
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        ~State(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    } state;
    if(BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)
        throw std::runtime_error("Cannot open SHA-256 provider");
    DWORD length=0,returned=0;
    if(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&returned,0)!=0)
        throw std::runtime_error("Cannot query SHA-256 buffer size");
    state.object.resize(length);
    if(BCryptCreateHash(state.algorithm,&state.hash,state.object.data(),length,nullptr,0,0)!=0)
        throw std::runtime_error("Cannot create SHA-256 hash");
    LARGE_INTEGER beginning{};
    if(!SetFilePointerEx(file,beginning,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot seek package file");
    std::array<unsigned char,64*1024> buffer{};uint64_t total=0;
    for(;;){
        CheckCancel(cancel);DWORD read=0;
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))throw std::runtime_error("Cannot read package file");
        if(!read)break;
        if(read>maximum-total)throw std::runtime_error("Package file exceeds byte limit");
        total+=read;
        if(BCryptHashData(state.hash,buffer.data(),read,0)!=0)throw std::runtime_error("Cannot hash package file");
        if(progress)progress->fetch_add(read,std::memory_order_relaxed);
    }
    CheckCancel(cancel);std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(state.hash,digest.data(),static_cast<ULONG>(digest.size()),0)!=0)throw std::runtime_error("Cannot finish SHA-256 hash");
    std::string result(64,'0');constexpr char digits[]="0123456789abcdef";
    for(size_t i=0;i<digest.size();++i){result[2*i]=digits[digest[i]>>4];result[2*i+1]=digits[digest[i]&15];}
    return result;
}
}
