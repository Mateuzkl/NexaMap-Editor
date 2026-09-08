//////////////////////////////////////////////////////////////////////
// Source-preserving global spell model for Lua and XML workspaces.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_DEFINITION_H_
#define NEXAMAP_SPELL_DEFINITION_H_

#include "monster_definition.h"
#include "monster_spell_area.h"
#include "server_content_index.h"
#include "spell_area_resolver.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class SpellField : uint8_t {
	Name = 0,
	Words,
	Group,
	Script,
	SpellId,
	RuneId,
	Level,
	MagicLevel,
	Mana,
	ManaPercent,
	Soul,
	Cooldown,
	GroupCooldown,
	Range,
	Charges,
	Premium,
	Enabled,
	Aggressive,
	NeedTarget,
	NeedDirection,
	BlockWalls,
	AllowFarUse,
	NeedLearn,
	SelfTarget,
	CombatType,
	Effect,
	Projectile,
	Area,
	Count,
};

struct SpellFieldCapability {
	bool present = false;
	bool editable = false;
	std::string limitation;
};

enum class SpellVocationCapabilityState : uint8_t {
	Unsupported = 0,
	ExistingEditableLiteral,
	SupportedInsertable,
	DynamicReadOnly,
	Ambiguous,
};

struct SpellVocationCapability {
	SpellVocationCapabilityState state = SpellVocationCapabilityState::Unsupported;
	bool editable = false;
	std::string limitation;
};

struct SpellDefinition {
	std::string name;
	std::string subtype;
	std::string words;
	std::string group;
	std::string script;
	int spellId = 0;
	int runeId = 0;
	int level = 0;
	int magicLevel = 0;
	int mana = 0;
	int manaPercent = 0;
	int soul = 0;
	int cooldown = 0;
	int groupCooldown = 0;
	int range = 0;
	int charges = 0;
	bool premium = false;
	bool enabled = true;
	bool aggressive = true;
	bool needTarget = false;
	bool needDirection = false;
	bool blockWalls = false;
	bool allowFarUse = false;
	bool needLearn = false;
	bool selfTarget = false;
	std::string combatType;
	std::string effect;
	std::string projectile;
	std::string areaExpression;
	std::string areaStatus;
	std::vector<std::string> vocations;
	bool allVocations = true;
	SpellVocationCapability vocationCapability;
	MonsterAttackDefinition preview;
	std::vector<MonsterAreaTile> customAreaTiles;
	SpellAreaResolutionState areaResolutionState = SpellAreaResolutionState::Single;
	std::array<SpellFieldCapability, static_cast<std::size_t>(SpellField::Count)> capabilities {};

	[[nodiscard]] const SpellFieldCapability& capability(SpellField field) const;
};

class SpellDefinitionDocument {
public:
	static std::unique_ptr<SpellDefinitionDocument> Load(const ServerContentSource& source, std::string& error);
	static std::unique_ptr<SpellDefinitionDocument> Load(const ServerContentSource& source, std::string& error, const ServerWorkspace* workspace);
	static std::unique_ptr<SpellDefinitionDocument> Load(
		const ServerContentSource& source,
		std::string& error,
		const ServerWorkspace* workspace,
		std::shared_ptr<const SpellAreaResolver> areaResolver
	);

	~SpellDefinitionDocument();
	SpellDefinitionDocument(SpellDefinitionDocument&&) noexcept;
	SpellDefinitionDocument& operator=(SpellDefinitionDocument&&) noexcept;
	SpellDefinitionDocument(const SpellDefinitionDocument&) = delete;
	SpellDefinitionDocument& operator=(const SpellDefinitionDocument&) = delete;

	[[nodiscard]] const SpellDefinition& definition() const;
	[[nodiscard]] const ServerContentSource& source() const;
	[[nodiscard]] const std::string& declarationText() const;
	[[nodiscard]] const std::string& implementationText() const;
	[[nodiscard]] const std::filesystem::path& implementationPath() const;
	[[nodiscard]] bool hasSeparateImplementation() const;
	[[nodiscard]] bool hasChanges(const SpellDefinition& edited) const;
	bool save(const SpellDefinition& edited, std::string& error);

private:
	struct Impl;
	static std::unique_ptr<SpellDefinitionDocument> LoadFromText(
		const ServerContentSource& source,
		std::vector<std::string> files,
		std::string& error,
		const ServerWorkspace* workspace,
		std::shared_ptr<const SpellAreaResolver> areaResolver
	);
	explicit SpellDefinitionDocument(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> implementation;
};

[[nodiscard]] const char* SpellFieldName(SpellField field);
[[nodiscard]] bool ValidateSpellDefinition(const SpellDefinition& definition, std::string& error);

#endif // NEXAMAP_SPELL_DEFINITION_H_
