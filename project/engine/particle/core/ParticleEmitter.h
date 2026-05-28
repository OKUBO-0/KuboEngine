#pragma once
#include <Vector3.h>
#include <string>

/// @brief 指定条件でパーティクル発生要求を出すエミッタクラス
/// @details 発生位置、頻度、発生数、対象グループ名を保持し、
///          更新タイミングに応じて ParticleManager へ Emit を依頼する。
namespace Engine::Particle {

class ParticleEmitter
{
public:
	/// @brief エミッタ設定を指定して生成する
	/// @param position 発生位置
	/// @param lifetime 発生間隔
	/// @param currentTime 経過時間の初期値
	/// @param count 1回あたりの発生数
	/// @param name 発生先パーティクルグループ名
	/// @return なし
	ParticleEmitter(
		const Vector3& position,
		float lifetime,
		float currentTime,
		uint32_t count,
		const std::string& name
	
	);

	/// @brief 経過時間を進めて発生判定を更新する
	/// @param なし
	/// @return なし
	void Update();

	/// @brief 現在設定でパーティクル発生要求を送る
	/// @param なし
	/// @return なし
	void Emit();

	/// @brief 発生位置を取得する
	/// @param なし
	/// @return 発生位置
	const Vector3& GetPosition() const { return position_; }

	/// @brief 発生間隔を取得する
	/// @param なし
	/// @return 発生間隔
	float GetFrequency() const { return frequency; }

	/// @brief 現在の経過時間を取得する
	/// @param なし
	/// @return 経過時間
	float GetFrequencyTime() const { return frequencyTime; }

	/// @brief 1 回あたりの発生数を取得する
	/// @param なし
	/// @return 発生数
	uint32_t GetCount() const { return count; }

	/// @brief 発生先グループ名を取得する
	/// @param なし
	/// @return グループ名
	const std::string& GetName() const { return name_; }

	/// @brief 発生位置を設定する
	/// @param position 新しい発生位置
	/// @return なし
	void SetPosition(const Vector3& position) { position_ = position; }

	/// @brief 発生間隔を設定する
	/// @param frequency 新しい発生間隔
	/// @return なし
	void SetFrequency(float frequency) { this->frequency = frequency; }

	/// @brief 現在の経過時間を設定する
	/// @param frequencyTime 新しい経過時間
	/// @return なし
	void SetFrequencyTime(float frequencyTime) { this->frequencyTime = frequencyTime; }

	/// @brief 1 回あたりの発生数を設定する
	/// @param count 新しい発生数
	/// @return なし
	void SetCount(uint32_t count) { this->count = count; }

	/// @brief 発生先グループ名を設定する
	/// @param name 新しいグループ名
	/// @return なし
	void SetName(const std::string& name) { name_ = name; }

	
	
	
private:
	// 位置
	Vector3 position_;
	// 発生間隔
	float frequency;
	// 経過時間
	float frequencyTime;
	// 発生数
	uint32_t count;
	// 発生先グループ名
	std::string name_;
};

}

