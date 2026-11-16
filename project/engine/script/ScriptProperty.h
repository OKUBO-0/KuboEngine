#pragma once
#include <map>
#include <variant>
#include <string>
#include <string_view>
#include <cmath>
#include <limits>
namespace Engine::Scripting {
using ScriptProperty = std::variant<double,bool,std::string>;
using ScriptProperties = std::map<std::string,ScriptProperty,std::less<>>;
inline bool ValidScriptText(std::string_view text,size_t limit,bool name=false) {
    if(text.size()>limit || (name && text.empty())) return false;
    for(size_t index=0;index<text.size();) {
        const auto first=static_cast<unsigned char>(text[index++]);
        if(first==0) return false;
        if(first<128) continue;
        unsigned count=0,code=0,minimum=0;
        if(first>=0xc2 && first<=0xdf) {count=1;code=first&31;minimum=128;}
        else if(first>=0xe0 && first<=0xef) {count=2;code=first&15;minimum=2048;}
        else if(first>=0xf0 && first<=0xf4) {count=3;code=first&7;minimum=65536;}
        else return false;
        if(index+count>text.size()) return false;
        while(count--) {const auto next=static_cast<unsigned char>(text[index++]);if((next&0xc0)!=0x80) return false;code=(code<<6)|(next&63);}
        if(code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    }
    return true;
}
inline bool ValidScriptProperty(const ScriptProperty& value) {
    if(const auto* number=std::get_if<double>(&value)) return std::isfinite(*number) && std::abs(*number)<=(std::numeric_limits<float>::max)();
    if(const auto* text=std::get_if<std::string>(&value)) return ValidScriptText(*text,1024);
    return true;
}
}
