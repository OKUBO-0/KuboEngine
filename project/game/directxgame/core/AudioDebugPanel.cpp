#include "AudioDebugPanel.h"

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace DirectXGame::DebugUI::Audio {

void Draw(
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
	if (ImGui::SliderFloat("Master", &masterVolume, 0.0f, 1.0f)) {
		GameAudioCache::SetMasterVolume(masterVolume);
	}
	float bgmVolume = GameAudioCache::GetBusVolume(AudioBus::Bgm);
	if (ImGui::SliderFloat("BGM", &bgmVolume, 0.0f, 1.0f)) {
		GameAudioCache::SetBusVolume(AudioBus::Bgm, bgmVolume);
	}
	float seVolume = GameAudioCache::GetBusVolume(AudioBus::Se);
	if (ImGui::SliderFloat("SE", &seVolume, 0.0f, 1.0f)) {
		GameAudioCache::SetBusVolume(AudioBus::Se, seVolume);
	}
	if (save && ImGui::Button("保存")) {
		save();
	}
	if (reload) {
		ImGui::SameLine();
		if (ImGui::Button("再読み込み")) {
			reload();
		}
	}
	if (ImGui::CollapsingHeader("Event Balance", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (const AudioTuningEntry& entry : entries) {
			float volume = GameAudioCache::GetTunedVolume(entry.key, entry.fallbackVolume);
			if (ImGui::SliderFloat(entry.label, &volume, 0.0f, 1.0f)) {
				GameAudioCache::SetTunedVolume(entry.key, volume);
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
