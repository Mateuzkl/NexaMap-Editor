#include "npc_definition.h"
#include "npc_definition_creation.h"
#include "server_content_index.h"
#include "server_workspace.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
	int failures = 0;
	int checks = 0;

	void Check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	class TemporaryDirectory {
	public:
		TemporaryDirectory() {
			path = std::filesystem::temp_directory_path() / ("nexamap-npc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(path);
		}
		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
		std::filesystem::path write(const std::string& relative, const std::string& bytes) {
			const auto target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream(target, std::ios::binary) << bytes;
			return target;
		}
		std::string read(const std::string& relative) const {
			std::ifstream stream(path / relative, std::ios::binary);
			return { std::istreambuf_iterator<char>(stream), {} };
		}
		std::filesystem::path path;
	};

	ServerContentSource Source(ServerContentFormat format, const std::filesystem::path& path, std::string name) {
		ServerContentSource source;
		source.kind = ServerContentKind::Npc;
		source.format = format;
		source.name = std::move(name);
		source.declarationPath = path;
		source.declarationExists = true;
		source.registered = true;
		source.declarationFingerprint = ResourceFingerprint::Read(path);
		return source;
	}

	void TestXml() {
		TemporaryDirectory directory;
		const auto path = directory.write("data/npc/trader.xml", R"(<?xml version="1.0"?>
<!-- keep -->
<npc name="Trader" script="trader.lua" custom="preserve" walkinterval="25" speed="10" floorchange="0" lookdir="2">
 <health now="100" max="100"/>
 <look type="128" head="1" body="2" legs="3" feet="4" addons="0" mount="0"/>
 <parameters>
  <parameter key="message_greet" value="Hello |PLAYERNAME|"/>
  <parameter key="shop_sellable" value="gold coin,2148,1; honeycomb,5902,100;"/>
  <parameter key="custom_behavior" value="untouched"/>
 </parameters>
</npc>
)");
		std::string error;
		auto document = NpcDefinitionDocument::Load(Source(ServerContentFormat::Xml, path, "Trader"), error);
		Check(document != nullptr, "XML NPC loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().messages.size() == 1 && document->definition().shop.size() == 2, "XML messages and shops are normalized");
		NpcDefinition edited = document->definition();
		edited.health = 90;
		edited.lookBody = 18;
		edited.messages[0].text = "Welcome!";
		edited.shop[1].sell = 125;
		Check(document->save(edited, error), "XML NPC saves: " + error);
		const std::string saved = directory.read("data/npc/trader.xml");
		Check(saved.find("custom=\"preserve\"") != std::string::npos && saved.find("custom_behavior") != std::string::npos && saved.find("<!-- keep -->") != std::string::npos, "XML unknown source remains byte-preserved");
		Check(saved.find("now=\"90\"") != std::string::npos && saved.find("body=\"18\"") != std::string::npos && saved.find("Welcome!") != std::string::npos && saved.find("5902,125") != std::string::npos, "XML literal edits are patched in place");
	}

	void TestLua() {
		TemporaryDirectory directory;
		const auto path = directory.write("data/npc/captain.lua", R"(local internalNpcName = "Captain"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}
npcConfig.name = internalNpcName
npcConfig.description = internalNpcName
npcConfig.health = 100
npcConfig.maxHealth = npcConfig.health
npcConfig.walkInterval = 2000
npcConfig.walkRadius = 2
npcConfig.outfit = { lookType = 155, lookHead = 1, lookBody = 2, lookLegs = 3, lookFeet = 4, addons = 1, lookMount = 0 }
npcConfig.flags = { floorchange = false }
npcConfig.shop = { { itemName = "backpack", clientId = 2854, buy = 10, sell = 5, custom = callback() } }
addTravelKeyword("edron", 150, Position(33173, 31764, 6))
npcHandler:setMessage(MESSAGE_GREET, "Hello")
customCallback(computeValue())
npcType:register(npcConfig)
)");
		std::string error;
		auto document = NpcDefinitionDocument::Load(Source(ServerContentFormat::Lua, path, "Captain"), error);
		Check(document != nullptr, "Lua NPC loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().messages.size() == 1 && document->definition().shop.size() == 1 && document->definition().travel.size() == 1, "Lua direct messages, shop and travel are normalized");
		Check(
			document->definition().lookAddons == 1 && document->definition().capability(NpcField::LookAddons).editable,
			"Lua direct-method outfit addons alias is normalized and editable"
		);
		NpcDefinition edited = document->definition();
		edited.name = "Captain Blue";
		edited.lookHead = 9;
		edited.lookAddons = 2;
		edited.messages[0].text = "Welcome";
		edited.shop[0].buy = 12;
		edited.travel[0].cost = 175;
		Check(document->save(edited, error), "Lua NPC saves: " + error);
		const std::string saved = directory.read("data/npc/captain.lua");
		Check(saved.find("custom = callback()") != std::string::npos && saved.find("customCallback(computeValue())") != std::string::npos, "custom Lua behavior remains byte-preserved");
		Check(
			saved.find("Captain Blue") != std::string::npos && saved.find("lookHead = 9") != std::string::npos
				&& saved.find("addons = 2") != std::string::npos && saved.find("buy = 12") != std::string::npos
				&& saved.find("\"edron\", 175") != std::string::npos,
			"Lua literal and outfit alias edits are patched in place"
		);
	}

	void TestLuaInsertableCapabilities() {
		TemporaryDirectory directory;
		const auto path = directory.write("data/npc/insertable.lua", R"(local internalNpcName = "Insertable"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}
npcConfig.name = internalNpcName
npcConfig.outfit = { lookType = 128 }
npcConfig.flags = {}
customCallback(computeValue())
npcType:register(npcConfig)
)");
		std::string error;
		auto document = NpcDefinitionDocument::Load(Source(ServerContentFormat::Lua, path, "Insertable"), error);
		Check(document != nullptr, "minimal npcConfig Lua NPC opens: " + error);
		if (!document) {
			return;
		}
		const NpcDefinition& definition = document->definition();
		for (const NpcField field : { NpcField::Description, NpcField::Health, NpcField::MaxHealth, NpcField::WalkInterval, NpcField::WalkRadius, NpcField::Speed, NpcField::FloorChange, NpcField::LookTypeEx, NpcField::LookMount }) {
			Check(definition.capability(field).state == NpcFieldCapability::State::SupportedInsertable && definition.capability(field).editable, std::string(NpcFieldName(field)) + " is absent but insertable in npcConfig");
		}
		Check(definition.capability(NpcField::Direction).state == NpcFieldCapability::State::Unsupported, "direction stays disabled when the detected TFS Lua API has no direction property");
		NpcDefinition edited = definition;
		edited.description = "an inserted NPC";
		edited.health = 90;
		edited.maxHealth = 120;
		edited.walkInterval = 1800;
		edited.walkRadius = 4;
		edited.speed = 75;
		edited.floorChange = true;
		edited.lookTypeEx = 2160;
		edited.lookMount = 368;
		Check(document->save(edited, error), "all supported missing npcConfig properties insert safely: " + error);
		const std::string saved = directory.read("data/npc/insertable.lua");
		Check(saved.find("npcConfig.description = \"an inserted NPC\"") != std::string::npos, "missing description is inserted");
		Check(saved.find("npcConfig.health = 90") != std::string::npos && saved.find("npcConfig.maxHealth = 120") != std::string::npos, "missing health fields are inserted");
		Check(saved.find("npcConfig.walkInterval = 1800") != std::string::npos && saved.find("npcConfig.walkRadius = 4") != std::string::npos && saved.find("npcConfig.walkSpeed = 75") != std::string::npos, "missing movement fields use the npcConfig schema");
		Check(saved.find("floorchange = true") != std::string::npos && saved.find("lookTypeEx = 2160") != std::string::npos && saved.find("lookMount = 368") != std::string::npos, "missing flags and outfit fields are inserted in their real tables");
		Check(saved.find("customCallback(computeValue())") != std::string::npos, "npcConfig insertion preserves custom Lua");
	}

	void TestDirectMethodCapabilities() {
		TemporaryDirectory directory;
		const auto path = directory.write("data/npc/direct.lua", R"(local npcType = Game.createNpcType("Direct NPC")
npcType:outfit({ lookType = 472 })
customDirectCallback()
npcType:defaultBehavior()
)");
		std::string error;
		auto document = NpcDefinitionDocument::Load(Source(ServerContentFormat::Lua, path, "Direct NPC"), error);
		Check(document != nullptr, "direct-method Lua NPC opens: " + error);
		if (!document) {
			return;
		}
		for (const NpcField field : { NpcField::Health, NpcField::MaxHealth, NpcField::WalkInterval, NpcField::WalkRadius, NpcField::Speed, NpcField::FloorChange, NpcField::LookTypeEx, NpcField::LookMount }) {
			Check(document->definition().capability(field).state == NpcFieldCapability::State::SupportedInsertable, std::string(NpcFieldName(field)) + " is insertable through the direct NpcType provider");
		}
		NpcDefinition edited = document->definition();
		edited.health = 95;
		edited.maxHealth = 130;
		edited.walkInterval = 1500;
		edited.walkRadius = 5;
		edited.speed = 90;
		edited.floorChange = true;
		edited.lookTypeEx = 100;
		edited.lookMount = 200;
		Check(document->save(edited, error), "direct NpcType methods and outfit fields insert safely: " + error);
		const std::string saved = directory.read("data/npc/direct.lua");
		Check(saved.find("npcType:health(95)") != std::string::npos && saved.find("npcType:maxHealth(130)") != std::string::npos, "direct health methods are inserted");
		Check(saved.find("npcType:walkInterval(1500)") != std::string::npos && saved.find("npcType:spawnRadius(5)") != std::string::npos && saved.find("npcType:walkSpeed(90)") != std::string::npos, "direct movement uses detected NpcType method names");
		Check(saved.find("npcType:floorChange(true)") != std::string::npos && saved.find("lookTypeEx = 100") != std::string::npos && saved.find("lookMount = 200") != std::string::npos, "direct floor and outfit fields are inserted");
		Check(saved.find("customDirectCallback()") != std::string::npos, "direct-method insertion preserves custom Lua");
	}

	void TestDynamicAndAmbiguousCapabilities() {
		TemporaryDirectory directory;
		const auto path = directory.write("data/npc/dynamic.lua", R"(local npcType = Game.createNpcType("Dynamic NPC")
local npcConfig = {}
npcConfig.name = "Dynamic NPC"
npcConfig.health = calculateHealth()
npcConfig.walkRadius = 2
npcConfig.walkRadius = overrideRadius
npcConfig.outfit = { lookType = chooseLookType() }
npcType:register(npcConfig)
)");
		std::string error;
		auto document = NpcDefinitionDocument::Load(Source(ServerContentFormat::Lua, path, "Dynamic NPC"), error);
		Check(document != nullptr, "dynamic Lua NPC opens: " + error);
		if (document) {
			Check(document->definition().capability(NpcField::Health).state == NpcFieldCapability::State::DynamicReadOnly, "computed Lua property is dynamic read-only");
			Check(document->definition().capability(NpcField::WalkRadius).state == NpcFieldCapability::State::Ambiguous, "duplicate Lua property is ambiguity-safe");
			Check(document->definition().capability(NpcField::LookType).state == NpcFieldCapability::State::DynamicReadOnly, "computed outfit property is dynamic read-only");
		}
	}

	void TestReal(const std::filesystem::path& luaRoot, const std::filesystem::path& xmlRoot) {
		for (const auto& [root, format] : { std::pair(luaRoot, ServerContentFormat::Lua), std::pair(xmlRoot, ServerContentFormat::Xml) }) {
			const auto detected = ServerResourceDetector::Detect(root);
			const auto index = ServerContentIndex::Build(detected.workspace);
			const ServerContentSource* source = nullptr;
			for (const auto& entry : index.entries()) {
				if (entry.kind == ServerContentKind::Npc && entry.format == format) {
					source = &entry;
					break;
				}
			}
			Check(source != nullptr, std::string("real ") + ServerContentFormatName(format) + " base indexes an NPC");
			std::string error;
			auto document = source ? NpcDefinitionDocument::Load(*source, error) : nullptr;
			Check(document != nullptr && !document->definition().name.empty(), std::string("real ") + ServerContentFormatName(format) + " NPC opens: " + error);
		}

		const auto luaDetection = ServerResourceDetector::Detect(luaRoot);
		const auto luaIndex = ServerContentIndex::Build(luaDetection.workspace);
		std::string error;
		const auto captain = luaIndex.findExact(ServerContentKind::Npc, "Captain Cookie");
		auto captainDocument = captain.value() ? NpcDefinitionDocument::Load(*captain.value(), error) : nullptr;
		Check(captainDocument != nullptr && !captainDocument->definition().messages.empty() && !captainDocument->definition().travel.empty(), "real TFS Lua travel NPC exposes direct messages and destination literals: " + error);

		const ServerContentSource* bankerSource = nullptr;
		for (const ServerContentSource& source : luaIndex.entries()) {
			if (source.kind == ServerContentKind::Npc && source.format == ServerContentFormat::Lua
				&& source.declarationPath.filename() == "banker.lua") {
				bankerSource = &source;
				break;
			}
		}
		auto bankerDocument = bankerSource ? NpcDefinitionDocument::Load(*bankerSource, error) : nullptr;
		Check(
			bankerDocument != nullptr && bankerDocument->definition().lookAddons == 1
				&& bankerDocument->definition().capability(NpcField::LookAddons).editable,
			"real TFS Lua Banker exposes the direct-method outfit and addons alias: " + error
		);

		bool foundLuaShop = false;
		for (const ServerContentSource& source : luaIndex.entries()) {
			if (source.kind != ServerContentKind::Npc || source.format != ServerContentFormat::Lua) {
				continue;
			}
			auto candidate = NpcDefinitionDocument::Load(source, error);
			if (candidate && !candidate->definition().shop.empty()) {
				foundLuaShop = true;
				break;
			}
		}
		Check(foundLuaShop, "real TFS Lua base exposes a source-preserved shop table");

		const auto xmlDetection = ServerResourceDetector::Detect(xmlRoot);
		const auto xmlIndex = ServerContentIndex::Build(xmlDetection.workspace);
		bool foundXmlShop = false;
		for (const ServerContentSource& source : xmlIndex.entries()) {
			if (source.kind != ServerContentKind::Npc || source.format != ServerContentFormat::Xml) {
				continue;
			}
			auto candidate = NpcDefinitionDocument::Load(source, error);
			if (candidate && !candidate->definition().shop.empty()) {
				foundXmlShop = true;
				break;
			}
		}
		Check(foundXmlShop, "real XML TFS base exposes parameter-based shop entries");
	}

	void TestRealCanary(const std::filesystem::path& canaryRoot) {
		const auto detected = ServerResourceDetector::Detect(canaryRoot);
		Check(detected.validRoot && detected.workspace.usesCanaryCrystalLoader(), "real Crystal/Canary base selects its engine profile");
		const auto index = ServerContentIndex::Build(detected.workspace);
		const ServerContentSource* source = nullptr;
		for (const auto& entry : index.entries()) {
			if (entry.kind == ServerContentKind::Npc && entry.format == ServerContentFormat::Lua) {
				source = &entry;
				break;
			}
		}
		Check(source != nullptr, "real Crystal/Canary base indexes a Lua NPC");
		std::string error;
		auto document = source ? NpcDefinitionDocument::Load(*source, error) : nullptr;
		Check(
			document != nullptr && !document->definition().name.empty()
				&& document->definition().capability(NpcField::LookType).editable,
			"real Crystal/Canary NPC opens with editable literal fields: " + error
		);
	}

	void TestCreation() {
		TemporaryDirectory directory;
		std::filesystem::create_directories(directory.path / "data/npc/custom");
		ServerWorkspace workspace;
		workspace.rootPath = directory.path;
		workspace.npcsDirectory = directory.path / "data/npc";
		workspace.serverType = ServerType::Tfs;
		ServerContentIndex index = ServerContentIndex::Build(workspace);
		NpcCreationResult created;
		std::string error;
		Check(CreateNpcDefinition(workspace, index, { "John & Jane", ServerContentFormat::Xml, directory.path / "data/npc/custom" }, created, error), "XML NPC creation succeeds: " + error);
		Check(std::filesystem::is_regular_file(directory.path / "data/npc/custom/john_jane.xml") && std::filesystem::is_regular_file(directory.path / "data/npc/scripts/john_jane.lua"), "XML NPC creation includes a related legacy behavior script");
		Check(directory.read("data/npc/custom/john_jane.xml").find("John &amp; Jane") != std::string::npos, "XML NPC creation escapes identity");
		std::filesystem::create_directories(directory.path / "data/npc/lua");
		index = ServerContentIndex::Build(workspace, &index);
		Check(CreateNpcDefinition(workspace, index, { "Lua Guide", ServerContentFormat::Lua, directory.path / "data/npc/lua" }, created, error), "Lua NPC creation succeeds and validates: " + error);
		auto document = NpcDefinitionDocument::Load(created.source, error);
		Check(document != nullptr && document->definition().name == "Lua Guide", "created Lua NPC opens through the provider");
	}
}

int main(int argc, char** argv) {
	TestXml();
	TestLua();
	TestLuaInsertableCapabilities();
	TestDirectMethodCapabilities();
	TestDynamicAndAmbiguousCapabilities();
	TestCreation();
	if (argc == 4) {
		TestReal(argv[1], argv[2]);
		TestRealCanary(argv[3]);
	} else if (argc == 3) {
		TestReal(argv[1], argv[2]);
	} else if (argc != 1) {
		return 2;
	}
	if (!failures) {
		std::cout << checks << " NPC provider checks passed.\n";
	}
	return failures ? 1 : 0;
}
