#include "ActionMap.h"
#include "engine/base/AtomicFile.h"
#include "crude_json.h"
#include <Windows.h>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <algorithm>

namespace Engine::InputSystem {

void ActionMap::Bind(const std::string& action, int key, const Vector3& contribution)
{
	BindSource(action,Binding{key,contribution});
}

void ActionMap::BindSource(const std::string& action, Binding value)
{
	auto& bindings = actions_[action];
	for (Binding& binding : bindings) {
		if (binding.key == value.key && binding.source == value.source) {
			binding = value;
			return;
		}
	}

	bindings.push_back(value);
}

void ActionMap::BindPadButton(const std::string& action,GamepadButton button,const Vector3& contribution) {
    BindSource(action,Binding{static_cast<int>(button),contribution,Source::PadButton});
}
void ActionMap::BindPadAxis(const std::string& action,PadAxis axis,const Vector3& contribution,float threshold) {
    BindSource(action,Binding{static_cast<int>(axis),contribution,Source::PadAxis,threshold});
}
float ActionMap::Value(const Binding& binding,const KeyDownFn& down,const GamepadFrame& pad) {
    if(binding.source==Source::Keyboard)return binding.key>=0 && binding.key<256 && down(binding.key) ? 1.0f : 0.0f;
    if(!pad.connected)return 0;
    if(binding.source==Source::PadButton)return binding.key>=0 && binding.key<static_cast<int>(GamepadButton::Count) && pad.buttons[static_cast<size_t>(binding.key)].down ? 1.0f : 0.0f;
    if(binding.source!=Source::PadAxis || binding.key<0 || binding.key>=static_cast<int>(PadAxis::Count))return 0;
    const float values[]={pad.leftX,pad.leftY,pad.rightX,pad.rightY,pad.leftTrigger,pad.rightTrigger};
    const float value=values[binding.key];
    return std::isfinite(value) ? (std::clamp)(value,binding.key>=static_cast<int>(PadAxis::LeftTrigger) ? 0.0f : -1.0f,1.0f) : 0.0f;
}
bool ActionMap::IsActive(const Binding& binding,float value) noexcept {
    return binding.source==Source::PadAxis ? std::isfinite(binding.activationThreshold) && binding.activationThreshold>0 && binding.activationThreshold<=1 && std::abs(value)>=binding.activationThreshold : value!=0;
}

void ActionMap::Bind(const std::string& action, int key)
{
	Bind(action, key, Vector3{ 1.0f, 0.0f, 0.0f });
}

void ActionMap::ClearAction(const std::string& action)
{
	actions_.erase(action);
}

void ActionMap::Clear()
{
	actions_.clear();
}

bool ActionMap::HasAction(const std::string& action) const
{
	const auto it = actions_.find(action);
	return it != actions_.end() && !it->second.empty();
}

bool ActionMap::Active(const std::string& action, const KeyDownFn& down) const
{
    return Active(action,down,{});
}
bool ActionMap::Active(const std::string& action, const KeyDownFn& down,const GamepadFrame& pad) const
{
	const auto it = actions_.find(action);
	if (it == actions_.end()) {
		return false;
	}

	for (const Binding& binding : it->second) {
		if (IsActive(binding,Value(binding,down,pad))) {
			return true;
		}
	}

	return false;
}

Vector3 ActionMap::Evaluate(const std::string& action, const KeyDownFn& down) const
{
    return Evaluate(action,down,{});
}
Vector3 ActionMap::Evaluate(const std::string& action, const KeyDownFn& down,const GamepadFrame& pad) const
{
	Vector3 result{};
	const auto it = actions_.find(action);
	if (it == actions_.end()) {
		return result;
	}

	for (const Binding& binding : it->second) {
		const float value=Value(binding,down,pad);
		if(value==0)continue;
		result.x+=binding.contribution.x*value;result.y+=binding.contribution.y*value;result.z+=binding.contribution.z*value;
	}

	return result;
}

std::size_t ActionMap::BindingCount(const std::string& action) const
{
	const auto it = actions_.find(action);
	return it == actions_.end() ? 0 : it->second.size();
}

bool ActionMap::Save(const std::filesystem::path& path, std::string& error) const
{
    try {
        if(actions_.size()>256) throw std::runtime_error("Too many input actions");
        crude_json::value root;
        bool extended=false;
        for(const auto& [name,bindings]:actions_){(void)name;for(const auto& binding:bindings)if(binding.source!=Source::Keyboard)extended=true;}
        root["version"] = extended ? 2.0 : 1.0;
        root["actions"] = crude_json::object{};
        for (const auto& [action, bindings] : actions_) {
            if(action.empty() || action.size()>256 || bindings.size()>256) throw std::runtime_error("Invalid input action name or binding count");
            root["actions"][action] = crude_json::array{};
            for (const auto& binding : bindings) {
                const int maximum=binding.source==Source::Keyboard ? 255 : binding.source==Source::PadButton ? static_cast<int>(GamepadButton::Count)-1 : static_cast<int>(PadAxis::Count)-1;
                if((binding.source!=Source::Keyboard && binding.source!=Source::PadButton && binding.source!=Source::PadAxis) || binding.key<0 || binding.key>maximum || !std::isfinite(binding.contribution.x) || !std::isfinite(binding.contribution.y) || !std::isfinite(binding.contribution.z) || (binding.source==Source::PadAxis && (!std::isfinite(binding.activationThreshold) || binding.activationThreshold<=0 || binding.activationThreshold>1)))
                    throw std::runtime_error("Input binding requires a valid scan code and finite contribution");
                crude_json::value item;
                if(extended) {
                    item["source"]=binding.source==Source::Keyboard ? "keyboard" : binding.source==Source::PadButton ? "pad_button" : "pad_axis";
                    item["code"]=static_cast<double>(binding.key);
                    if(binding.source==Source::PadAxis)item["threshold"]=static_cast<double>(binding.activationThreshold);
                }else item["key"] = static_cast<double>(binding.key);
                item["contribution"] = crude_json::array{
                    static_cast<double>(binding.contribution.x), static_cast<double>(binding.contribution.y),
                    static_cast<double>(binding.contribution.z)};
                root["actions"][action].push_back(std::move(item));
            }
        }
        const auto text = root.dump(2);
        return Engine::Base::AtomicFile::Write(path,text,1024*1024,error);
    } catch (const std::exception& e) {
        error = e.what(); return false;
    }
}

bool ActionMap::Load(const std::filesystem::path& path, std::string& error)
{
    try {
        if (std::filesystem::file_size(path) > 1024 * 1024) throw std::runtime_error("Input binding file exceeds 1 MiB");
        std::ifstream in(path, std::ios::binary);
        if (!in) { throw std::runtime_error("Cannot open input binding file"); }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto root = crude_json::value::parse(text);
        if (!root.is_object() || !root.contains("version") || !root["version"].is_number() || (root["version"].get<crude_json::number>() != 1.0 && root["version"].get<crude_json::number>() != 2.0) ||
            !root.contains("actions") || !root["actions"].is_object()) { throw std::runtime_error("Unsupported input binding format"); }
        ActionMap loaded;
        const bool extended=root["version"].get<crude_json::number>()==2.0;
        if(root["actions"].get<crude_json::object>().size()>256) throw std::runtime_error("Too many input actions");
        for (const auto& [action, values] : root["actions"].get<crude_json::object>()) {
            if(action.empty() || action.size()>256) throw std::runtime_error("Invalid input action name");
            if (!values.is_array()) { throw std::runtime_error("Action bindings must be arrays"); }
            if(values.get<crude_json::array>().size()>256) throw std::runtime_error("Too many bindings for input action");
            for (const auto& item : values.get<crude_json::array>()) {
                const char* codeName=extended ? "code" : "key";
                if (!item.is_object() || !item.contains(codeName) || !item.contains("contribution") ||
                    !item[codeName].is_number() || !item["contribution"].is_array() || item["contribution"].get<crude_json::array>().size() != 3 || (extended ? item.contains("key") : item.contains("source") || item.contains("code") || item.contains("threshold"))) {
                    throw std::runtime_error("Malformed action binding");
                }
                const auto& c = item["contribution"].get<crude_json::array>();
                Source source=Source::Keyboard;float threshold=0.5f;
                if(extended){
                    if(!item.contains("source") || !item["source"].is_string())throw std::runtime_error("Input binding source is required");
                    const auto& name=item["source"].get<crude_json::string>();
                    if(name=="pad_button")source=Source::PadButton;
                    else if(name=="pad_axis")source=Source::PadAxis;
                    else if(name!="keyboard")throw std::runtime_error("Unknown input binding source");
                    if(source==Source::PadAxis){
                        if(!item.contains("threshold") || !item["threshold"].is_number())throw std::runtime_error("Pad axis requires an activation threshold");
                        const double value=item["threshold"].get<crude_json::number>();
                        if(!std::isfinite(value) || value<=0 || value>1 || static_cast<float>(value)<=0)throw std::runtime_error("Pad activation threshold must be in (0,1]");
                        threshold=static_cast<float>(value);
                    }else if(item.contains("threshold"))throw std::runtime_error("Digital bindings do not use thresholds");
                }
                const auto code=item[codeName].get<crude_json::number>();
                const int maximum=source==Source::Keyboard ? 255 : source==Source::PadButton ? static_cast<int>(GamepadButton::Count)-1 : static_cast<int>(PadAxis::Count)-1;
                if(!std::isfinite(code) || code<0 || code>maximum || std::floor(code)!=code) throw std::runtime_error("Input binding code is out of range");
                Vector3 contribution;
                float* components[]={&contribution.x,&contribution.y,&contribution.z};
                for(size_t index=0;index<3;++index) {
                    if(!c[index].is_number()) throw std::runtime_error("Input contribution must contain numbers");
                    const auto value=c[index].get<crude_json::number>();
                    if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)()) throw std::runtime_error("Input contribution must contain finite floats");
                    *components[index]=static_cast<float>(value);
                }
                loaded.BindSource(action,Binding{static_cast<int>(code),contribution,source,threshold});
            }
        }
        actions_ = std::move(loaded.actions_); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

}
