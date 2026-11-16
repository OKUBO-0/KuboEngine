#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "externals/imgui_node_editor/crude_json.h"
#include <set>
#include <string_view>
#include <stdexcept>

namespace Engine::Base {
// Strict, UTF-8/no-NUL JSON admission before the recursive DOM parser. Callers
// still own their schema and file/identity rules. No BOM normalization here.
class BoundedJson final {
public:
    struct Bounds {size_t bytes=1024*1024,depth=8,nodes=32768;};
private:
    std::string_view text_;Bounds bounds_;size_t cursor_=0,nodes_=0;
    static void Need(bool ok){if(!ok)throw std::invalid_argument("Invalid, duplicate or over-budget JSON metadata");}
    char Peek() const noexcept{return cursor_<text_.size()?text_[cursor_]:'\0';}
    bool Take(char c){if(Peek()!=c)return false;++cursor_;return true;}
    void Space(){while(Peek()==' ' || Peek()=='\t' || Peek()=='\r' || Peek()=='\n')++cursor_;}
    static bool Digit(char c){return c>='0' && c<='9';}
    unsigned Hex4(){unsigned value=0;for(int i=0;i<4;++i){Need(cursor_<text_.size());const char h=text_[cursor_++];Need(Digit(h) || (h>='a' && h<='f') || (h>='A' && h<='F'));value=value*16+(Digit(h)?h-'0':h>='a'?h-'a'+10:h-'A'+10);}return value;}
    static bool Utf8(std::string_view text){return text.find('\0')==text.npos && (text.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)>0);}
    std::string String(){
        const auto start=cursor_;Need(Take('"'));bool closed=false;
        while(cursor_<text_.size()){
            const auto c=static_cast<unsigned char>(text_[cursor_++]);Need(c>=32);
            if(c=='"'){closed=true;break;}
            if(c=='\\'){
                Need(cursor_<text_.size());const char e=text_[cursor_++];
                Need(e=='"' || e=='\\' || e=='/' || e=='b' || e=='f' || e=='n' || e=='r' || e=='t' || e=='u');
                if(e=='u'){const auto unit=Hex4();if(unit>=0xd800 && unit<=0xdbff){Need(Take('\\') && Take('u'));const auto low=Hex4();Need(low>=0xdc00 && low<=0xdfff);}else Need(unit<0xdc00 || unit>0xdfff);}
            }
        }
        Need(closed);auto value=crude_json::value::parse(std::string(text_.substr(start,cursor_-start)));Need(value.is_string());
        auto decoded=value.get<crude_json::string>();Need(Utf8(decoded));return decoded;
    }
    void Number(){
        Take('-');if(!Take('0')){Need(Peek()>='1' && Peek()<='9');while(Digit(Peek()))++cursor_;}
        if(Take('.')){Need(Digit(Peek()));while(Digit(Peek()))++cursor_;}
        if(Take('e') || Take('E')){if(!Take('+'))Take('-');Need(Digit(Peek()));while(Digit(Peek()))++cursor_;}
    }
    void Value(size_t depth){
        Need(depth<=bounds_.depth && ++nodes_<=bounds_.nodes);Space();
        if(Take('{')){std::set<std::string> keys;Space();if(Take('}'))return;
            do{Space();auto key=String();Need(keys.insert(std::move(key)).second);Space();Need(Take(':'));Value(depth+1);Space();if(Take('}'))return;Need(Take(','));}while(true);}
        if(Take('[')){Space();if(Take(']'))return;do{Value(depth+1);Space();if(Take(']'))return;Need(Take(','));}while(true);}
        if(Peek()=='"'){String();return;}
        for(const auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(text_.substr(cursor_,literal.size())==literal){cursor_+=literal.size();return;}
        Number();
    }
    BoundedJson(std::string_view text,Bounds bounds):text_(text),bounds_(bounds){}
public:
    static bool Validate(std::string_view text,Bounds bounds,std::string& error){
        try{
            Need(bounds.bytes>0 && bounds.bytes<=4*1024*1024 && bounds.depth>0 && bounds.depth<=64 && bounds.nodes>0 && bounds.nodes<=1024*1024);
            Need(!text.empty() && text.size()<=bounds.bytes && Utf8(text));
            BoundedJson gate(text,bounds);gate.Value(0);gate.Space();Need(gate.cursor_==text.size());error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
};
}
