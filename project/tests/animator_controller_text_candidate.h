#pragma once
#include "animator_controller_candidate.h"
#include <set>
#include <string_view>
namespace AnimatorControllerCandidate {
// Private controller text gate, NOT a file/asset/GUID loader or adopted parser.
class TextCodec final {
    std::string_view text;size_t cursor=0,nodes=0;
    static void Need(bool ok){if(!ok)throw std::invalid_argument("Invalid/bounded/duplicate controller JSON");}
    char Peek() const noexcept {return cursor<text.size()?text[cursor]:'\0';}
    void Space(){while(Peek()==' ' || Peek()=='\t' || Peek()=='\r' || Peek()=='\n')++cursor;}
    bool Take(char c){if(Peek()!=c)return false;++cursor;return true;}
    std::string String() {
        const auto start=cursor;Need(Take('"'));bool closed=false;
        while(cursor<text.size()) {
            const char c=text[cursor++];
            if(c=='"'){closed=true;break;}
            if(c=='\\'){Need(cursor<text.size());++cursor;}
        }
        Need(closed);
        auto decoded=crude_json::value::parse(std::string(text.substr(start,cursor-start)));
        Need(decoded.is_string());auto value=decoded.get<crude_json::string>();
        Need(Engine::Scripting::ValidScriptText(value,MaxBytes));return value;
    }
    static bool Digit(char c){return c>='0' && c<='9';}
    void Number() {
        Take('-');
        if(!Take('0')) {Need(Peek()>='1' && Peek()<='9');while(Digit(Peek()))++cursor;}
        if(Take('.')){Need(Digit(Peek()));while(Digit(Peek()))++cursor;}
        if(Take('e') || Take('E')){if(!Take('+'))Take('-');Need(Digit(Peek()));while(Digit(Peek()))++cursor;}
    }
    void Value(size_t depth) {
        Need(depth<=MaxDepth && ++nodes<=MaxNodes);Space();
        if(Take('{')) {
            std::set<std::string> keys;Space();if(Take('}'))return;
            do {Space();auto key=String();Need(keys.insert(std::move(key)).second);Space();Need(Take(':'));Value(depth+1);Space();if(Take('}'))return;Need(Take(','));}while(true);
        }
        if(Take('[')) {
            Space();if(Take(']'))return;
            do {Value(depth+1);Space();if(Take(']'))return;Need(Take(','));}while(true);
        }
        if(Peek()=='"'){String();return;}
        for(const auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})
            if(text.substr(cursor,literal.size())==literal){cursor+=literal.size();return;}
        Number();
    }
    explicit TextCodec(std::string_view input):text(input){}
public:
    static constexpr size_t MaxBytes=256*1024,MaxDepth=32,MaxNodes=8192;
    static bool Decode(std::string_view input,Graph& output,std::string& error) {
        try {
            Need(!input.empty() && Engine::Scripting::ValidScriptText(input,MaxBytes));
            TextCodec gate(input);gate.Value(0);gate.Space();Need(gate.cursor==input.size());
            auto parsed=crude_json::value::parse(std::string(input));Need(!parsed.is_discarded());
            return Codec::Decode(parsed,output,error);
        }catch(const std::exception& e){error=e.what();return false;}
    }
};
}
