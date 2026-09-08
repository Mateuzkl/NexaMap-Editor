//////////////////////////////////////////////////////////////////////
// Source-preserving monster definition model for XML and Lua servers.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_DEFINITION_H_
#define NEXAMAP_MONSTER_DEFINITION_H_

#include "server_content_index.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class MonsterField : uint8_t {
	Name = 0,
	Description,
	Race,
	Health,
	MaxHealth,
	Experience,
	Speed,
	Armor,
	Defense,
	TargetDistance,
	Corpse,
	ManaCost,
	Skull,
	TargetChangeInterval,
	TargetChangeChance,
	StrategyAttack,
	StrategyDefense,
	Summonable,
	Convinceable,
	Attackable,
	Hostile,
	Pushable,
	CanPushItems,
	CanPushCreatures,
	StaticAttack,
	LightLevel,
	LightColor,
	RunOnHealth,
	LookType,
	LookTypeEx,
	LookHead,
	LookBody,
	LookLegs,
	LookFeet,
	LookAddons,
	LookMount,
	Count,
};

struct MonsterFieldCapability {
	bool present = false;
	bool editable = false;
	std::string limitation;
};

struct MonsterOutfitDefinition {
	int lookType = 0;
	int lookTypeEx = 0;
	int head = 0;
	int body = 0;
	int legs = 0;
	int feet = 0;
	int addons = 0;
	int mount = 0;

	friend bool operator==(const MonsterOutfitDefinition&, const MonsterOutfitDefinition&) = default;
};

struct MonsterCustomProperty {
	std::string name;
	std::string value;
	bool rawValue = false;

	friend bool operator==(const MonsterCustomProperty&, const MonsterCustomProperty&) = default;
};

enum class MonsterAreaShape : uint8_t {
	Single = 0,
	Target,
	Radius,
	Ring,
	Beam,
};

struct MonsterAttackArea {
	MonsterAreaShape shape = MonsterAreaShape::Single;
	int range = 0;
	int radius = 0;
	int ring = 0;
	int length = 0;
	int spread = 0;
	bool target = false;

	friend bool operator==(const MonsterAttackArea&, const MonsterAttackArea&) = default;
};

struct MonsterAttackDefinition {
	std::string name;
	std::string type;
	int interval = 2000;
	int chance = 100;
	int minDamage = 0;
	int maxDamage = 0;
	int skill = 0;
	int attack = 0;
	MonsterAttackArea area;
	std::string effect;
	std::string projectile;
	std::vector<MonsterCustomProperty> customProperties;
	std::string preservedChildren;
	std::string annotation;

	friend bool operator==(const MonsterAttackDefinition&, const MonsterAttackDefinition&) = default;
};

struct MonsterDefenseAction {
	std::string name;
	std::string type;
	int interval = 2000;
	int chance = 100;
	int minDamage = 0;
	int maxDamage = 0;
	std::string effect;
	bool target = false;
	std::vector<MonsterCustomProperty> customProperties;
	std::string preservedChildren;

	friend bool operator==(const MonsterDefenseAction&, const MonsterDefenseAction&) = default;
};

struct MonsterResistance {
	std::string type;
	int percent = 0;
	std::vector<MonsterCustomProperty> customProperties;

	friend bool operator==(const MonsterResistance&, const MonsterResistance&) = default;
};

struct MonsterImmunity {
	std::string type;
	bool combat = false;
	bool condition = true;
	bool usesCombat = false;
	bool usesCondition = true;
	std::vector<MonsterCustomProperty> customProperties;

	friend bool operator==(const MonsterImmunity&, const MonsterImmunity&) = default;
};

struct MonsterLootEntry {
	int itemId = 0;
	std::string itemName;
	bool usesName = false;
	int chance = 100000;
	int maxCount = 1;
	int subtype = 0;
	int actionId = 0;
	std::string text;
	std::vector<MonsterCustomProperty> customProperties;
	std::vector<MonsterLootEntry> children;
	std::string annotation;

	friend bool operator==(const MonsterLootEntry&, const MonsterLootEntry&) = default;
};

struct MonsterSummon {
	std::string name;
	int interval = 2000;
	int chance = 100;
	int max = 0;
	bool force = false;
	std::vector<MonsterCustomProperty> customProperties;

	friend bool operator==(const MonsterSummon&, const MonsterSummon&) = default;
};

struct MonsterVoice {
	std::string text;
	bool yell = false;
	std::vector<MonsterCustomProperty> customProperties;

	friend bool operator==(const MonsterVoice&, const MonsterVoice&) = default;
};

struct MonsterVoiceDefinition {
	int interval = 5000;
	int chance = 10;
	std::vector<MonsterVoice> entries;
	std::vector<MonsterCustomProperty> customProperties;

	friend bool operator==(const MonsterVoiceDefinition&, const MonsterVoiceDefinition&) = default;
};

enum class MonsterSection : uint8_t {
	Defenses = 0,
	Resistances,
	Immunities,
	Loot,
	Summons,
	Voices,
	Attacks,
	Count,
};

struct MonsterSectionCapability {
	bool present = false;
	bool editable = false;
	std::string limitation;
};

struct MonsterDefinition {
	std::string name;
	std::string description;
	std::string race;
	int health = 0;
	int maxHealth = 0;
	int experience = 0;
	int speed = 0;
	int armor = 0;
	int defense = 0;
	int targetDistance = 0;
	int corpse = 0;
	int manaCost = 0;
	std::string skull;
	int targetChangeInterval = 0;
	int targetChangeChance = 0;
	int strategyAttack = 0;
	int strategyDefense = 0;
	bool summonable = false;
	bool convinceable = false;
	bool attackable = false;
	bool hostile = false;
	bool pushable = false;
	bool canPushItems = false;
	bool canPushCreatures = false;
	int staticAttack = 0;
	int lightLevel = 0;
	int lightColor = 0;
	int runOnHealth = 0;
	MonsterOutfitDefinition outfit;
	std::vector<MonsterCustomProperty> defenseProperties;
	std::vector<MonsterDefenseAction> defenseActions;
	std::vector<MonsterResistance> resistances;
	std::vector<MonsterImmunity> immunities;
	std::vector<MonsterCustomProperty> lootProperties;
	std::vector<MonsterLootEntry> loot;
	int maxSummons = 0;
	std::vector<MonsterCustomProperty> summonProperties;
	std::vector<MonsterSummon> summons;
	MonsterVoiceDefinition voices;
	std::vector<MonsterCustomProperty> attackProperties;
	std::vector<MonsterAttackDefinition> attacks;
	std::array<MonsterFieldCapability, static_cast<std::size_t>(MonsterField::Count)> capabilities;
	std::array<MonsterSectionCapability, static_cast<std::size_t>(MonsterSection::Count)> sectionCapabilities;

	[[nodiscard]] const MonsterFieldCapability& capability(MonsterField field) const;
	[[nodiscard]] const MonsterSectionCapability& capability(MonsterSection section) const;
};

class MonsterDefinitionDocument {
public:
	static std::unique_ptr<MonsterDefinitionDocument> Load(const ServerContentSource& source, std::string& error);

	~MonsterDefinitionDocument();
	MonsterDefinitionDocument(MonsterDefinitionDocument&&) noexcept;
	MonsterDefinitionDocument& operator=(MonsterDefinitionDocument&&) noexcept;
	MonsterDefinitionDocument(const MonsterDefinitionDocument&) = delete;
	MonsterDefinitionDocument& operator=(const MonsterDefinitionDocument&) = delete;

	[[nodiscard]] const MonsterDefinition& definition() const;
	[[nodiscard]] const ServerContentSource& source() const;
	[[nodiscard]] const std::string& sourceText() const;
	[[nodiscard]] bool hasChanges(const MonsterDefinition& edited) const;
	bool save(const MonsterDefinition& edited, std::string& error);

private:
	struct Impl;
	static std::unique_ptr<MonsterDefinitionDocument> LoadFromText(const ServerContentSource& source, std::vector<std::string> files, std::string& error);
	explicit MonsterDefinitionDocument(std::unique_ptr<Impl> implementation);

	std::unique_ptr<Impl> implementation;
};

[[nodiscard]] const char* MonsterFieldName(MonsterField field);
[[nodiscard]] const char* MonsterSectionName(MonsterSection section);
[[nodiscard]] bool ValidateMonsterDefinition(const MonsterDefinition& definition, std::string& error);

#endif // NEXAMAP_MONSTER_DEFINITION_H_
