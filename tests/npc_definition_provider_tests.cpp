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
npcConfig.outfit = { lookType = 155, lookHead = 1, lookBody = 2, lookLegs = 3, lookFeet = 4, lookAddons = 0, lookMount = 0 }
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
		NpcDefinition edited = document->definition();
		edited.name = "Captain Blue";
		edited.lookHead = 9;
		edited.messages[0].text = "Welcome";
		edited.shop[0].buy = 12;
		edited.travel[0].cost = 175;
		Check(document->save(edited, error), "Lua NPC saves: " + error);
		const std::string saved = directory.read("data/npc/captain.lua");
		Check(saved.find("custom = callback()") != std::string::npos && saved.find("customCallback(computeValue())") != std::string::npos, "custom Lua behavior remains byte-preserved");
		Check(saved.find("Captain Blue") != std::string::npos && saved.find("lookHead = 9") != std::string::npos && saved.find("buy = 12") != std::string::npos && saved.find("\"edron\", 175") != std::string::npos, "Lua literal edits are patched in place");
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
	TestCreation();
	if (argc == 3) {
		TestReal(argv[1], argv[2]);
	} else if (argc != 1) {
		return 2;
	}
	if (!failures) {
		std::cout << checks << " NPC provider checks passed.\n";
	}
	return failures ? 1 : 0;
}
