#pragma once
// Private authored-data prototype. Not referenced by engine/SDK/test registry.
#include "engine/script/ScriptProperty.h"
#include "externals/imgui_node_editor/crude_json.h"
#include <vector>
#include <optional>
#include <stdexcept>
#include <algorithm>
namespace AnimatorControllerCandidate {
enum class Kind { Float, Bool, Trigger };
struct Parameter {std::string name;Kind kind=Kind::Float;double number=0;bool flag=false;};
struct State {std::string name,clip;float speed=1;bool loop=true;};
struct Condition {size_t parameter=0;std::string op;double number=0;bool flag=false;};
struct Transition {std::optional<size_t> from;size_t to=0;float blendSeconds=0;std::optional<float> exitTime;bool reenter=false;std::vector<Condition> conditions;};
struct Layer {std::string name,mode;float weight=1;size_t entry=0;std::vector<std::string> mask;std::vector<State> states;std::vector<Transition> transitions;};
struct Graph {std::vector<Parameter> parameters;std::vector<Layer> layers;void Swap(Graph& other) noexcept {parameters.swap(other.parameters);layers.swap(other.layers);}};
class Codec final {
    using V=crude_json::value;
    static void Check(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
    static void Fields(const V& v,std::initializer_list<const char*> keys) {
        Check(v.is_object() && v.get<crude_json::object>().size()==keys.size(),"Unexpected controller object fields");
        for(const auto key:keys)Check(v.contains(key),"Missing controller field");
    }
    static std::string Text(const V& v,size_t limit=128) {
        Check(v.is_string(),"Controller text must be a string");const auto& s=v.get<crude_json::string>();
        Check(Engine::Scripting::ValidScriptText(s,limit,true),"Invalid controller UTF-8/name bound");return s;
    }
    static double Number(const V& v,double low,double high) {
        Check(v.is_number(),"Controller number has wrong type");const auto n=v.get<crude_json::number>();
        Check(std::isfinite(n) && n>=low && n<=high,"Invalid controller finite/range bound");return n;
    }
    static bool Flag(const V& v){Check(v.is_boolean(),"Controller flag has wrong type");return v.get<crude_json::boolean>();}
    static const crude_json::array& Array(const V& v,size_t minimum,size_t maximum) {
        Check(v.is_array(),"Controller field must be an array");const auto& a=v.get<crude_json::array>();
        Check(a.size()>=minimum && a.size()<=maximum,"Controller array count bound");return a;
    }
    template<class Entries> static size_t Find(const Entries& entries,const std::string& name) {
        for(size_t i=0;i<entries.size();++i)if(entries[i].name==name)return i;
        throw std::invalid_argument("Controller reference does not exist");
    }
    template<class Entries> static void Unique(const Entries& entries,const std::string& name) {
        for(const auto& entry:entries)Check(entry.name!=name,"Duplicate controller name");
    }
public:
    // Takes an ALREADY-PARSED value. A future asset loader must separately bound
    // bytes/nesting, reject decoded duplicate JSON keys, and admit GUID/revision.
    static bool Decode(const V& value,Graph& output,std::string& error) {
        try {
            Fields(value,{"format","parameters","layers"});
            Check(Text(value["format"])=="kubo-controller-candidate-1","Unsupported controller candidate format");
            Graph candidate;
            const double finiteFloat=(std::numeric_limits<float>::max)();
            for(const auto& v:Array(value["parameters"],0,32)) {
                Fields(v,{"name","type","default"});Parameter p;p.name=Text(v["name"]);Unique(candidate.parameters,p.name);
                const auto type=Text(v["type"]);
                if(type=="float"){p.kind=Kind::Float;p.number=Number(v["default"],-finiteFloat,finiteFloat);}
                else {Check(type=="bool" || type=="trigger","Unknown controller parameter kind");p.kind=type=="bool" ? Kind::Bool : Kind::Trigger;p.flag=Flag(v["default"]);Check(p.kind!=Kind::Trigger || !p.flag,"Trigger default must be false");}
                candidate.parameters.push_back(std::move(p));
            }
            size_t totalStates=0,totalTransitions=0;
            for(const auto& v:Array(value["layers"],1,4)) {
                Fields(v,{"name","mode","weight","mask","entry","states","transitions"});Layer layer;
                layer.name=Text(v["name"]);Unique(candidate.layers,layer.name);layer.mode=Text(v["mode"]);
                Check(layer.mode=="override" || layer.mode=="additive","Unknown controller layer mode");layer.weight=static_cast<float>(Number(v["weight"],0,1));
                for(const auto& joint:Array(v["mask"],0,256)) {
                    auto name=Text(joint);Check(std::find(layer.mask.begin(),layer.mask.end(),name)==layer.mask.end(),"Duplicate controller mask joint");layer.mask.push_back(std::move(name));
                }
                for(const auto& s:Array(v["states"],1,64)) {
                    Fields(s,{"name","clip","speed","loop"});State state;state.name=Text(s["name"]);Unique(layer.states,state.name);
                    state.clip=Text(s["clip"],256);state.speed=static_cast<float>(Number(s["speed"],0,finiteFloat));state.loop=Flag(s["loop"]);layer.states.push_back(std::move(state));
                }
                totalStates+=layer.states.size();Check(totalStates<=128,"Controller total state bound");layer.entry=Find(layer.states,Text(v["entry"]));
                for(const auto& t:Array(v["transitions"],0,256)) {
                    Fields(t,{"from","to","blendSeconds","exitTime","reenter","conditions"});Transition transition;
                    if(!t["from"].is_null())transition.from=Find(layer.states,Text(t["from"]));
                    transition.to=Find(layer.states,Text(t["to"]));transition.blendSeconds=static_cast<float>(Number(t["blendSeconds"],0,60));
                    if(!t["exitTime"].is_null())transition.exitTime=static_cast<float>(Number(t["exitTime"],0,1));
                    transition.reenter=Flag(t["reenter"]);Check(!transition.from || *transition.from!=transition.to || transition.reenter,"Self transition requires explicit reentry");
                    for(const auto& c:Array(t["conditions"],0,8)) {
                        Fields(c,{"parameter","op","value"});Condition condition;condition.parameter=Find(candidate.parameters,Text(c["parameter"]));condition.op=Text(c["op"]);
                        const auto kind=candidate.parameters[condition.parameter].kind;
                        if(kind==Kind::Float){Check(condition.op=="eq" || condition.op=="ne" || condition.op=="lt" || condition.op=="le" || condition.op=="gt" || condition.op=="ge","Invalid float condition operator");condition.number=Number(c["value"],-finiteFloat,finiteFloat);}
                        else {condition.flag=Flag(c["value"]);if(kind==Kind::Trigger)Check(condition.op=="set" && condition.flag,"Trigger condition must test set/true");else Check(condition.op=="eq" || condition.op=="ne","Invalid bool condition operator");}
                        transition.conditions.push_back(std::move(condition));
                    }
                    layer.transitions.push_back(std::move(transition));
                }
                totalTransitions+=layer.transitions.size();Check(totalTransitions<=512,"Controller total transition bound");candidate.layers.push_back(std::move(layer));
            }
            output.Swap(candidate);error.clear();return true;
        } catch(const std::exception& e){error=e.what();return false;}
    }
};
}
