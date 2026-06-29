#include "AudioDebugPanel.h"
#include <string>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame {

void DebugUI::Audio::Draw(
	bool* open,
	std::span<const AudioTuningEntry> entries,
	const std::function<void()>& save,
	const std::function<void()>& reload)
{
#ifdef _DEBUG
	if (!open || !*open) {
		return;
	}

	ImGui::Begin("オーディオ", open);
	float masterVolume = GameAudioCache::GetMasterVolume();
	if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
		GameAudioCache::SetMasterVolume(masterVolume);
	}

	if (save) {
		if (ImGui::Button("Save Debug Tuning")) {
			save();
		}
		if (reload) {
			ImGui::SameLine();
		}
	}
	if (reload && ImGui::Button("Reload Debug Tuning")) {
		reload();
	}

	if (!entries.empty() &&
		ImGui::CollapsingHeader("Audio Balance", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (const AudioTuningEntry& entry : entries) {
			const std::string label(entry.label);
			float volume =
				GameAudioCache::GetTunedVolume(entry.key, entry.fallbackVolume);
			if (ImGui::SliderFloat(
					label.c_str(),
					&volume,
					0.0f,
					1.0f)) {
				GameAudioCache::SetTunedVolume(entry.key, volume);
				if (entry.liveHandle) {
					GameAudioCache::SetVolumeFromTuning(
						entry.liveHandle,
						entry.key,
						entry.fallbackVolume);
				}
			}
		}
	}
	ImGui::End();
#else
	(void)open;
	(void)entries;
	(void)save;
	(void)reload;
#endif
}

}
