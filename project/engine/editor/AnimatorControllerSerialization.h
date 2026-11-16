#pragma once
#include "../animation/AnimatorControllerText.h"

namespace Engine::Editor {
// Editor-only typed authoring encoder. Runtime text/schema admission remains the
// authority; index references become names without silently dropping edges.
class AnimatorControllerSerialization final {
    using V=crude_json::value;
    using Graph=AnimationSystem::Controller::Graph;
    static void Require(bool condition){if(!condition)throw std::invalid_argument("Invalid Controller authoring counts/reference/kind");}
    static void Text(std::string_view value,size_t maximum=128){Require(Scripting::ValidScriptText(value,maximum,true));}
public:
    static bool Encode(const Graph& graph,std::string& output,std::string& error) {
        try {
            using namespace AnimationSystem::Controller;
            Require(graph.parameters.size()<=32 && !graph.layers.empty() && graph.layers.size()<=4);
            size_t states=0,transitions=0,nodes=4+4*graph.parameters.size();
            for(const auto& parameter:graph.parameters)Text(parameter.name);
            for(const auto& layer:graph.layers) {
                Require(!layer.states.empty() && layer.states.size()<=64 && layer.transitions.size()<=256 && layer.mask.size()<=256 && layer.entry<layer.states.size());
                states+=layer.states.size();transitions+=layer.transitions.size();
                Require(states<=128 && transitions<=512);
                Text(layer.name);Require(layer.mode=="override" || layer.mode=="additive");
                for(const auto& name:layer.mask)Text(name);
                for(const auto& state:layer.states){Text(state.name);Text(state.clip,256);}
                nodes+=8+layer.mask.size()+5*layer.states.size();
                Require(nodes<=TextCodec::MaxNodes);
                for(const auto& edge:layer.transitions) {
                    Require(edge.to<layer.states.size() && (!edge.from || *edge.from<layer.states.size()) && edge.conditions.size()<=8);
                    nodes+=7+4*edge.conditions.size();Require(nodes<=TextCodec::MaxNodes);
                    for(const auto& condition:edge.conditions){Require(condition.parameter<graph.parameters.size());Text(condition.op,4);}
                }
            }
            V root;root["format"]="kubo-animator-controller-1";root["parameters"]=crude_json::array{};root["layers"]=crude_json::array{};
            for(const auto& parameter:graph.parameters) {
                Require(parameter.kind==Kind::Float || parameter.kind==Kind::Bool || parameter.kind==Kind::Trigger);
                V p;p["name"]=parameter.name;p["type"]=parameter.kind==Kind::Float?"float":parameter.kind==Kind::Bool?"bool":"trigger";
                p["default"]=parameter.kind==Kind::Float?V(parameter.number):V(parameter.flag);root["parameters"].push_back(p);
            }
            for(const auto& layer:graph.layers) {
                V l;l["name"]=layer.name;l["mode"]=layer.mode;l["weight"]=double(layer.weight);l["mask"]=crude_json::array{};
                for(const auto& name:layer.mask)l["mask"].push_back(name);
                l["entry"]=layer.states[layer.entry].name;l["states"]=crude_json::array{};l["transitions"]=crude_json::array{};
                for(const auto& state:layer.states) {
                    V s;s["name"]=state.name;s["clip"]=state.clip;s["speed"]=double(state.speed);s["loop"]=state.loop;l["states"].push_back(s);
                }
                for(const auto& edge:layer.transitions) {
                    V t;t["from"]=edge.from?V(layer.states[*edge.from].name):V(crude_json::null{});
                    t["to"]=layer.states[edge.to].name;t["blendSeconds"]=double(edge.blendSeconds);
                    t["exitTime"]=edge.exitTime?V(double(*edge.exitTime)):V(crude_json::null{});
                    t["reenter"]=edge.reenter;t["conditions"]=crude_json::array{};
                    for(const auto& condition:edge.conditions) {
                        V c;c["parameter"]=graph.parameters[condition.parameter].name;c["op"]=condition.op;
                        c["value"]=graph.parameters[condition.parameter].kind==Kind::Float?V(condition.number):V(condition.flag);
                        t["conditions"].push_back(c);
                    }
                    l["transitions"].push_back(t);
                }
                root["layers"].push_back(l);
            }
            Graph checked;if(!Codec::Decode(root,checked,error))return false;
            auto text=root.dump();Graph decoded;if(!TextCodec::Decode(text,decoded,error))return false;
            output.swap(text);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
