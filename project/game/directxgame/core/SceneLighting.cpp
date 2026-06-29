#include "SceneLighting.h"
#include "Object3DCommon.h"
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

std::string Key(std::string_view prefix, std::string_view name)
{
	std::string key(prefix);
	key.append(name);
	return key;
}

}

namespace DirectXGame::SceneLighting {

void ApplyDefaults(const Defaults& defaults)
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	objectCommon->SetSceneLight(defaults.light);
	objectCommon->SetShadowEnabled(defaults.shadowEnabled);
	objectCommon->SetShadowStrength(defaults.shadowStrength);
	objectCommon->SetShadowSoftness(defaults.shadowSoftness);
	objectCommon->SetShadowBias(defaults.shadowBias);
	objectCommon->SetShadowArea(defaults.shadowArea);
}

void Load(const UILayoutIO::LayoutMap& tuning, std::string_view keyPrefix)
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	SceneLightData sceneLight = objectCommon->GetSceneLight();
	const Vector3 lightColor = UILayoutIO::GetVector3(
		tuning,
		Key(keyPrefix, "sun.color"),
		{ sceneLight.color.x, sceneLight.color.y, sceneLight.color.z });
	sceneLight.color = { lightColor.x, lightColor.y, lightColor.z, 1.0f };
	sceneLight.direction = UILayoutIO::GetVector3(
		tuning, Key(keyPrefix, "sun.direction"), sceneLight.direction);
	sceneLight.intensity = UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "sun.intensity"), sceneLight.intensity);
	const Vector3 ambientColor = UILayoutIO::GetVector3(
		tuning,
		Key(keyPrefix, "sun.ambientColor"),
		{ sceneLight.ambientColor.x, sceneLight.ambientColor.y, sceneLight.ambientColor.z });
	sceneLight.ambientColor = { ambientColor.x, ambientColor.y, ambientColor.z, 1.0f };
	sceneLight.ambientIntensity = UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "sun.ambientIntensity"), sceneLight.ambientIntensity);
	sceneLight.specularStrength = UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "sun.specularStrength"), sceneLight.specularStrength);
	sceneLight.enable = UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "sun.enabled"), sceneLight.enable ? 1.0f : 0.0f) > 0.5f;
	objectCommon->SetSceneLight(sceneLight);

	objectCommon->SetShadowEnabled(UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "shadow.enabled"), objectCommon->IsShadowEnabled() ? 1.0f : 0.0f) > 0.5f);
	objectCommon->SetShadowStrength(UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "shadow.strength"), objectCommon->GetShadowStrength()));
	objectCommon->SetShadowSoftness(UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "shadow.softness"), objectCommon->GetShadowSoftness()));
	objectCommon->SetShadowBias(UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "shadow.bias"), objectCommon->GetShadowBias()));
	objectCommon->SetShadowArea(UILayoutIO::GetFloat(
		tuning, Key(keyPrefix, "shadow.area"), objectCommon->GetShadowArea()));
}

void AppendTuningEntries(
	std::vector<UILayoutIO::Entry>& entries,
	std::string_view keyPrefix)
{
	const Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	const SceneLightData& sceneLight = objectCommon->GetSceneLight();
	entries.push_back({ Key(keyPrefix, "sun.enabled"), { sceneLight.enable ? 1.0f : 0.0f } });
	entries.push_back({ Key(keyPrefix, "sun.color"), {
		sceneLight.color.x, sceneLight.color.y, sceneLight.color.z } });
	entries.push_back({ Key(keyPrefix, "sun.direction"), {
		sceneLight.direction.x, sceneLight.direction.y, sceneLight.direction.z } });
	entries.push_back({ Key(keyPrefix, "sun.intensity"), { sceneLight.intensity } });
	entries.push_back({ Key(keyPrefix, "sun.ambientColor"), {
		sceneLight.ambientColor.x, sceneLight.ambientColor.y, sceneLight.ambientColor.z } });
	entries.push_back({ Key(keyPrefix, "sun.ambientIntensity"), { sceneLight.ambientIntensity } });
	entries.push_back({ Key(keyPrefix, "sun.specularStrength"), { sceneLight.specularStrength } });
	entries.push_back({ Key(keyPrefix, "shadow.enabled"), {
		objectCommon->IsShadowEnabled() ? 1.0f : 0.0f } });
	entries.push_back({ Key(keyPrefix, "shadow.strength"), { objectCommon->GetShadowStrength() } });
	entries.push_back({ Key(keyPrefix, "shadow.softness"), { objectCommon->GetShadowSoftness() } });
	entries.push_back({ Key(keyPrefix, "shadow.bias"), { objectCommon->GetShadowBias() } });
	entries.push_back({ Key(keyPrefix, "shadow.area"), { objectCommon->GetShadowArea() } });
}

#ifdef _DEBUG
void DrawDebugUI()
{
	Engine::Graphics3D::Object3DCommon* objectCommon =
		Engine::Graphics3D::Object3DCommon::GetInstance();
	SceneLightData sceneLight = objectCommon->GetSceneLight();
	bool lightChanged = false;

	bool sunEnabled = sceneLight.enable != 0;
	if (ImGui::Checkbox("Sun Enabled", &sunEnabled)) {
		sceneLight.enable = sunEnabled ? 1 : 0;
		lightChanged = true;
	}
	float sunColor[3]{ sceneLight.color.x, sceneLight.color.y, sceneLight.color.z };
	if (ImGui::ColorEdit3("Sun Color", sunColor)) {
		sceneLight.color = { sunColor[0], sunColor[1], sunColor[2], 1.0f };
		lightChanged = true;
	}
	float sunDirection[3]{
		sceneLight.direction.x, sceneLight.direction.y, sceneLight.direction.z
	};
	if (ImGui::DragFloat3("Sun Direction", sunDirection, 0.02f, -1.0f, 1.0f)) {
		sceneLight.direction = { sunDirection[0], sunDirection[1], sunDirection[2] };
		lightChanged = true;
	}
	lightChanged |= ImGui::SliderFloat("Sun Intensity", &sceneLight.intensity, 0.0f, 3.0f);
	float ambientColor[3]{
		sceneLight.ambientColor.x, sceneLight.ambientColor.y, sceneLight.ambientColor.z
	};
	if (ImGui::ColorEdit3("Ambient Color", ambientColor)) {
		sceneLight.ambientColor = { ambientColor[0], ambientColor[1], ambientColor[2], 1.0f };
		lightChanged = true;
	}
	lightChanged |= ImGui::SliderFloat(
		"Ambient Intensity", &sceneLight.ambientIntensity, 0.0f, 1.0f);
	lightChanged |= ImGui::SliderFloat(
		"Specular Strength", &sceneLight.specularStrength, 0.0f, 1.0f);
	if (lightChanged) {
		objectCommon->SetSceneLight(sceneLight);
	}

	ImGui::Separator();
	bool shadowEnabled = objectCommon->IsShadowEnabled();
	if (ImGui::Checkbox("Shadow Enabled", &shadowEnabled)) {
		objectCommon->SetShadowEnabled(shadowEnabled);
	}
	float shadowStrength = objectCommon->GetShadowStrength();
	if (ImGui::SliderFloat("Shadow Strength", &shadowStrength, 0.0f, 1.0f)) {
		objectCommon->SetShadowStrength(shadowStrength);
	}
	float shadowSoftness = objectCommon->GetShadowSoftness();
	if (ImGui::SliderFloat("Shadow Softness", &shadowSoftness, 0.5f, 4.0f)) {
		objectCommon->SetShadowSoftness(shadowSoftness);
	}
	float shadowBias = objectCommon->GetShadowBias();
	if (ImGui::SliderFloat("Shadow Bias", &shadowBias, 0.0f, 0.006f, "%.5f")) {
		objectCommon->SetShadowBias(shadowBias);
	}
	float shadowArea = objectCommon->GetShadowArea();
	if (ImGui::SliderFloat("Shadow Area", &shadowArea, 20.0f, 160.0f)) {
		objectCommon->SetShadowArea(shadowArea);
	}
}
#endif

}
