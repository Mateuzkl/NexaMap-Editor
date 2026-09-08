//////////////////////////////////////////////////////////////////////
// Source-preserving NPC model for XML and Lua Server Workspaces.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_NPC_DEFINITION_H_
#define NEXAMAP_NPC_DEFINITION_H_

#include "server_content_index.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

enum class NpcField : uint8_t {
	Name = 0,
	Description,
	Script,
	Health,
	MaxHealth,
	WalkInterval,
	WalkRadius,
	Speed,
	FloorChange,
	LookType,
	LookTypeEx,
	LookHead,
	LookBody,
	LookLegs,
	LookFeet,
	LookAddons,
	LookMount,
	Direction,
	Count,
};

struct NpcFieldCapability {
	enum class State : uint8_t {
		Unsupported = 0,
		ExistingEditableLiteral,
		SupportedInsertable,
		DynamicReadOnly,
		Ambiguous,
	};

	State state = State::Unsupported;
	bool present = false;
	bool editable = false;
	std::string limitation;

	[[nodiscard]] bool insertable() const {
		return state == State::SupportedInsertable;
	}
};

struct NpcMessage {
	std::string key;
	std::string text;
	bool editable = false;
	friend bool operator==(const NpcMessage&, const NpcMessage&) = default;
};

struct NpcShopEntry {
	std::string itemName;
	int itemId = 0;
	int buy = 0;
	int sell = 0;
	bool editable = false;
	friend bool operator==(const NpcShopEntry&, const NpcShopEntry&) = default;
};

struct NpcTravelEntry {
	std::string keyword;
	int cost = 0;
	int x = 0;
	int y = 0;
	int z = 0;
	bool premium = false;
	bool editable = false;
	friend bool operator==(const NpcTravelEntry&, const NpcTravelEntry&) = default;
};

struct NpcDefinition {
	std::string name;
	std::string description;
	std::string script;
	int health = 100;
	int maxHealth = 100;
	int walkInterval = 2000;
	int walkRadius = 2;
	int speed = 0;
	bool floorChange = false;
	int lookType = 128;
	int lookTypeEx = 0;
	int lookHead = 0;
	int lookBody = 0;
	int lookLegs = 0;
	int lookFeet = 0;
	int lookAddons = 0;
	int lookMount = 0;
	int direction = 2;
	std::vector<NpcMessage> messages;
	std::vector<NpcShopEntry> shop;
	std::vector<NpcTravelEntry> travel;
	std::vector<std::string> behaviorNotes;
	std::array<NpcFieldCapability, static_cast<std::size_t>(NpcField::Count)> capabilities {};

	[[nodiscard]] const NpcFieldCapability& capability(NpcField field) const;
};

class NpcDefinitionDocument {
public:
	struct Impl;
	~NpcDefinitionDocument();
	NpcDefinitionDocument(NpcDefinitionDocument&&) noexcept;
	NpcDefinitionDocument& operator=(NpcDefinitionDocument&&) noexcept;
	NpcDefinitionDocument(const NpcDefinitionDocument&) = delete;
	NpcDefinitionDocument& operator=(const NpcDefinitionDocument&) = delete;

	[[nodiscard]] static std::unique_ptr<NpcDefinitionDocument> Load(const ServerContentSource& source, std::string& error);
	[[nodiscard]] const NpcDefinition& definition() const;
	[[nodiscard]] const ServerContentSource& source() const;
	[[nodiscard]] const std::string& sourceText() const;
	[[nodiscard]] bool hasChanges(const NpcDefinition& edited) const;
	[[nodiscard]] bool save(const NpcDefinition& edited, std::string& error);

private:
	[[nodiscard]] static std::unique_ptr<NpcDefinitionDocument> LoadFromText(const ServerContentSource& source, std::string bytes, std::string& error);
	explicit NpcDefinitionDocument(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> implementation;
};

[[nodiscard]] const char* NpcFieldName(NpcField field);
[[nodiscard]] bool ValidateNpcDefinition(const NpcDefinition& definition, std::string& error);

#endif // NEXAMAP_NPC_DEFINITION_H_
