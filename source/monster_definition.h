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
	std::array<MonsterFieldCapability, static_cast<std::size_t>(MonsterField::Count)> capabilities;

	[[nodiscard]] const MonsterFieldCapability& capability(MonsterField field) const;
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
	explicit MonsterDefinitionDocument(std::unique_ptr<Impl> implementation);

	std::unique_ptr<Impl> implementation;
};

[[nodiscard]] const char* MonsterFieldName(MonsterField field);

#endif // NEXAMAP_MONSTER_DEFINITION_H_
