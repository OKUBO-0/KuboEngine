#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DirectXGame {

enum class CharacterId : int32_t {
	Default = 0,
	Bow = 1,
	Sword = 2,
	Handgun = 3,
};

struct CharacterDefinition {
	CharacterId id;
	int32_t unlockCost = 0;
	bool initiallyUnlocked = false;
};

struct RunResult {
	RunResult()
		: finalLevel(1), totalKillCount(0)
	{
	}

	int32_t totalExp = 0;
	int32_t coins = 0;
	union {
		int32_t finalLevel;
		uint32_t level;
	};
	union {
		int32_t totalKillCount;
		uint32_t killCount;
	};
	uint32_t elapsedFrames = 0;
};

class GameSession {
public:
	enum class SceneStressStage {
		Title,
		Game,
		Result,
	};
	struct SceneStressSample {
		SceneStressStage stage = SceneStressStage::Title;
		uint32_t cycle = 0;
		uint32_t sceneFrame = 0;
		uint32_t srvUsed = 0;
		uint32_t srvHighWatermark = 0;
		uint32_t texture2D = 0;
		uint32_t textureCube = 0;
		uint32_t structuredBuffer = 0;
		uint32_t shadowMap = 0;
		uint32_t otherSrv = 0;
		uint64_t workingSetBytes = 0;
		uint64_t privateBytes = 0;
	};

	GameSession();
	void BeginNewRun();
	void SetRandomSeed(uint32_t seed);
	bool IsSceneStressEnabled() const { return sceneStressCycleTarget_ > 0; }
	void AdvanceSceneStressFrame() { ++sceneStressFrameCount_; }
	uint32_t GetSceneStressFrameCount() const { return sceneStressFrameCount_; }
	uint32_t GetSceneStressCycleTarget() const { return sceneStressCycleTarget_; }
	uint32_t GetSceneStressGameFrameTarget() const { return sceneStressGameFrameTarget_; }
	bool IsSceneStressComplete() const;
	bool IsSceneStressReportWritten() const { return sceneStressReportWritten_; }
	void RecordSrvUsage(
		SceneStressStage stage,
		uint32_t usedCount,
		uint32_t highWatermark,
		uint32_t texture2D,
		uint32_t textureCube,
		uint32_t structuredBuffer,
		uint32_t shadowMap,
		uint32_t otherSrv);
	void WriteSceneStressReport();
	void RecordGpuTimingSummary(
		double averageFrameMilliseconds,
		double averageShadowMilliseconds,
		double frameP95Milliseconds,
		double shadowP95Milliseconds,
		double frameMaxMilliseconds,
		double shadowMaxMilliseconds,
		uint64_t frameSampleCount,
		uint64_t shadowSampleCount);
	void RecordShadowPassSummary(
		uint64_t passCount,
		uint64_t candidateCount,
		uint64_t submittedCount,
		uint64_t culledCount,
		uint32_t shadowMapSize,
		uint64_t shadowMapMemoryBytes,
		float shadowArea);
	void RecordSceneObjectCounts(
		uint32_t activeEnemyCount,
		uint32_t enemyStorageCount,
		uint32_t expOrbCount);

	void OnEnterTitleScene();
	void OnEnterGameScene();
	void OnEnterResultScene();

	void AdvanceGameFrame();
	void SetResultSummary(
		uint32_t elapsedFrames,
		uint32_t level,
		uint32_t killCount,
		int32_t totalExp = 0,
		int32_t coins = 0);
	void AddRunCoins(int32_t amount);
	bool TryPurchasePermanentMaxHP();
	bool TryPurchasePermanentAttack();
	bool TryPurchasePermanentMoveSpeed();
	bool TryPurchasePermanentExpPickupRange();
	bool TryPurchasePermanentCoinGain();
	bool TrySelectCharacter(CharacterId id);
	bool TryUnlockCharacter(CharacterId id);
	bool IsCharacterUnlocked(CharacterId id) const;
	int32_t GetRunCoins() const { return runCoins_; }
	int32_t GetOwnedCoins() const { return ownedCoins_; }
	CharacterId GetSelectedCharacterId() const { return selectedCharacterId_; }
	int32_t GetCharacterUnlockCost(CharacterId id) const;
	int32_t GetPermanentMaxHPLevel() const { return permanentMaxHPLevel_; }
	int32_t GetPermanentAttackLevel() const { return permanentAttackLevel_; }
	int32_t GetPermanentMoveSpeedLevel() const { return permanentMoveSpeedLevel_; }
	int32_t GetPermanentExpPickupRangeLevel() const { return permanentExpPickupRangeLevel_; }
	int32_t GetPermanentCoinGainLevel() const { return permanentCoinGainLevel_; }
	int32_t GetPermanentMaxHPCost() const;
	int32_t GetPermanentAttackCost() const;
	int32_t GetPermanentMoveSpeedCost() const;
	int32_t GetPermanentExpPickupRangeCost() const;
	int32_t GetPermanentCoinGainCost() const;

	uint32_t GetRunCount() const { return runCount_; }
	uint32_t GetTitleVisitCount() const { return titleVisitCount_; }
	uint32_t GetGameVisitCount() const { return gameVisitCount_; }
	uint32_t GetResultVisitCount() const { return resultVisitCount_; }
	uint32_t GetGameFrameCount() const { return gameFrameCount_; }
	uint32_t GetRunRandomSeed() const { return runRandomSeed_; }

	RunResult& GetResultData() { return resultData_; }
	const RunResult& GetResultData() const { return resultData_; }

private:
	void CommitRunCoinsToProfile();
	void LoadProfile();
	void SaveProfile() const;

	uint32_t runCount_ = 0;
	uint32_t titleVisitCount_ = 0;
	uint32_t gameVisitCount_ = 0;
	uint32_t resultVisitCount_ = 0;
	uint32_t gameFrameCount_ = 0;
	uint32_t baseRandomSeed_ = 0;
	uint32_t runRandomSeed_ = 0;
	uint32_t sceneStressCycleTarget_ = 0;
	uint32_t sceneStressFrameCount_ = 0;
	uint32_t sceneStressGameFrameTarget_ = 60;
	uint32_t stressMaxSrvUsed_ = 0;
	uint32_t stressMaxSrvHighWatermark_ = 0;
	bool sceneStressReportWritten_ = false;
	std::vector<SceneStressSample> sceneStressSamples_;
	double stressAverageFrameGpuMilliseconds_ = 0.0;
	double stressAverageShadowGpuMilliseconds_ = 0.0;
	double stressFrameGpuP95Milliseconds_ = 0.0;
	double stressShadowGpuP95Milliseconds_ = 0.0;
	double stressFrameGpuMaxMilliseconds_ = 0.0;
	double stressShadowGpuMaxMilliseconds_ = 0.0;
	uint64_t stressFrameGpuSampleCount_ = 0;
	uint64_t stressShadowGpuSampleCount_ = 0;
	uint64_t stressShadowPassCount_ = 0;
	uint64_t stressShadowCandidateCount_ = 0;
	uint64_t stressShadowSubmittedCount_ = 0;
	uint64_t stressShadowCulledCount_ = 0;
	uint32_t stressShadowMapSize_ = 0;
	uint64_t stressShadowMapMemoryBytes_ = 0;
	float stressShadowArea_ = 0.0f;
	uint32_t stressMaxActiveEnemyCount_ = 0;
	uint32_t stressMaxEnemyStorageCount_ = 0;
	uint32_t stressMaxExpOrbCount_ = 0;
	int32_t runCoins_ = 0;
	int32_t ownedCoins_ = 0;
	bool runCoinsCommitted_ = false;
	CharacterId selectedCharacterId_ = CharacterId::Default;
	int32_t unlockedCharacterMask_ = 0;
	int32_t permanentMaxHPLevel_ = 0;
	int32_t permanentAttackLevel_ = 0;
	int32_t permanentMoveSpeedLevel_ = 0;
	int32_t permanentExpPickupRangeLevel_ = 0;
	int32_t permanentCoinGainLevel_ = 0;
	RunResult resultData_{};
};

}
