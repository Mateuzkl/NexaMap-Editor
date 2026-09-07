#include "npc_definition_creation.h"

#include "file_transaction.h"
#include "npc_definition.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {
	std::string Trim(std::string value) {
		const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
		const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
		return first < last ? std::string(first, last) : std::string();
	}
	std::string Stem(const std::string& name) {
		std::string result;
		bool separator = false;
		for (unsigned char c : name) {
			if (std::isalnum(c)) {
				if (separator && !result.empty()) {
					result.push_back('_');
				}
				result.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
				separator = false;
			} else {
				separator = true;
			}
		}
		return result.empty() ? "npc" : result;
	}
	std::string EscapeXml(const std::string& value) {
		std::string result;
		for (char c : value) {
			if (c == '&') {
				result += "&amp;";
			} else if (c == '"') {
				result += "&quot;";
			} else if (c == '<') {
				result += "&lt;";
			} else if (c == '>') {
				result += "&gt;";
			} else {
				result.push_back(c);
			}
		}
		return result;
	}
	std::string EscapeLua(const std::string& value) {
		std::string result;
		for (char c : value) {
			if (c == '\\' || c == '"') {
				result.push_back('\\');
			}
			result.push_back(c);
		}
		return result;
	}
	std::string Xml(const std::string& name, const std::string& script) {
		return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<npc name=\"" + EscapeXml(name) + "\" script=\"" + EscapeXml(script) + "\" walkinterval=\"2000\" speed=\"0\" floorchange=\"0\" lookdir=\"2\">\n\t<health now=\"100\" max=\"100\"/>\n\t<look type=\"128\" head=\"0\" body=\"0\" legs=\"0\" feet=\"0\" addons=\"0\" mount=\"0\"/>\n\t<parameters>\n\t\t<parameter key=\"message_greet\" value=\"Hello |PLAYERNAME|.\"/>\n\t</parameters>\n</npc>\n";
	}
	std::string LegacyScript() {
		return "local keywordHandler = KeywordHandler:new()\nlocal npcHandler = NpcHandler:new(keywordHandler)\nNpcSystem.parseParameters(npcHandler)\n\nfunction onCreatureAppear(cid) npcHandler:onCreatureAppear(cid) end\nfunction onCreatureDisappear(cid) npcHandler:onCreatureDisappear(cid) end\nfunction onCreatureSay(cid, type, msg) npcHandler:onCreatureSay(cid, type, msg) end\nfunction onThink() npcHandler:onThink() end\n\nnpcHandler:addModule(FocusModule:new())\n";
	}
	std::string Lua(const std::string& name) {
		return "local internalNpcName = \"" + EscapeLua(name) + "\"\nlocal npcType = Game.createNpcType(internalNpcName)\nlocal npcConfig = {}\n\nnpcConfig.name = internalNpcName\nnpcConfig.description = internalNpcName\nnpcConfig.health = 100\nnpcConfig.maxHealth = npcConfig.health\nnpcConfig.walkInterval = 2000\nnpcConfig.walkRadius = 2\nnpcConfig.outfit = { lookType = 128, lookHead = 0, lookBody = 0, lookLegs = 0, lookFeet = 0, lookAddons = 0, lookMount = 0 }\nnpcConfig.flags = { floorchange = false }\n\nlocal keywordHandler = KeywordHandler:new()\nlocal npcHandler = NpcHandler:new(keywordHandler)\nnpcType.onThink = function(npc, interval) npcHandler:onThink(npc, interval) end\nnpcType.onAppear = function(npc, creature) npcHandler:onAppear(npc, creature) end\nnpcType.onDisappear = function(npc, creature) npcHandler:onDisappear(npc, creature) end\nnpcType.onMove = function(npc, creature, fromPosition, toPosition) npcHandler:onMove(npc, creature, fromPosition, toPosition) end\nnpcType.onSay = function(npc, creature, type, message) npcHandler:onSay(npc, creature, type, message) end\nnpcType.onCloseChannel = function(npc, creature) npcHandler:onCloseChannel(npc, creature) end\nnpcHandler:setMessage(MESSAGE_GREET, \"Hello |PLAYERNAME|.\")\nnpcHandler:addModule(FocusModule:new(), npcConfig.name, true, true, true)\nnpcType:register(npcConfig)\n";
	}
	bool Write(const std::filesystem::path& path, const std::string& bytes, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << bytes;
		if (!stream) {
			error = "Could not stage new NPC source.";
			return false;
		}
		return true;
	}
	bool SafeDirectory(const std::filesystem::path& path, const std::filesystem::path& root) {
		std::error_code ec;
		const auto p = std::filesystem::weakly_canonical(path, ec);
		if (ec) {
			return false;
		}
		const auto r = std::filesystem::weakly_canonical(root, ec);
		if (ec) {
			return false;
		}
		const auto relative = p.lexically_relative(r);
		return !relative.empty() && !relative.is_absolute() && std::none_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; });
	}
}

bool CreateNpcDefinition(const ServerWorkspace& workspace, const ServerContentIndex& index, const NpcCreationRequest& request, NpcCreationResult& result, std::string& error) {
	error.clear();
	result = {};
	const std::string name = Trim(request.name);
	if (name.empty()) {
		error = "Enter an NPC name.";
		return false;
	}
	if (!index.findCaseInsensitive(ServerContentKind::Npc, name).empty()) {
		error = "An NPC named '" + name + "' already exists.";
		return false;
	}
	if (workspace.npcsDirectory.empty() || !SafeDirectory(request.destinationDirectory, workspace.npcsDirectory)) {
		error = "Choose an existing directory inside the active NPC directory.";
		return false;
	}
	if (request.format != ServerContentFormat::Xml && request.format != ServerContentFormat::Lua) {
		error = "Choose XML or Lua.";
		return false;
	}
	const std::string stem = Stem(name);
	const auto declaration = request.destinationDirectory / (stem + (request.format == ServerContentFormat::Xml ? ".xml" : ".lua"));
	std::error_code ec;
	if (std::filesystem::exists(declaration, ec) || ec) {
		error = "The destination file already exists: " + declaration.string();
		return false;
	}
	FileSaveTransaction transaction;
	const auto staged = transaction.Stage(declaration);
	std::optional<std::filesystem::path> behavior;
	if (request.format == ServerContentFormat::Xml) {
		behavior = workspace.npcsDirectory / "scripts" / (stem + ".lua");
		if (std::filesystem::exists(*behavior, ec) || ec) {
			error = "The related NPC script already exists: " + behavior->string();
			return false;
		}
		std::filesystem::create_directories(behavior->parent_path(), ec);
		if (ec) {
			error = "Could not create the NPC scripts directory.";
			return false;
		}
		if (!Write(staged, Xml(name, behavior->filename().string()), error) || !Write(transaction.Stage(*behavior), LegacyScript(), error)) {
			return false;
		}
	} else if (!Write(staged, Lua(name), error)) {
		return false;
	}
	ServerContentSource candidate;
	candidate.kind = ServerContentKind::Npc;
	candidate.format = request.format;
	candidate.name = name;
	candidate.declarationPath = staged;
	candidate.declarationExists = true;
	candidate.registered = true;
	candidate.declarationFingerprint = ResourceFingerprint::Read(staged);
	auto document = NpcDefinitionDocument::Load(candidate, error);
	if (!document || !ValidateNpcDefinition(document->definition(), error)) {
		error = "Generated NPC validation failed: " + error;
		return false;
	}
	if (!transaction.Commit(error)) {
		return false;
	}
	result.source = candidate;
	result.source.declarationPath = declaration;
	result.source.relatedScriptPath = behavior;
	result.source.declarationFingerprint = ResourceFingerprint::Read(declaration);
	if (behavior) {
		result.source.relatedScriptFingerprint = ResourceFingerprint::Read(*behavior);
	}
	return true;
}
