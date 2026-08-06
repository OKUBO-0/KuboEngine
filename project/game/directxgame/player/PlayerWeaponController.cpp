#include "PlayerWeaponController.h"
#include "WeaponUpgradeData.h"

#include "CsvReader.h"
#include "GameplayRules.h"
#include "EnemyManager.h"
#include "GameAudioCache.h"
#include "GameModelCache.h"
#include "GameSession.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>
#ifdef _DEBUG
#include "DataPaths.h"
#include <imgui.h>
#endif

namespace {

constexpr float kMinWeaponInterval =
	DirectXGame::GameplayRules::kMinimumWeaponInterval;
constexpr int32_t kMaxCatchUpAttacksPerFrame = 4;
constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
const DirectXGame::NormalBullet::VisualStyle kBowArrowVisual{
	"quaternius_weapons/arrow.glb",
	{ 1.0f, 0.96f, 0.72f, 1.0f },
	{ 150.0f, 150.0f, 144.0f },
	{ 0.0f, 0.0f, 0.0f },
	0.58f,
};
const DirectXGame::NormalBullet::VisualStyle kFlameStaffVisual{
	"fireball.obj",
	{ 1.0f, 0.28f, 0.03f, 1.0f },
	{ 1.26f, 1.26f, 1.26f },
};

float PositiveFiniteOr(float value, float fallback)
{
	return std::isfinite(value) && value > 0.0f
		? value
		: fallback;
}

Vector3 PlayerFacingDirection(const DirectXGame::Player& player)
{
	const float angle = player.GetWorldRotationY();
	return { std::sin(angle), 0.0f, std::cos(angle) };
}

void PlayWeaponSound(
	const char* path,
	std::string_view key,
	float fallbackVolume,
	float minimumIntervalSeconds)
{
	const DirectXGame::SoundHandle handle =
		DirectXGame::GameAudioCache::LoadWave(path);
	DirectXGame::GameAudioCache::PlayTuned(
		handle,
		key,
		fallbackVolume,
		minimumIntervalSeconds);
}

#ifdef _DEBUG
const char* CharacterIdDebugName(DirectXGame::CharacterId characterId)
{
	switch (characterId) {
	case DirectXGame::CharacterId::Default:
		return "0 Default";
	case DirectXGame::CharacterId::Bow:
		return "1 Bow";
	case DirectXGame::CharacterId::Sword:
		return "2 Sword";
	case DirectXGame::CharacterId::Handgun:
		return "3 Handgun";
	}
	return "Unknown";
}
#endif

}

namespace DirectXGame {

PlayerWeaponController::~PlayerWeaponController() = default;

void PlayerWeaponController::Initialize(
	const std::string& upgradeSettingsPath)
{
	LoadUpgradeSettings(upgradeSettingsPath);
	explosiveBurstInterval_ = GetUpgradeSetting(
		"flameStaff.burstInterval",
		explosiveBurstInterval_);
}

void PlayerWeaponController::SetCharacterId(CharacterId characterId)
{
	characterId_ = characterId;
}

bool PlayerWeaponController::LoadStatusValue(
	const std::string& key,
	const std::string& value)
{
	if (key == "normalBulletInterval") {
		normalBulletInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletInterval");
	} else if (key == "normalBulletMinInterval") {
		normalBulletMinInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.normalBulletMinInterval");
	} else if (key == "explosiveBulletInterval") {
		explosiveBulletInterval_ = CsvReader::ParseFloat(
			value,
			"playerStatus.explosiveBulletInterval");
	} else {
		return false;
	}
	if (normalBulletInterval_ <= 0.0f ||
		normalBulletMinInterval_ <= 0.0f ||
		explosiveBulletInterval_ <= 0.0f) {
		throw std::runtime_error(
			"playerStatus contains an invalid weapon interval value");
	}
	normalBulletInterval_ = GameplayRules::NormalizeWeaponInterval(
		normalBulletInterval_,
		normalBulletMinInterval_);
	normalBulletMinInterval_ = (std::max)(
		kMinWeaponInterval,
		normalBulletMinInterval_);
	explosiveBulletInterval_ = GameplayRules::NormalizeWeaponInterval(
		explosiveBulletInterval_,
		kMinWeaponInterval);
	return true;
}

void PlayerWeaponController::LoadUpgradeSettings(
	const std::string& filePath)
{
	upgradeSettings_.clear();
	const CsvReader::CsvTable rows = CsvReader::LoadRows(filePath);
	for (const CsvReader::CsvRow& row : rows) {
		if (row.size() < 2 || row[0].empty() || row[1].empty()) {
			continue;
		}
		const float setting = CsvReader::ParseFloat(
			row[1],
			"weaponUpgradeSettings." + row[0]);
		// damageBonus は 0 を「増加なし」として許可し、それ以外の間隔/半径/個数系は正値に限定する。
		const bool zeroAllowed =
			row[0].find("damageBonus") != std::string::npos;
		if (setting < 0.0f || (!zeroAllowed && setting == 0.0f)) {
			throw std::runtime_error(
				"weaponUpgradeSettings." + row[0] +
				" must be positive");
		}
		upgradeSettings_[row[0]] = setting;
	}
}

void PlayerWeaponController::LoadVisualTuning(
	const UILayoutIO::LayoutMap& tuning)
{
	heldBowVisual_.scale = UILayoutIO::GetFloat(
		tuning, "weaponVisual.bowScale", heldBowVisual_.scale);
	heldBowVisual_.position = UILayoutIO::GetVector3(
		tuning, "weaponVisual.bowPosition", heldBowVisual_.position);
	heldBowVisual_.rotation = UILayoutIO::GetVector3(
		tuning, "weaponVisual.bowRotation", heldBowVisual_.rotation);
	heldSwordVisual_.scale = UILayoutIO::GetFloat(
		tuning, "weaponVisual.swordScale", heldSwordVisual_.scale);
	heldSwordVisual_.position = UILayoutIO::GetVector3(
		tuning, "weaponVisual.swordPosition", heldSwordVisual_.position);
	heldSwordVisual_.rotation = UILayoutIO::GetVector3(
		tuning, "weaponVisual.swordRotation", heldSwordVisual_.rotation);
	heldSwordVisual_.rotateWithPlayer = UILayoutIO::GetFloat(
		tuning, "weaponVisual.swordRotateWithPlayer", heldSwordVisual_.rotateWithPlayer ? 1.0f : 0.0f) != 0.0f;
	heldHandgunVisual_.scale = UILayoutIO::GetFloat(
		tuning, "weaponVisual.handgunScale", heldHandgunVisual_.scale);
	heldHandgunVisual_.position = UILayoutIO::GetVector3(
		tuning, "weaponVisual.handgunPosition", heldHandgunVisual_.position);
	heldHandgunVisual_.rotation = UILayoutIO::GetVector3(
		tuning, "weaponVisual.handgunRotation", heldHandgunVisual_.rotation);
	heldHandgunVisual_.rotateWithPlayer = UILayoutIO::GetFloat(
		tuning, "weaponVisual.handgunRotateWithPlayer", heldHandgunVisual_.rotateWithPlayer ? 1.0f : 0.0f) != 0.0f;
}

void PlayerWeaponController::AppendVisualTuningEntries(
	std::vector<UILayoutIO::Entry>& entries) const
{
	entries.insert(entries.end(), {
		{ "weaponVisual.bowScale", { heldBowVisual_.scale } },
		{ "weaponVisual.bowPosition", { heldBowVisual_.position.x, heldBowVisual_.position.y, heldBowVisual_.position.z } },
		{ "weaponVisual.bowRotation", { heldBowVisual_.rotation.x, heldBowVisual_.rotation.y, heldBowVisual_.rotation.z } },
		{ "weaponVisual.swordScale", { heldSwordVisual_.scale } },
		{ "weaponVisual.swordPosition", { heldSwordVisual_.position.x, heldSwordVisual_.position.y, heldSwordVisual_.position.z } },
		{ "weaponVisual.swordRotation", { heldSwordVisual_.rotation.x, heldSwordVisual_.rotation.y, heldSwordVisual_.rotation.z } },
		{ "weaponVisual.swordRotateWithPlayer", { heldSwordVisual_.rotateWithPlayer ? 1.0f : 0.0f } },
		{ "weaponVisual.handgunScale", { heldHandgunVisual_.scale } },
		{ "weaponVisual.handgunPosition", { heldHandgunVisual_.position.x, heldHandgunVisual_.position.y, heldHandgunVisual_.position.z } },
		{ "weaponVisual.handgunRotation", { heldHandgunVisual_.rotation.x, heldHandgunVisual_.rotation.y, heldHandgunVisual_.rotation.z } },
		{ "weaponVisual.handgunRotateWithPlayer", { heldHandgunVisual_.rotateWithPlayer ? 1.0f : 0.0f } },
	});
}

#ifdef _DEBUG
void PlayerWeaponController::DrawWeaponVisualDebugUI()
{
	if (!ImGui::CollapsingHeader("武器モデルSRT調整")) {
		return;
	}
	ImGui::Text("CharacterId: %s", CharacterIdDebugName(characterId_));
	const auto drawCharacterButton = [this](CharacterId id, const char* label) {
		const bool selectedBeforeClick = characterId_ == id;
		if (selectedBeforeClick) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.56f, 0.92f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.64f, 1.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.44f, 0.78f, 1.0f));
		}
		if (ImGui::Button(label)) {
			characterId_ = id;
		}
		if (selectedBeforeClick) {
			ImGui::PopStyleColor(3);
		}
	};
	drawCharacterButton(CharacterId::Default, "0 Default");
	ImGui::SameLine();
	drawCharacterButton(CharacterId::Bow, "1 Bow");
	ImGui::SameLine();
	drawCharacterButton(CharacterId::Sword, "2 Sword");
	ImGui::SameLine();
	drawCharacterButton(CharacterId::Handgun, "3 Handgun");
	ImGui::TextUnformatted("表示ルール: 1=Bow  2=Sword  3=Handgun");
	ImGui::DragFloat("Bow Scale", &heldBowVisual_.scale, 1.0f, 10.0f, 600.0f);
	ImGui::DragFloat3("Bow Position", &heldBowVisual_.position.x, 0.02f, -4.0f, 4.0f);
	ImGui::DragFloat3("Bow Rotation", &heldBowVisual_.rotation.x, 0.02f, -6.28f, 6.28f);
	ImGui::Separator();
	ImGui::DragFloat("Sword Scale", &heldSwordVisual_.scale, 1.0f, 10.0f, 600.0f);
	ImGui::DragFloat3("Sword Position", &heldSwordVisual_.position.x, 0.02f, -4.0f, 4.0f);
	ImGui::DragFloat3("Sword Rotation", &heldSwordVisual_.rotation.x, 0.02f, -6.28f, 6.28f);
	ImGui::Checkbox("Sword Rotate With Player", &heldSwordVisual_.rotateWithPlayer);
	ImGui::Separator();
	ImGui::DragFloat("Handgun Scale", &heldHandgunVisual_.scale, 1.0f, 10.0f, 600.0f);
	ImGui::DragFloat3("Handgun Position", &heldHandgunVisual_.position.x, 0.02f, -4.0f, 4.0f);
	ImGui::DragFloat3("Handgun Rotation", &heldHandgunVisual_.rotation.x, 0.02f, -6.28f, 6.28f);
	ImGui::Checkbox("Handgun Rotate With Player", &heldHandgunVisual_.rotateWithPlayer);
	ImGui::TextUnformatted("Position はプレイヤー基準です。x が右手方向、z が正面距離です。");
	if (ImGui::Button("Save Weapon SRT")) {
		std::vector<UILayoutIO::Entry> entries;
		AppendVisualTuningEntries(entries);
		UILayoutIO::Save(DataPaths::kDebugTuning, entries);
	}
}
#endif

void PlayerWeaponController::Update(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& playerStats)
{
	// 武器更新は固定順にする。弾生成、範囲ダメージ、演出イベントの発生順がフレーム間でぶれないようにする。
	UpdateNormalBullets(deltaTime, player, enemyManager, playerStats);
	UpdateOrbitBullets(deltaTime, player, playerStats);
	UpdateLightning(deltaTime, player, enemyManager, playerStats);
	UpdateExplosiveBullets(deltaTime, player, enemyManager, playerStats);
	UpdateSword(deltaTime, player, enemyManager, playerStats);
	UpdateAura(deltaTime, player, enemyManager, playerStats);
	UpdateFlameShoes(deltaTime, player, enemyManager, playerStats);
	UpdateBone(deltaTime, player, enemyManager, playerStats);
	UpdateHandgun(deltaTime, player, enemyManager, playerStats);
	UpdateBoomerang(deltaTime, player, enemyManager, playerStats);
	if (!player) {
		return;
	}
	if (HasWeapon(WeaponType::BowArrow) && ShouldDrawHeldWeapon(WeaponType::BowArrow)) {
		EnsureHeldWeaponModel(WeaponType::BowArrow, bowObject_);
		UpdateHeldWeaponModel(*bowObject_, heldBowVisual_, *player);
	}
	if (HasWeapon(WeaponType::Sword) && ShouldDrawHeldWeapon(WeaponType::Sword)) {
		EnsureHeldWeaponModel(WeaponType::Sword, swordObject_);
		UpdateHeldWeaponModel(*swordObject_, heldSwordVisual_, *player);
	}
	if (HasWeapon(WeaponType::Handgun) && ShouldDrawHeldWeapon(WeaponType::Handgun)) {
		EnsureHeldWeaponModel(WeaponType::Handgun, handgunObject_);
		UpdateHeldWeaponModel(*handgunObject_, heldHandgunVisual_, *player);
	}
}

void PlayerWeaponController::Draw()
{
	// Draw は所有中の弾・投射物だけを描画する。範囲攻撃系の見た目は Presentation 側がイベントから描く。
	std::vector<Engine::Graphics3D::Object3D*> renderObjects;
	renderObjects.reserve(
		normalBullets_.size() +
		orbitBullets_.size() +
		explosiveBullets_.size() +
		boneWeapon_.GetBullets().size() +
		handgunWeapon_.GetBullets().size() +
		boomerangWeapon_.GetBullets().size() +
		3);
	if (HasWeapon(WeaponType::BowArrow) &&
		ShouldDrawHeldWeapon(WeaponType::BowArrow) &&
		bowObject_) {
		renderObjects.push_back(bowObject_.get());
	}
	if (HasWeapon(WeaponType::Sword) &&
		ShouldDrawHeldWeapon(WeaponType::Sword) &&
		swordObject_) {
		renderObjects.push_back(swordObject_.get());
	}
	if (HasWeapon(WeaponType::Handgun) &&
		ShouldDrawHeldWeapon(WeaponType::Handgun) &&
		handgunObject_) {
		renderObjects.push_back(handgunObject_.get());
	}
	for (const std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		if (!bullet) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = bullet->GetRenderObject()) {
			renderObjects.push_back(object);
		}
	}
	for (const std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		if (!bullet) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = bullet->GetRenderObject()) {
			renderObjects.push_back(object);
		}
	}
	for (const std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		if (!bullet) {
			continue;
		}
		if (Engine::Graphics3D::Object3D* object = bullet->GetRenderObject()) {
			renderObjects.push_back(object);
		}
	}
	boneWeapon_.AppendRenderObjects(renderObjects);
	handgunWeapon_.AppendRenderObjects(renderObjects);
	boomerangWeapon_.AppendRenderObjects(renderObjects);
	for (Engine::Graphics3D::Object3D* object : renderObjects) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object);
	}
}

bool PlayerWeaponController::ShouldDrawHeldWeapon(WeaponType type) const
{
	(void)type;
	return false;

	switch (characterId_) {
	case CharacterId::Bow:
		return type == WeaponType::BowArrow;
	case CharacterId::Sword:
		return type == WeaponType::Sword;
	case CharacterId::Handgun:
		return type == WeaponType::Handgun;
	case CharacterId::Default:
	default:
		return false;
	}
}

void PlayerWeaponController::EnsureHeldWeaponModel(
	WeaponType type,
	std::unique_ptr<Engine::Graphics3D::Object3D>& object)
{
	if (object) {
		return;
	}
	const char* modelPath = "quaternius_weapons/bow.glb";
	if (type == WeaponType::Sword) {
		modelPath = "quaternius_weapons/sword.glb";
	} else if (type == WeaponType::Handgun) {
		modelPath = "quaternius_weapons/pistol.glb";
	}
	const ModelHandle modelHandle = GameModelCache::Load(modelPath);
	object = std::make_unique<Engine::Graphics3D::Object3D>();
	object->Initialize(
		Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*object, modelHandle);
	object->SetSkyboxFilePath(kEnvironmentTexturePath);
	object->SetEnvironmentReflectionStrength(0.0f);
	object->SetEnvironmentRoughness(1.0f);
	object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
}

void PlayerWeaponController::UpdateHeldWeaponModel(
	Engine::Graphics3D::Object3D& object,
	const HeldWeaponVisualTuning& tuning,
	const Player& player)
{
	const Vector3 playerPosition = player.GetWorldPosition();
	const float yaw = player.GetWorldRotationY();
	const Vector3 forward{ std::sin(yaw), 0.0f, std::cos(yaw) };
	const Vector3 right{ std::cos(yaw), 0.0f, -std::sin(yaw) };
	object.SetScale({
		tuning.scale,
		tuning.scale,
		tuning.scale,
		});
	object.SetRotate({
		tuning.rotation.x,
		(tuning.rotateWithPlayer ? yaw : 0.0f) + tuning.rotation.y,
		tuning.rotation.z,
		});
	object.SetTranslate({
		playerPosition.x + right.x * tuning.position.x + forward.x * tuning.position.z,
		playerPosition.y + tuning.position.y,
		playerPosition.z + right.z * tuning.position.x + forward.z * tuning.position.z,
		});
	object.Update();
}

void PlayerWeaponController::UpgradeNormalBullets(Player*)
{
	if (IsNormalBulletMaxLevel()) {
		return;
	}
	++normalBulletLevel_;
	ApplyNormalBulletUpgradeLevel(normalBulletLevel_);
	normalBulletInterval_ = (std::max)(
		normalBulletMinInterval_,
		PositiveFiniteOr(
			normalBulletInterval_,
			normalBulletMinInterval_));
}

void PlayerWeaponController::AddOrbitBullets(Player* player)
{
	if (hasOrbitBullets_ || !CanAcquireWeapon(WeaponType::Rock)) {
		return;
	}
	hasOrbitBullets_ = true;
	orbitBulletLevel_ = 1;
	ApplyOrbitUpgradeLevel(orbitBulletLevel_);
	RebuildOrbitBullets(player);
}

void PlayerWeaponController::UpgradeOrbitBullets(Player* player)
{
	if (!hasOrbitBullets_) {
		AddOrbitBullets(player);
		return;
	}
	if (IsOrbitBulletMaxLevel()) {
		return;
	}
	++orbitBulletLevel_;
	ApplyOrbitUpgradeLevel(orbitBulletLevel_);
	RebuildOrbitBullets(player);
}

void PlayerWeaponController::AddLightning()
{
	if (hasLightning_ || !CanAcquireWeapon(WeaponType::ThunderStaff)) {
		return;
	}
	hasLightning_ = true;
	lightningLevel_ = 1;
	ApplyLightningUpgradeLevel(lightningLevel_);
}

void PlayerWeaponController::UpgradeLightning()
{
	if (!hasLightning_) {
		AddLightning();
		return;
	}
	if (IsLightningMaxLevel()) {
		return;
	}
	++lightningLevel_;
	ApplyLightningUpgradeLevel(lightningLevel_);
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(lightningInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::AddExplosiveBullets()
{
	if (hasExplosiveBullets_ ||
		!CanAcquireWeapon(WeaponType::FlameStaff)) {
		return;
	}
	hasExplosiveBullets_ = true;
	explosiveBulletLevel_ = 1;
	ApplyExplosiveBulletUpgradeLevel(explosiveBulletLevel_);
}

void PlayerWeaponController::UpgradeExplosiveBullets()
{
	if (!hasExplosiveBullets_) {
		AddExplosiveBullets();
		return;
	}
	if (IsExplosiveBulletMaxLevel()) {
		return;
	}
	++explosiveBulletLevel_;
	ApplyExplosiveBulletUpgradeLevel(explosiveBulletLevel_);
	explosiveBulletInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(explosiveBulletInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::UpgradeSword()
{
	if (!hasSword_) {
		if (!CanAcquireWeapon(WeaponType::Sword)) {
			return;
		}
		hasSword_ = true;
		swordLevel_ = 1;
	} else if (IsSwordMaxLevel()) {
		return;
	} else {
		++swordLevel_;
	}
	ApplySwordUpgradeLevel(swordLevel_);
}

void PlayerWeaponController::UpgradeAura()
{
	if (!hasAura_) {
		if (!CanAcquireWeapon(WeaponType::Aura)) {
			return;
		}
		hasAura_ = true;
		auraLevel_ = 1;
	} else if (IsAuraMaxLevel()) {
		return;
	} else {
		++auraLevel_;
	}
	ApplyAuraUpgradeLevel(auraLevel_);
}

void PlayerWeaponController::UpgradeFlameShoes()
{
	if (!hasFlameShoes_) {
		if (!CanAcquireWeapon(WeaponType::FlameShoes)) {
			return;
		}
		hasFlameShoes_ = true;
		flameShoesLevel_ = 1;
		flameShoesPositionInitialized_ = false;
		flameShoesLastDirection_ = { 0.0f, 0.0f, 1.0f };
	} else if (IsFlameShoesMaxLevel()) {
		return;
	} else {
		++flameShoesLevel_;
	}
	ApplyFlameShoesUpgradeLevel(flameShoesLevel_);
}

void PlayerWeaponController::UpgradeBone()
{
	if (!CanAcquireWeapon(WeaponType::Bone) || IsBoneMaxLevel()) return;
	boneWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeHandgun()
{
	if (!CanAcquireWeapon(WeaponType::Handgun) || IsHandgunMaxLevel()) return;
	handgunWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeBoomerang()
{
	if (!CanAcquireWeapon(WeaponType::Boomerang) || IsBoomerangMaxLevel()) return;
	boomerangWeapon_.Upgrade(upgradeSettings_);
}

void PlayerWeaponController::UpgradeWeapon(WeaponType type, Player* player)
{
	switch (type) {
	case WeaponType::BowArrow:
		UpgradeNormalBullets(player);
		break;
	case WeaponType::Rock:
		UpgradeOrbitBullets(player);
		break;
	case WeaponType::ThunderStaff:
		UpgradeLightning();
		break;
	case WeaponType::FlameStaff:
		UpgradeExplosiveBullets();
		break;
	case WeaponType::Sword:
		UpgradeSword();
		break;
	case WeaponType::Aura:
		UpgradeAura();
		break;
	case WeaponType::FlameShoes:
		UpgradeFlameShoes();
		break;
	case WeaponType::Bone: UpgradeBone(); break;
	case WeaponType::Handgun: UpgradeHandgun(); break;
	case WeaponType::Boomerang: UpgradeBoomerang(); break;
	}
}

bool PlayerWeaponController::IsWeaponMaxLevel(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow:
		return IsNormalBulletMaxLevel();
	case WeaponType::Rock:
		return IsOrbitBulletMaxLevel();
	case WeaponType::ThunderStaff:
		return IsLightningMaxLevel();
	case WeaponType::FlameStaff:
		return IsExplosiveBulletMaxLevel();
	case WeaponType::Sword:
		return IsSwordMaxLevel();
	case WeaponType::Aura:
		return IsAuraMaxLevel();
	case WeaponType::FlameShoes:
		return IsFlameShoesMaxLevel();
	case WeaponType::Bone: return IsBoneMaxLevel();
	case WeaponType::Handgun: return IsHandgunMaxLevel();
	case WeaponType::Boomerang: return IsBoomerangMaxLevel();
	}
	return true;
}

int32_t PlayerWeaponController::GetWeaponLevel(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow: return GetNormalBulletLevel();
	case WeaponType::Rock: return GetOrbitBulletLevel();
	case WeaponType::ThunderStaff: return GetLightningLevel();
	case WeaponType::FlameStaff: return GetExplosiveBulletLevel();
	case WeaponType::Sword: return GetSwordLevel();
	case WeaponType::Aura: return GetAuraLevel();
	case WeaponType::FlameShoes: return GetFlameShoesLevel();
	case WeaponType::Bone: return GetBoneLevel();
	case WeaponType::Handgun: return GetHandgunLevel();
	case WeaponType::Boomerang: return GetBoomerangLevel();
	}
	return 0;
}

int32_t PlayerWeaponController::GetWeaponMaxLevel(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow: return kNormalBulletMaxLevel;
	case WeaponType::Rock: return kOrbitBulletMaxLevel;
	case WeaponType::ThunderStaff: return kLightningMaxLevel;
	case WeaponType::FlameStaff: return kExplosiveBulletMaxLevel;
	case WeaponType::Sword: return kSwordMaxLevel;
	case WeaponType::Aura: return kAuraMaxLevel;
	case WeaponType::FlameShoes: return kFlameShoesMaxLevel;
	case WeaponType::Bone: return kBoneMaxLevel;
	case WeaponType::Handgun: return kHandgunMaxLevel;
	case WeaponType::Boomerang: return kBoomerangMaxLevel;
	}
	return 0;
}

void PlayerWeaponController::DebugSetWeaponLevel(
	WeaponType type,
	Player* player,
	int32_t level)
{
	const int32_t targetLevel =
		std::clamp(level, 0, GetWeaponMaxLevel(type));
	switch (type) {
	case WeaponType::BowArrow:
		ResetNormalBulletWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasNormalBullets_ = true;
		normalBulletLevel_ = 1;
		for (int32_t currentLevel = 2; currentLevel <= targetLevel; ++currentLevel) {
			normalBulletLevel_ = currentLevel;
			ApplyNormalBulletUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::Rock:
		ResetOrbitWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasOrbitBullets_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			orbitBulletLevel_ = currentLevel;
			ApplyOrbitUpgradeLevel(currentLevel);
		}
		RebuildOrbitBullets(player);
		break;
	case WeaponType::ThunderStaff:
		ResetLightningWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasLightning_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			lightningLevel_ = currentLevel;
			ApplyLightningUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::FlameStaff:
		ResetExplosiveWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasExplosiveBullets_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			explosiveBulletLevel_ = currentLevel;
			ApplyExplosiveBulletUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::Sword:
		ResetSwordWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasSword_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			swordLevel_ = currentLevel;
			ApplySwordUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::Aura:
		ResetAuraWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasAura_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			auraLevel_ = currentLevel;
			ApplyAuraUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::FlameShoes:
		ResetFlameShoesWeapon();
		if (targetLevel <= 0) {
			return;
		}
		hasFlameShoes_ = true;
		for (int32_t currentLevel = 1; currentLevel <= targetLevel; ++currentLevel) {
			flameShoesLevel_ = currentLevel;
			ApplyFlameShoesUpgradeLevel(currentLevel);
		}
		break;
	case WeaponType::Bone:
		boneWeapon_.DebugSetLevel(upgradeSettings_, targetLevel);
		break;
	case WeaponType::Handgun:
		handgunWeapon_.DebugSetLevel(upgradeSettings_, targetLevel);
		break;
	case WeaponType::Boomerang:
		boomerangWeapon_.DebugSetLevel(upgradeSettings_, targetLevel);
		break;
	}
}

void PlayerWeaponController::DebugRemoveWeapon(WeaponType type, Player* player)
{
	DebugSetWeaponLevel(type, player, 0);
}

bool PlayerWeaponController::HasWeapon(WeaponType type) const
{
	switch (type) {
	case WeaponType::BowArrow:
		return hasNormalBullets_;
	case WeaponType::Rock:
		return hasOrbitBullets_;
	case WeaponType::ThunderStaff:
		return hasLightning_;
	case WeaponType::FlameStaff:
		return hasExplosiveBullets_;
	case WeaponType::Sword:
		return hasSword_;
	case WeaponType::Aura:
		return hasAura_;
	case WeaponType::FlameShoes:
		return hasFlameShoes_;
	case WeaponType::Bone: return HasBone();
	case WeaponType::Handgun: return HasHandgun();
	case WeaponType::Boomerang: return HasBoomerang();
	}
	return false;
}

size_t PlayerWeaponController::GetEquippedWeaponCount() const
{
	return static_cast<size_t>(HasWeapon(WeaponType::BowArrow)) +
		static_cast<size_t>(hasOrbitBullets_) +
		static_cast<size_t>(hasLightning_) +
		static_cast<size_t>(hasExplosiveBullets_) +
		static_cast<size_t>(hasSword_) +
		static_cast<size_t>(hasAura_) +
		static_cast<size_t>(hasFlameShoes_) +
		static_cast<size_t>(HasBone()) +
		static_cast<size_t>(HasHandgun()) +
		static_cast<size_t>(HasBoomerang());
}

bool PlayerWeaponController::CanAcquireWeapon(WeaponType type) const
{
	return HasWeapon(type) ||
		GetEquippedWeaponCount() < kMaxEquippedWeaponTypes;
}

void PlayerWeaponController::DebugFireWeapon(
	WeaponType type,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& playerStats)
{
	if (!player || !HasWeapon(type)) {
		return;
	}
	switch (type) {
	case WeaponType::BowArrow:
		normalBulletTimer_ = 999.0f;
		UpdateNormalBullets(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::Rock:
		RebuildOrbitBullets(player);
		UpdateOrbitBullets(1.0f / 60.0f, player, playerStats);
		break;
	case WeaponType::ThunderStaff:
		lightningTimer_ = 999.0f;
		UpdateLightning(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::FlameStaff:
		explosiveBulletTimer_ = 999.0f;
		explosiveBurstTimer_ = 999.0f;
		UpdateExplosiveBullets(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::Sword:
		swordTimer_ = 999.0f;
		UpdateSword(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::Aura:
		auraTimer_ = 999.0f;
		UpdateAura(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::FlameShoes:
		flameShoesPositionInitialized_ = true;
		flameShoesLastSpawnPosition_ =
			player->GetWorldPosition() -
			PlayerFacingDirection(*player) * flameShoesSpawnDistance_;
		flameShoesLastDirection_ = PlayerFacingDirection(*player);
		UpdateFlameShoes(0.0f, player, enemyManager, playerStats);
		break;
	case WeaponType::Bone:
		boneWeapon_.DebugFire(
			player->GetWorldPosition(),
			ResolveAimDirection(*player, enemyManager),
			playerStats);
		break;
	case WeaponType::Handgun:
		handgunWeapon_.DebugFire(
			player->GetWorldPosition(),
			ResolveAimDirection(*player, enemyManager),
			playerStats);
		break;
	case WeaponType::Boomerang:
		boomerangWeapon_.DebugFire(
			player->GetWorldPosition(),
			ResolveAimDirection(*player, enemyManager),
			playerStats);
		break;
	}
}

void PlayerWeaponController::MaxAllWeapons(Player* player)
{
	for (WeaponType type : kUpgradeableWeaponTypes) {
		if (!CanAcquireWeapon(type)) {
			continue;
		}
		while (!IsWeaponMaxLevel(type)) {
			UpgradeWeapon(type, player);
		}
	}
}

void PlayerWeaponController::ResetBulletTelemetry()
{
	peakNormalBulletCount_ = normalBullets_.size();
	normalBulletPruneCount_ = 0;
}

void PlayerWeaponController::UpdateNormalBullets(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentNormalBulletShotPositions_.clear();
	const WeaponRuntimeStats runtime = stats.Resolve({
		9.0f + normalBulletDamageBonus_,
		normalBulletInterval_,
		normalBulletSpeed_,
		normalBulletRange_,
		normalBulletScale_,
		normalBulletAmount_,
		}, GetWeaponStatApplicability(WeaponType::BowArrow));
	if (hasNormalBullets_ && player) {
		normalBulletTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (normalBulletTimer_ >= runtime.interval &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			const Vector3 forward = ResolveAimDirection(
				*player, enemyManager);
			const Vector3 right{
				forward.z,
				0.0f,
				-forward.x,
			};
			const float centerOffset =
				static_cast<float>(runtime.projectileCount - 1) *
				0.5f;
			for (int32_t index = 0;
				index < runtime.projectileCount;
				++index) {
				const float spreadAngle =
					(static_cast<float>(index) - centerOffset) * 0.12f;
				const float cosine = std::cos(spreadAngle);
				const float sine = std::sin(spreadAngle);
				const Vector3 shotDirection{
					forward.x * cosine + right.x * sine,
					0.0f,
					forward.z * cosine + right.z * sine,
				};
				const Vector3 spawnPosition = MakePlayerProjectileSpawnPosition(
					player->GetWorldPosition(),
					shotDirection);
				NormalBullet& bullet = AcquireNormalBullet();
				bullet.InitializeForward(
					spawnPosition,
					shotDirection,
					runtime.projectileSpeed,
					runtime.duration,
					normalBulletPierceCount_,
					runtime.areaSize,
					NormalBullet::MovementMode::Straight,
					kBowArrowVisual);
				peakNormalBulletCount_ = (std::max)(
					peakNormalBulletCount_,
					normalBullets_.size());
				if (normalBullets_.size() >
					kMaxActiveNormalBullets) {
					RecycleNormalBullet(0);
					++normalBulletPruneCount_;
				}
				recentNormalBulletShotPositions_.push_back(
					spawnPosition + shotDirection * 0.42f);
			}
			normalBulletTimer_ -= runtime.interval;
			++catchUpAttackCount;
		}
		if (normalBulletTimer_ >= runtime.interval) {
			normalBulletTimer_ = std::fmod(
				normalBulletTimer_,
				runtime.interval);
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : normalBullets_) {
		bullet->Update(
			player ? player->GetWorldPosition() : Vector3{},
			deltaTime);
	}
	RecycleInactiveNormalBullets();
}

NormalBullet& PlayerWeaponController::AcquireNormalBullet()
{
	if (normalBulletPool_.empty()) {
		normalBullets_.push_back(std::make_unique<NormalBullet>());
		return *normalBullets_.back();
	}

	normalBullets_.push_back(std::move(normalBulletPool_.back()));
	normalBulletPool_.pop_back();
	return *normalBullets_.back();
}

void PlayerWeaponController::RecycleNormalBullet(size_t index)
{
	if (index >= normalBullets_.size()) {
		return;
	}

	if (normalBullets_[index]) {
		normalBullets_[index]->Deactivate();
		normalBulletPool_.push_back(std::move(normalBullets_[index]));
	}
	if (index != normalBullets_.size() - 1) {
		normalBullets_[index] = std::move(normalBullets_.back());
	}
	normalBullets_.pop_back();
}

void PlayerWeaponController::RecycleInactiveNormalBullets()
{
	for (size_t index = 0; index < normalBullets_.size();) {
		if (normalBullets_[index] && normalBullets_[index]->IsActive()) {
			++index;
			continue;
		}
		RecycleNormalBullet(index);
	}
}

void PlayerWeaponController::UpdateExplosiveBullets(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentExplosiveBulletShotPositions_.clear();
	const WeaponRuntimeStats runtime = stats.Resolve({
		10.0f + explosiveBulletDamageBonus_,
		explosiveBulletInterval_,
		explosiveBulletSpeed_,
		explosiveBulletRange_,
		1.0f,
		explosiveBulletCount_,
		}, GetWeaponStatApplicability(WeaponType::FlameStaff));
	if (hasExplosiveBullets_ && player) {
		explosiveBulletTimer_ += deltaTime;
		explosiveBurstTimer_ += deltaTime;
		int32_t catchUpAttackCount = 0;
		while (explosiveBulletTimer_ >= runtime.interval &&
			catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
			if (explosiveBurstShotsRemaining_ <= 0) {
				explosiveBurstShotsRemaining_ = runtime.projectileCount;
				explosiveBurstTimer_ = explosiveBurstInterval_;
			}
			explosiveBulletTimer_ -= runtime.interval;
			++catchUpAttackCount;
		}
		if (explosiveBulletTimer_ >= runtime.interval) {
			explosiveBulletTimer_ = std::fmod(
				explosiveBulletTimer_,
				runtime.interval);
		}

		const float effectiveBurstInterval =
			explosiveBurstInterval_ / stats.GetAttackSpeedMultiplier();
		if (explosiveBurstShotsRemaining_ > 0 &&
			explosiveBurstTimer_ >= effectiveBurstInterval) {
			const Vector3 direction = ResolveAimDirection(
				*player, enemyManager);
			const Vector3 spawnPosition = MakePlayerProjectileSpawnPosition(
				player->GetWorldPosition(),
				direction);
			NormalBullet& bullet = AcquireExplosiveBullet();
			bullet.InitializeForward(
				spawnPosition,
				direction,
				runtime.projectileSpeed,
				runtime.duration,
				1,
				runtime.areaSize,
				NormalBullet::MovementMode::Straight,
				kFlameStaffVisual);
			recentExplosiveBulletShotPositions_.push_back(
				spawnPosition + direction * 0.38f);
			--explosiveBurstShotsRemaining_;
			explosiveBurstTimer_ -= effectiveBurstInterval;
		}
	}

	for (std::unique_ptr<NormalBullet>& bullet : explosiveBullets_) {
		bullet->Update(
			player ? player->GetWorldPosition() : Vector3{},
			deltaTime);
	}
	RecycleInactiveExplosiveBullets();
}

void PlayerWeaponController::UpdateSword(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentSwordSlashes_.clear();
	if (!hasSword_ || !player || !enemyManager) {
		return;
	}
	swordTimer_ += deltaTime;
	const WeaponRuntimeStats runtime = stats.Resolve({
		11.0f + swordDamageBonus_,
		swordInterval_,
		1.0f,
		1.0f,
		swordRadius_,
		swordSlashCount_,
		}, GetWeaponStatApplicability(WeaponType::Sword));
	const float effectiveInterval =
		runtime.interval;
	if (swordTimer_ < effectiveInterval) {
		return;
	}
	const Vector3 direction = PlayerFacingDirection(*player);
	const Vector3 right{
		direction.z,
		0.0f,
		-direction.x,
	};
	const int32_t slashCount = runtime.projectileCount;
	const float centerOffset = static_cast<float>(slashCount - 1) * 0.5f;
	const float radius = runtime.areaSize;
	const int32_t damage = runtime.damage;
	for (int32_t index = 0; index < slashCount; ++index) {
		const int32_t directionSign = index % 2 == 0 ? 1 : -1;
		const int32_t sideStep = index / 2;
		const float spreadAngle =
			static_cast<float>(directionSign * sideStep) *
			(swordHalfAngle_ * 0.38f);
		const float cosine = std::cos(spreadAngle);
		const float sine = std::sin(spreadAngle);
		const Vector3 slashDirection{
			direction.x * cosine + right.x * sine,
			0.0f,
			direction.z * cosine + right.z * sine,
		};
		enemyManager->ApplyArcDamage(
			player->GetWorldPosition(),
			slashDirection,
			radius,
			swordHalfAngle_ * 0.55f,
			damage,
			swordKnockbackStrength_);
		recentSwordSlashes_.push_back({
			player->GetWorldPosition(),
			slashDirection,
			radius,
			swordHalfAngle_ * 0.55f,
			directionSign,
		});
	}
	PlayWeaponSound("se/weapon_sword.wav", "weapon.sword", 0.42f, 0.16f);
	swordTimer_ = std::fmod(swordTimer_, effectiveInterval);
}

void PlayerWeaponController::UpdateAura(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	auraPulseThisFrame_ = false;
	if (!hasAura_ || !player || !enemyManager) {
		return;
	}
	auraTimer_ += deltaTime;
	const WeaponRuntimeStats runtime = stats.Resolve({
		4.0f + auraDamageBonus_,
		auraInterval_,
		1.0f,
		1.0f,
		auraRadius_,
		1,
		}, GetWeaponStatApplicability(WeaponType::Aura));
	const float interval = runtime.interval;
	if (auraTimer_ < interval) {
		return;
	}
	enemyManager->ApplyAreaDamage(
		player->GetWorldPosition(),
		runtime.areaSize,
		runtime.damage);
	auraPulseThisFrame_ = true;
	PlayWeaponSound("se/weapon_aura.wav", "weapon.aura", 0.34f, 0.20f);
	auraTimer_ = std::fmod(auraTimer_, interval);
}

void PlayerWeaponController::UpdateFlameShoes(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	recentFlameZoneSpawns_.clear();
	flameZoneVisuals_.clear();
	if (!hasFlameShoes_ || !player || !enemyManager) {
		return;
	}
	const Vector3 playerPosition = player->GetWorldPosition();
	const WeaponRuntimeStats runtime = stats.Resolve({
		5.0f + flameShoesDamageBonus_,
		flameShoesDamageInterval_,
		1.0f,
		flameShoesZoneDuration_,
		flameShoesRadius_,
		flameShoesZoneCount_,
		}, GetWeaponStatApplicability(WeaponType::FlameShoes));
	if (!flameShoesPositionInitialized_) {
		flameShoesLastSpawnPosition_ = playerPosition;
		flameShoesPositionInitialized_ = true;
	}
	const float spawnDx = playerPosition.x - flameShoesLastSpawnPosition_.x;
	const float spawnDz = playerPosition.z - flameShoesLastSpawnPosition_.z;
	const float spawnDistanceSq = spawnDx * spawnDx + spawnDz * spawnDz;
	if (spawnDistanceSq >=
		flameShoesSpawnDistance_ * flameShoesSpawnDistance_) {
		Vector3 moveDirection{ spawnDx, 0.0f, spawnDz };
		const float moveLength = std::sqrt(spawnDistanceSq);
		if (moveLength > 0.0001f) {
			moveDirection.x /= moveLength;
			moveDirection.z /= moveLength;
			flameShoesLastDirection_ = moveDirection;
		} else {
			moveDirection = flameShoesLastDirection_;
		}
		const int32_t zoneCount = (std::max)(
			1,
			runtime.projectileCount);
		const float zoneDuration = runtime.duration;
		const float zoneSpacing =
			runtime.areaSize * 1.08f;
		const Vector3 trailCenter =
			playerPosition - moveDirection *
				(runtime.areaSize * 0.06f);
		for (int32_t index = 0; index < zoneCount; ++index) {
			if (flameZones_.size() >= kMaxFlameZones) {
				flameZones_.erase(flameZones_.begin());
			}
			const float backOffset =
				static_cast<float>(index) * zoneSpacing;
			const Vector3 zonePosition =
				trailCenter - moveDirection * backOffset;
			flameZones_.push_back({
				zonePosition,
				moveDirection,
				runtime.areaSize,
				zoneDuration,
				zoneDuration,
				runtime.interval,
				});
			recentFlameZoneSpawns_.push_back(zonePosition);
		}
		PlayWeaponSound("se/weapon_flame.wav", "weapon.flame", 0.40f, 0.16f);
		flameShoesLastSpawnPosition_ = playerPosition;
	}

	const float damageInterval = runtime.interval;
	for (FlameZone& zone : flameZones_) {
		zone.remainingDuration -= deltaTime;
		zone.damageTimer += deltaTime;
		const float lifeRatio = std::clamp(
			zone.remainingDuration / (std::max)(0.001f, zone.totalDuration),
			0.0f,
			1.0f);
		const float activeRadius = zone.radius *
			(0.55f + std::sqrt(lifeRatio) * 0.45f);
		flameZoneVisuals_.push_back({
			zone.position,
			zone.direction,
			activeRadius,
			zone.remainingDuration,
			zone.totalDuration,
			});
		if (zone.damageTimer < damageInterval) {
			continue;
		}
		enemyManager->ApplyAreaDamage(
			zone.position,
			activeRadius,
			runtime.damage);
		zone.damageTimer = std::fmod(zone.damageTimer, damageInterval);
	}
	std::erase_if(flameZones_, [](const FlameZone& zone) {
		return zone.remainingDuration <= 0.0f;
	});
}

void PlayerWeaponController::UpdateBone(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	boneWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

void PlayerWeaponController::UpdateHandgun(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	handgunWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

void PlayerWeaponController::UpdateBoomerang(
	float deltaTime, Player* player, EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (!player) return;
	boomerangWeapon_.Update(deltaTime, player->GetWorldPosition(),
		ResolveAimDirection(*player, enemyManager), stats);
}

Vector3 PlayerWeaponController::ResolveAimDirection(
	const Player& player,
	const EnemyManager* enemyManager) const
{
	const Vector3 origin = player.GetWorldPosition();
	Vector3 target{};
	if (enemyManager &&
		enemyManager->FindNearestEnemyPosition(origin, 1000.0f, target)) {
		const float deltaX = target.x - origin.x;
		const float deltaZ = target.z - origin.z;
		const float length = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
		if (length > 0.0001f) {
			return { deltaX / length, 0.0f, deltaZ / length };
		}
	}
	const float angle = player.GetWorldRotationY();
	return { std::sin(angle), 0.0f, std::cos(angle) };
}

NormalBullet& PlayerWeaponController::AcquireExplosiveBullet()
{
	if (explosiveBulletPool_.empty()) {
		explosiveBullets_.push_back(std::make_unique<NormalBullet>());
		return *explosiveBullets_.back();
	}

	explosiveBullets_.push_back(std::move(explosiveBulletPool_.back()));
	explosiveBulletPool_.pop_back();
	return *explosiveBullets_.back();
}

void PlayerWeaponController::RecycleExplosiveBullet(size_t index)
{
	if (index >= explosiveBullets_.size()) {
		return;
	}

	if (explosiveBullets_[index]) {
		explosiveBullets_[index]->Deactivate();
		explosiveBulletPool_.push_back(std::move(explosiveBullets_[index]));
	}
	if (index != explosiveBullets_.size() - 1) {
		explosiveBullets_[index] = std::move(explosiveBullets_.back());
	}
	explosiveBullets_.pop_back();
}

void PlayerWeaponController::RecycleInactiveExplosiveBullets()
{
	for (size_t index = 0; index < explosiveBullets_.size();) {
		if (explosiveBullets_[index] && explosiveBullets_[index]->IsActive()) {
			++index;
			continue;
		}
		RecycleExplosiveBullet(index);
	}
}

void PlayerWeaponController::UpdateOrbitBullets(
	float deltaTime,
	Player* player,
	const PlayerStats& stats)
{
	if (!hasOrbitBullets_ || !player) {
		return;
	}
	const WeaponStatApplicability applicability =
		GetWeaponStatApplicability(WeaponType::Rock);
	const int32_t projectileCountBonus =
		applicability.projectileCount ? stats.GetProjectileCountBonus() : 0;
	const int32_t effectiveCount = (std::max)(
		1, orbitBulletCount_ + projectileCountBonus);
	if (orbitBullets_.size() != static_cast<size_t>(effectiveCount)) {
		RebuildOrbitBullets(player, projectileCountBonus);
	}
	for (std::unique_ptr<OrbitBullet>& bullet : orbitBullets_) {
		bullet->ApplyRuntimeModifiers(
			applicability.projectileSpeed
				? stats.GetProjectileSpeedMultiplier()
				: 1.0f,
			applicability.areaSize
				? stats.GetAreaSizeMultiplier()
				: 1.0f,
			applicability.attackSpeed
				? stats.GetAttackSpeedMultiplier()
				: 1.0f);
		bullet->Update(player->GetWorldPosition(), deltaTime);
	}
}

void PlayerWeaponController::UpdateLightning(
	float deltaTime,
	Player* player,
	EnemyManager* enemyManager,
	const PlayerStats& stats)
{
	if (lightningEffectTimer_ > 0.0f) {
		lightningEffectTimer_ = (std::max)(
			0.0f,
			lightningEffectTimer_ - deltaTime);
		if (lightningEffectTimer_ <= 0.0f) {
			lightningEffectTargets_.clear();
		}
	}
	if (!hasLightning_ || !player || !enemyManager) {
		return;
	}

	lightningTimer_ += deltaTime;
	int32_t catchUpAttackCount = 0;
	const WeaponStatApplicability applicability =
		GetWeaponStatApplicability(WeaponType::ThunderStaff);
	const float attackSpeedMultiplier =
		applicability.attackSpeed ? stats.GetAttackSpeedMultiplier() : 1.0f;
	const float areaSizeMultiplier =
		applicability.areaSize ? stats.GetAreaSizeMultiplier() : 1.0f;
	const float durationMultiplier =
		applicability.duration ? stats.GetDurationMultiplier() : 1.0f;
	const int32_t projectileCountBonus =
		applicability.projectileCount ? stats.GetProjectileCountBonus() : 0;
	const float effectiveInterval =
		lightningInterval_ / attackSpeedMultiplier;
	while (lightningTimer_ >= effectiveInterval &&
		catchUpAttackCount < kMaxCatchUpAttacksPerFrame) {
		const std::vector<Vector3> targets =
			enemyManager->PickLightningChainTargets(
				player->GetWorldPosition(),
				lightningStrikeCount_ + projectileCountBonus,
				lightningRadius_ * areaSizeMultiplier);
		if (!targets.empty()) {
			lightningEffectTargets_ = targets;
			lightningEffectTimer_ =
				0.22f * durationMultiplier;
			PlayWeaponSound(
				"se/weapon_lightning.wav",
				"weapon.lightning",
				0.48f,
				0.16f);
		}
		for (const Vector3& target : targets) {
			enemyManager->ApplyLightningDamage(
				target,
				lightningImpactRadius_ * areaSizeMultiplier,
				GetLightningDamage(stats));
		}
		lightningTimer_ -= effectiveInterval;
		++catchUpAttackCount;
		if (targets.empty()) {
			break;
		}
	}
	if (lightningTimer_ >= effectiveInterval) {
		lightningTimer_ = std::fmod(
			lightningTimer_,
			effectiveInterval);
	}
}

void PlayerWeaponController::RebuildOrbitBullets(
	Player* player,
	int32_t projectileCountBonus)
{
	if (!player) {
		orbitBullets_.clear();
		return;
	}
	orbitBullets_.clear();
	const int32_t effectiveCount = (std::max)(
		1, orbitBulletCount_ + projectileCountBonus);
	orbitBullets_.reserve(effectiveCount);
	for (int32_t index = 0; index < effectiveCount; ++index) {
		const float angle =
			(2.0f * std::numbers::pi_v<float> *
				static_cast<float>(index)) /
			static_cast<float>(effectiveCount);
		auto bullet = std::make_unique<OrbitBullet>();
		bullet->Initialize(
			player->GetWorldPosition(),
			orbitRadius_,
			angle,
			orbitAngularSpeed_,
			orbitBulletScale_,
			orbitHitInterval_);
		orbitBullets_.push_back(std::move(bullet));
	}
}

void PlayerWeaponController::ApplyNormalBulletUpgradeLevel(int32_t level)
{
	normalBulletAmount_ = GetLevelUpgradeSettingInt(
		"bowArrow",
		level,
		"amount",
		normalBulletAmount_);
	normalBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"bowArrow",
		level,
		"damageBonusAdd",
		0);
	normalBulletPierceCount_ = GetLevelUpgradeSettingInt(
		"bowArrow",
		level,
		"pierceCount",
		normalBulletPierceCount_);
	normalBulletSpeed_ *= GetLevelUpgradeSetting(
		"bowArrow",
		level,
		"speedMultiplier",
		1.0f);
	normalBulletInterval_ *= GetLevelUpgradeSetting(
		"bowArrow",
		level,
		"intervalMultiplier",
		1.0f);
	normalBulletScale_ *= GetLevelUpgradeSetting(
		"bowArrow", level, "scaleMultiplier", 1.0f);
}

void PlayerWeaponController::ApplyOrbitUpgradeLevel(int32_t level)
{
	orbitBulletCount_ = GetLevelUpgradeSettingInt(
		"rock", level, "count", orbitBulletCount_);
	orbitDamageBonus_ += GetLevelUpgradeSettingInt(
		"rock", level, "damageBonusAdd", 0);
	orbitAngularSpeed_ *= GetLevelUpgradeSetting(
		"rock", level, "speedMultiplier", 1.0f);
	orbitBulletScale_ *= GetLevelUpgradeSetting(
		"rock", level, "scaleMultiplier", 1.0f);
	orbitHitInterval_ = GetLevelUpgradeSetting(
		"rock", level, "hitInterval", orbitHitInterval_);
	orbitHitInterval_ *= GetLevelUpgradeSetting(
		"rock", level, "hitIntervalMultiplier", 1.0f);
}

void PlayerWeaponController::ApplyLightningUpgradeLevel(int32_t level)
{
	lightningStrikeCount_ = GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"strikeCount",
		lightningStrikeCount_);
	lightningDamageBonus_ = GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"damageBonus",
		lightningDamageBonus_);
	lightningDamageBonus_ += GetLevelUpgradeSettingInt(
		"thunderStaff",
		level,
		"damageBonusAdd",
		0);
	lightningRadius_ = GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"radius",
		lightningRadius_);
	lightningRadius_ += GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"radiusAdd",
		0.0f);
	lightningImpactRadius_ = GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"impactRadius",
		lightningImpactRadius_);
	lightningImpactRadius_ += GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"impactRadiusAdd",
		0.0f);
	lightningInterval_ = GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"interval",
		lightningInterval_);
	lightningInterval_ *= GetLevelUpgradeSetting(
		"thunderStaff",
		level,
		"intervalMultiplier",
		1.0f);
	lightningInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(lightningInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyExplosiveBulletUpgradeLevel(int32_t level)
{
	explosiveBulletDamageBonus_ = GetLevelUpgradeSettingInt(
		"flameStaff",
		level,
		"damageBonus",
		explosiveBulletDamageBonus_);
	explosiveBulletDamageBonus_ += GetLevelUpgradeSettingInt(
		"flameStaff",
		level,
		"damageBonusAdd",
		0);
	explosiveBulletRadius_ = GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"radius",
		explosiveBulletRadius_);
	explosiveBulletRadius_ += GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"radiusAdd",
		0.0f);
	explosiveBulletInterval_ = GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"interval",
		explosiveBulletInterval_);
	explosiveBulletInterval_ *= GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"intervalMultiplier",
		1.0f);
	explosiveBulletSpeed_ *= GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"speedMultiplier",
		1.0f);
	explosiveBulletRange_ += GetLevelUpgradeSetting(
		"flameStaff",
		level,
		"rangeAdd",
		0.0f);
	explosiveBulletInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(explosiveBulletInterval_, kMinWeaponInterval));
	explosiveBulletCount_ = GetLevelUpgradeSettingInt(
		"flameStaff", level, "count", explosiveBulletCount_);
}

void PlayerWeaponController::ApplySwordUpgradeLevel(int32_t level)
{
	swordDamageBonus_ += GetLevelUpgradeSettingInt(
		"sword", level, "damageBonusAdd", 0);
	swordSlashCount_ = GetLevelUpgradeSettingInt(
		"sword", level, "count", swordSlashCount_);
	swordRadius_ += GetLevelUpgradeSetting(
		"sword", level, "radiusAdd", 0.0f);
	swordHalfAngle_ += GetLevelUpgradeSetting(
		"sword", level, "halfAngleAdd", 0.0f);
	swordKnockbackStrength_ += GetLevelUpgradeSetting(
		"sword", level, "knockbackAdd", 0.0f);
	swordInterval_ *= GetLevelUpgradeSetting(
		"sword", level, "intervalMultiplier", 1.0f);
	swordInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(swordInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyAuraUpgradeLevel(int32_t level)
{
	auraDamageBonus_ += GetLevelUpgradeSettingInt(
		"aura", level, "damageBonusAdd", 0);
	auraRadius_ += GetLevelUpgradeSetting(
		"aura", level, "radiusAdd", 0.0f);
	auraInterval_ *= GetLevelUpgradeSetting(
		"aura", level, "intervalMultiplier", 1.0f);
	auraInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(auraInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ApplyFlameShoesUpgradeLevel(int32_t level)
{
	flameShoesDamageBonus_ += GetLevelUpgradeSettingInt(
		"flameShoes", level, "damageBonusAdd", 0);
	flameShoesZoneCount_ = GetLevelUpgradeSettingInt(
		"flameShoes", level, "count", flameShoesZoneCount_);
	flameShoesRadius_ += GetLevelUpgradeSetting(
		"flameShoes", level, "radiusAdd", 0.0f);
	flameShoesZoneDuration_ += GetLevelUpgradeSetting(
		"flameShoes", level, "durationAdd", 0.0f);
	flameShoesDamageInterval_ *= GetLevelUpgradeSetting(
		"flameShoes", level, "intervalMultiplier", 1.0f);
	flameShoesSpawnDistance_ *= GetLevelUpgradeSetting(
		"flameShoes", level, "spawnDistanceMultiplier", 1.0f);
	flameShoesDamageInterval_ = (std::max)(
		kMinWeaponInterval,
		PositiveFiniteOr(flameShoesDamageInterval_, kMinWeaponInterval));
}

void PlayerWeaponController::ResetNormalBulletWeapon()
{
	normalBullets_.clear();
	normalBulletPool_.clear();
	hasNormalBullets_ = false;
	normalBulletInterval_ = 0.85f;
	normalBulletTimer_ = 0.0f;
	normalBulletLevel_ = 0;
	normalBulletAmount_ = 1;
	normalBulletDamageBonus_ = 0;
	normalBulletPierceCount_ = 1;
	normalBulletSpeed_ = 1.5f;
	normalBulletRange_ = 30.0f;
	normalBulletScale_ = 1.0f;
	peakNormalBulletCount_ = 0;
	normalBulletPruneCount_ = 0;
	normalBulletMinInterval_ = 0.18f;
}

void PlayerWeaponController::ResetOrbitWeapon()
{
	orbitBullets_.clear();
	hasOrbitBullets_ = false;
	orbitBulletLevel_ = 0;
	orbitBulletCount_ = 1;
	orbitDamageBonus_ = 0;
	orbitRadius_ = 10.0f;
	orbitRadiusUpgradeStep_ = 2.0f;
	orbitAngularSpeed_ = 0.03f;
	orbitAngularSpeedUpgradeStep_ = 0.01f;
	orbitBulletScale_ = 1.0f;
	orbitBulletScaleUpgradeStep_ = 0.2f;
	orbitHitInterval_ = 0.5f;
	orbitHitIntervalUpgradeMultiplier_ = 0.8f;
}

void PlayerWeaponController::ResetLightningWeapon()
{
	hasLightning_ = false;
	lightningLevel_ = 0;
	lightningStrikeCount_ = 1;
	lightningDamageBonus_ = 0;
	lightningRadius_ = 6.0f;
	lightningImpactRadius_ = 1.45f;
	lightningInterval_ = 2.4f;
	lightningTimer_ = 0.0f;
	lightningEffectTargets_.clear();
	lightningEffectTimer_ = 0.0f;
}

void PlayerWeaponController::ResetExplosiveWeapon()
{
	explosiveBullets_.clear();
	explosiveBulletPool_.clear();
	hasExplosiveBullets_ = false;
	explosiveBulletLevel_ = 0;
	explosiveBulletDamageBonus_ = 4;
	explosiveBulletInterval_ = 2.0f;
	explosiveBulletTimer_ = 0.0f;
	explosiveBulletSpeed_ = 0.5f;
	explosiveBulletRange_ = 28.0f;
	explosiveBulletRadius_ = 4.2f;
	explosiveBulletCount_ = 1;
	explosiveBurstShotsRemaining_ = 0;
	explosiveBurstTimer_ = 0.0f;
	explosiveBurstInterval_ = GetUpgradeSetting(
		"flameStaff.burstInterval",
		0.14f);
}

void PlayerWeaponController::ResetSwordWeapon()
{
	hasSword_ = false;
	swordLevel_ = 0;
	swordDamageBonus_ = 0;
	swordInterval_ = 1.15f;
	swordTimer_ = 0.0f;
	swordRadius_ = 7.0f;
	swordHalfAngle_ = 0.9f;
	swordSlashCount_ = 1;
	swordKnockbackStrength_ = 1.3f;
	recentSwordSlashes_.clear();
}

void PlayerWeaponController::ResetAuraWeapon()
{
	hasAura_ = false;
	auraLevel_ = 0;
	auraDamageBonus_ = 0;
	auraInterval_ = 0.32f;
	auraTimer_ = 0.0f;
	auraRadius_ = 6.0f;
	auraPulseThisFrame_ = false;
}

void PlayerWeaponController::ResetFlameShoesWeapon()
{
	hasFlameShoes_ = false;
	flameShoesLevel_ = 0;
	flameShoesDamageBonus_ = 0;
	flameShoesZoneDuration_ = 1.5f;
	flameShoesDamageInterval_ = 0.6f;
	flameShoesRadius_ = 3.2f;
	flameShoesSpawnDistance_ = 7.0f;
	flameShoesZoneCount_ = 1;
	flameShoesLastSpawnPosition_ = {};
	flameShoesLastDirection_ = { 0.0f, 0.0f, 1.0f };
	flameShoesPositionInitialized_ = false;
	flameZones_.clear();
	flameZoneVisuals_.clear();
	recentFlameZoneSpawns_.clear();
}

int32_t PlayerWeaponController::GetLevelUpgradeSettingInt(
	const std::string& prefix,
	int32_t level,
	const std::string& suffix,
	int32_t fallback) const
{
	return static_cast<int32_t>(GetLevelUpgradeSetting(
		prefix,
		level,
		suffix,
		static_cast<float>(fallback)));
}

float PlayerWeaponController::GetLevelUpgradeSetting(
	const std::string& prefix,
	int32_t level,
	const std::string& suffix,
	float fallback) const
{
	return GetUpgradeSetting(
		prefix + ".lv" + std::to_string(level) + "." + suffix,
		fallback);
}

float PlayerWeaponController::GetUpgradeSetting(
	const std::string& key,
	float fallback) const
{
	const auto it = upgradeSettings_.find(key);
	return it == upgradeSettings_.end() ? fallback : it->second;
}

}
