#include "monster_definition.h"
#include "monster_definition_creation.h"
#include "editor_autosave_state.h"
#include "editor_source_monitor.h"
#include "server_content_index.h"
#include "server_workspace.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			path = std::filesystem::temp_directory_path() / ("nexamap-monster-provider-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path write(const std::filesystem::path& relative, const std::string& contents) const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary | std::ios::trunc);
			stream << contents;
			return target;
		}

		std::string read(const std::filesystem::path& relative) const {
			std::ifstream stream(path / relative, std::ios::binary);
			return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		}

		std::filesystem::path path;
	};

	ServerContentSource SourceFor(
		ServerContentFormat format,
		const std::filesystem::path& declaration,
		const std::optional<std::filesystem::path>& registration = std::nullopt
	) {
		ServerContentSource source;
		source.kind = ServerContentKind::Monster;
		source.format = format;
		source.name = "Demon";
		source.declarationPath = std::filesystem::weakly_canonical(declaration);
		source.declarationFingerprint = ResourceFingerprint::Read(source.declarationPath);
		source.declarationExists = true;
		source.registered = registration.has_value();
		if (registration) {
			source.registrationPath = std::filesystem::weakly_canonical(*registration);
			source.registrationFingerprint = ResourceFingerprint::Read(*source.registrationPath);
		}
		return source;
	}

	void TestXmlPreservingSave() {
		TemporaryDirectory server;
		const std::string declaration = "<?xml version=\"1.0\"?>\r\n"
										"<!-- keep this exact comment -->\r\n"
										"<monster name=\"Demon\" nameDescription='a demon' race=\"fire\" experience=\"6000\" speed=\"256\" custom=\"untouched\">\r\n"
										"  <health now=\"8200\" max=\"8200\" />\r\n"
										"  <look type=\"35\" head=\"1\" body=\"2\" legs=\"3\" feet=\"4\" addons=\"0\" corpse=\"5995\" />\r\n"
										"  <targetchange interval=\"4000\" chance=\"20\" />\r\n"
										"  <strategy attack=\"100\" defense=\"0\" />\r\n"
										"  <flags><flag attackable=\"1\"/><flag hostile=\"1\"/><flag canpushitems=\"1\"/><flag lightlevel=\"2\"/></flags>\r\n"
										"  <attacks custom=\"kept\"><attack name=\"fire\" interval=\"1500\" chance=\"15\" range=\"7\" radius=\"4\" target=\"1\" min=\"-60\" max=\"-110\"><attribute key=\"shootEffect\" value=\"fire\"/><attribute key=\"areaEffect\" value=\"firearea\"/><condition kind=\"kept\"/></attack><attack name=\"fire\" interval=\"2000\" chance=\"8\" length=\"8\" spread=\"3\" min=\"-90\" max=\"-170\"/></attacks>\r\n"
										"  <defenses armor=\"44\" defense=\"43\" mitigation=\"0.9\"><defense name=\"healing\" interval=\"1000\" chance=\"25\" min=\"50\" max=\"80\"><attribute key=\"areaEffect\" value=\"blueshimmer\"/></defense></defenses>\r\n"
										"  <elements><element firePercent=\"30\" custom=\"kept\"/></elements>\r\n"
										"  <immunities><immunity invisible=\"1\"/></immunities>\r\n"
										"  <summons maxSummons=\"2\"><summon name=\"Fire Elemental\" interval=\"2000\" chance=\"40\" force=\"1\"/></summons>\r\n"
										"  <voices interval=\"2000\" chance=\"5\"><voice sentence=\"Burn!\" yell=\"1\" custom=\"kept\"/></voices>\r\n"
										"  <loot custom=\"kept\"><!-- rare bag --><item id=\"1987\" chance=\"100000\"><inside><item name=\"gold coin\" countmax=\"20\" chance=\"50000\"/></inside></item></loot>\r\n"
										"  <unknown foo=\"bar\" />\r\n"
										"</monster>\r\n";
		const std::filesystem::path monster = server.write("data/monster/demons/Demon.xml", declaration);
		const std::filesystem::path registry = server.write(
			"data/monster/monsters.xml",
			"<monsters>\n  <monster name=\"Demon\" file=\"demons/Demon.xml\"/>\n</monsters>\n"
		);

		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, monster, registry), error);
		Check(document != nullptr, "XML monster loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().health == 8200 && document->definition().outfit.lookType == 35, "XML Main and Look values are normalized");
		Check(document->definition().strategyAttack == 100 && document->definition().lightLevel == 2, "XML strategy and light flags are normalized");
		Check(document->definition().capability(MonsterField::Health).editable, "XML literal health is editable");
		Check(!document->definition().capability(MonsterField::ManaCost).editable, "absent XML field is read-only");
		Check(document->definition().attacks.size() == 2 && document->definition().attacks.front().area.radius == 4 && document->definition().attacks.back().area.shape == MonsterAreaShape::Beam, "XML attacks, target radius and beam are normalized");
		Check(
			document->definition().defenseActions.size() == 1 && document->definition().defenseActions.front().preservedChildren.find("areaEffect") != std::string::npos,
			"XML defense actions and custom child attributes are normalized"
		);
		Check(
			document->definition().resistances.size() == 1 && document->definition().immunities.size() == 1,
			"XML resistances and immunities are normalized"
		);
		Check(
			document->definition().loot.size() == 1 && document->definition().loot.front().children.size() == 1,
			"XML nested container loot is normalized"
		);
		Check(
			document->definition().summons.size() == 1 && document->definition().voices.entries.size() == 1,
			"XML summons and voices are normalized"
		);

		const std::string originalRegistry = server.read("data/monster/monsters.xml");
		Check(document->save(document->definition(), error), "unchanged XML save succeeds");
		Check(server.read("data/monster/demons/Demon.xml") == declaration, "unchanged XML remains byte-identical");
		Check(server.read("data/monster/monsters.xml") == originalRegistry, "unchanged registry remains byte-identical");

		MonsterDefinition edited = document->definition();
		edited.name = "Demon Prime";
		edited.description = "a demon & guardian";
		edited.health = 9001;
		edited.outfit.lookType = 36;
		edited.attackable = false;
		edited.strategyDefense = 25;
		edited.defenseActions.front().chance = 30;
		edited.resistances.front().percent = 45;
		edited.immunities.push_back({ "paralyze", false, true, false, true, {} });
		edited.loot.front().children.front().maxCount = 50;
		edited.summons.front().chance = 55;
		edited.voices.entries.push_back({ "Run!", false, {} });
		edited.attacks.front().projectile = "energy";
		edited.attacks.back().area.spread = 5;
		Check(document->save(edited, error), "supported XML fields save transactionally: " + error);
		const std::string saved = server.read("data/monster/demons/Demon.xml");
		Check(saved.find("<!-- keep this exact comment -->") != std::string::npos, "XML comments are preserved");
		Check(saved.find("custom=\"untouched\"") != std::string::npos && saved.find("<unknown foo=\"bar\" />") != std::string::npos, "unknown XML content is preserved");
		Check(saved.find("name=\"Demon Prime\"") != std::string::npos && saved.find("now=\"9001\"") != std::string::npos, "known XML literals are replaced in place");
		Check(saved.find("nameDescription='a demon &amp; guardian'") != std::string::npos, "XML text is escaped without changing quote style");
		Check(saved.find("attackable=\"0\"") != std::string::npos, "XML boolean representation is preserved");
		Check(saved.find("defense=\"25\"") != std::string::npos, "XML strategy is saved in place");
		Check(
			saved.find("mitigation=\"0.9\"") != std::string::npos && saved.find("areaEffect") != std::string::npos
				&& saved.find("custom=\"kept\"") != std::string::npos && saved.find("rare bag") != std::string::npos,
			"unknown XML section properties and defense children survive structured edits"
		);
		Check(
			saved.find("firePercent=\"45\"") != std::string::npos && saved.find("paralyze=\"1\"") != std::string::npos
				&& saved.find("countmax=\"50\"") != std::string::npos && saved.find("chance=\"55\"") != std::string::npos
				&& saved.find("sentence=\"Run!\"") != std::string::npos,
			"XML advanced section edits are serialized"
		);
		Check(saved.find("shootEffect\" value=\"energy") != std::string::npos && saved.find("spread=\"5\"") != std::string::npos && saved.find("condition kind=\"kept\"") != std::string::npos, "XML attack edits and unknown child content are preserved");
		Check(server.read("data/monster/monsters.xml").find("name=\"Demon Prime\"") != std::string::npos, "registered XML name is updated in the same transaction");
		Check(
			document->definition().loot.front().children.front().maxCount == 50 && document->definition().voices.entries.size() == 2,
			"XML advanced sections reload after save"
		);
	}

	void TestLuaPreservingSave() {
		TemporaryDirectory server;
		const std::string source = "-- custom prologue must survive\n"
								   "local documentation = [=[ monster.health = 1 ]=]\n"
								   "--[=[ monster.health = 2 ]=]\n"
								   "local mType = Game.createMonsterType(\"Demon\")\n"
								   "local monster = {}\n"
								   "monster.name = \"Demon\"\n"
								   "monster.description = prefix .. \" demon\"\n"
								   "monster.health = 8200\n"
								   "monster.maxHealth = 8200\n"
								   "monster.experience = 6000\n"
								   "monster.outfit = { lookType = 35, lookHead = 0, lookBody = 1, lookLegs = 2, lookFeet = 3, lookAddons = 0 }\n"
								   "monster.flags = { attackable = true, hostile = true, targetDistance = 1, customFlag = computeFlag() }\n"
								   "monster.changeTarget = { interval = 4000, chance = 20 }\n"
								   "monster.attacks = {\n"
								   "\t{ name = \"combat\", interval = 2000, chance = 20, type = COMBAT_EARTHDAMAGE, minDamage = -15, maxDamage = -120, length = 8, spread = 0, effect = CONST_ME_CARNIPHILA, target = false, condition = { type = CONDITION_CURSED } }, -- earth beam\n"
								   "\t{ name = \"combat\", interval = 2000, chance = 15, type = COMBAT_FIREDAMAGE, minDamage = -70, maxDamage = -180, range = 7, radius = 5, effect = 243, shootEffect = CONST_ANI_FIRE, target = true },\n"
								   "}\n"
								   "monster.defenses = {\n"
								   "\tdefense = 30, armor = 25, mitigation = 0.99,\n"
								   "\t{ name = \"combat\", interval = 2000, chance = 15, type = COMBAT_HEALING, minDamage = 40, maxDamage = 70, effect = CONST_ME_MAGIC_BLUE, target = false, custom = compute() },\n"
								   "}\n"
								   "monster.elements = { { type = COMBAT_FIREDAMAGE, percent = 100, custom = \"kept\" } }\n"
								   "monster.immunities = { { type = \"paralyze\", condition = true }, { type = \"physical\", combat = true } }\n"
								   "monster.loot = {\n"
								   "\tcustomLoot = makeLoot(),\n"
								   "\t{ name = \"gold coin\", chance = 89920, maxCount = 102 }, -- rare coin\n"
								   "\t{ id = 1987, chance = 100000, childLoot = { { id = 2160, chance = 1000 } } },\n"
								   "}\n"
								   "monster.summons = { maxSummons = 2, customSummons = setting(), { name = \"Fire Elemental\", interval = 2000, chance = 40, force = true } }\n"
								   "monster.voices = { interval = 5000, chance = 10, customVoice = value(), { text = \"Burn!\", yell = true } }\n"
								   "monster.custom = makeCustom({ nested = true })\n"
								   "mType:register(monster)\n";
		const std::filesystem::path monster = server.write("data/monsters/demon.lua", source);
		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Lua, monster), error);
		Check(document != nullptr, "Lua monster loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().health == 8200 && document->definition().outfit.body == 1, "Lua Main and Look values are normalized");
		Check(!document->definition().capability(MonsterField::Description).editable, "computed Lua value is read-only");
		Check(document->definition().capability(MonsterField::Name).editable, "coordinated Lua name literals are editable");
		Check(
			document->definition().attacks.size() == 2
				&& document->definition().attacks.front().area.shape == MonsterAreaShape::Beam
				&& document->definition().attacks.back().effect == "243"
				&& document->definition().attacks.back().projectile == "CONST_ANI_FIRE",
			"Lua attacks, numeric effects, constants and areas are normalized"
		);
		Check(
			document->definition().defenseActions.size() == 1 && document->definition().defenseActions.front().type == "COMBAT_HEALING",
			"Lua defense actions and constants are normalized"
		);
		Check(
			document->definition().resistances.size() == 1 && document->definition().immunities.size() == 2,
			"Lua elements and immunities are normalized"
		);
		Check(
			document->definition().loot.size() == 2 && document->definition().loot.back().children.size() == 1,
			"Lua nested childLoot is normalized"
		);
		Check(
			document->definition().summons.size() == 1 && document->definition().voices.entries.size() == 1,
			"Lua summons and voices are normalized"
		);

		MonsterDefinition invalid = document->definition();
		invalid.description = "unsafe rewrite";
		Check(!document->save(invalid, error) && error.find("Description") != std::string::npos, "computed Lua field cannot be overwritten");
		Check(server.read("data/monsters/demon.lua") == source, "rejected Lua edit leaves the source byte-identical");

		MonsterDefinition edited = document->definition();
		edited.name = "Demon Prime";
		edited.health = 9001;
		edited.outfit.body = 7;
		edited.attackable = false;
		edited.defenseActions.front().chance = 25;
		edited.resistances.front().percent = 80;
		std::reverse(edited.immunities.begin(), edited.immunities.end());
		edited.loot.back().children.front().chance = 2000;
		edited.summons.front().force = false;
		edited.voices.entries.push_back({ "Run!", false, {} });
		edited.attacks.front().effect = "CONST_ME_FIREAREA";
		edited.attacks.back().area.radius = 4;
		Check(document->save(edited, error), "supported Lua literals save transactionally: " + error);
		const std::string saved = server.read("data/monsters/demon.lua");
		Check(saved.find("Game.createMonsterType(\"Demon Prime\")") != std::string::npos && saved.find("monster.name = \"Demon Prime\"") != std::string::npos, "coordinated Lua name literals stay consistent");
		Check(saved.find("monster.health = 9001") != std::string::npos && saved.find("lookBody = 7") != std::string::npos, "known Lua literals are replaced in place");
		Check(saved.find("customFlag = computeFlag()") != std::string::npos && saved.find("monster.custom = makeCustom({ nested = true })") != std::string::npos, "custom Lua expressions are preserved");
		Check(
			saved.find("mitigation = 0.99") != std::string::npos && saved.find("custom = compute()") != std::string::npos
				&& saved.find("customLoot = makeLoot()") != std::string::npos
				&& saved.find("customSummons = setting()") != std::string::npos
				&& saved.find("customVoice = value()") != std::string::npos && saved.find("-- rare coin") != std::string::npos,
			"unknown Lua section expressions survive structured edits"
		);
		Check(
			saved.find("percent = 80") != std::string::npos && saved.find("chance = 2000") != std::string::npos
				&& saved.find("text = \"Run!\"") != std::string::npos,
			"Lua advanced section edits are serialized"
		);
		Check(saved.find("effect = CONST_ME_FIREAREA") != std::string::npos && saved.find("radius = 4") != std::string::npos && saved.find("condition = { type = CONDITION_CURSED }") != std::string::npos && saved.find("-- earth beam") != std::string::npos, "Lua attack edits preserve nested custom expressions and comments");
		Check(
			document->definition().loot.back().children.front().chance == 2000 && document->definition().voices.entries.size() == 2,
			"Lua advanced sections reload after save"
		);
	}

	void TestAmbiguousAndExternalChanges() {
		TemporaryDirectory server;
		ServerContentSource registered;
		registered.registered = true;
		ServerContentSource unregistered;
		ServerContentLookupResult authoritative;
		authoritative.matches = { &unregistered, &registered };
		Check(authoritative.uniqueRegisteredValue() == &registered, "one registered source safely resolves otherwise ambiguous matches");
		ServerContentSource secondRegistered;
		secondRegistered.registered = true;
		authoritative.matches.push_back(&secondRegistered);
		Check(authoritative.uniqueRegisteredValue() == nullptr, "multiple registered sources remain ambiguous");

		const std::filesystem::path duplicate = server.write(
			"duplicate.lua",
			"local m = Game.createMonsterType(\"Demon\")\nlocal monster = {}\nmonster.health = 10\nmonster.health = 20\nm:register(monster)\n"
		);
		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Lua, duplicate), error);
		Check(document != nullptr, "Lua with duplicate assignments still opens for inspection");
		if (document) {
			Check(!document->definition().capability(MonsterField::Health).editable, "duplicate Lua assignment is ambiguity-safe");
		}

		const std::filesystem::path external = server.write(
			"external.xml",
			"<monster name=\"Demon\"><health now=\"10\" max=\"10\"/><look type=\"35\"/></monster>\n"
		);
		document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, external), error);
		Check(document != nullptr, "XML opens before external modification");
		if (document) {
			server.write("external.xml", "<monster name=\"Demon\"><health now=\"11\" max=\"10\"/><look type=\"35\"/></monster>\n");
			MonsterDefinition edited = document->definition();
			edited.health = 12;
			Check(!document->save(edited, error) && error.find("changed on disk") != std::string::npos, "external source modification refuses to save");
		}

		const std::filesystem::path malformed = server.write("broken.xml", "<monster name=\"Broken\">");
		Check(!MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, malformed), error), "malformed XML is rejected");
	}

	void TestAdvancedValidation() {
		MonsterDefinition definition;
		std::string error;
		definition.loot.push_back({ 0, "", false });
		Check(!ValidateMonsterDefinition(definition, error) && error.find("loot") != std::string::npos, "loot requires a valid item identity");
		definition.loot.front().itemId = 2160;
		definition.resistances.push_back({ "fire", 101 });
		Check(!ValidateMonsterDefinition(definition, error) && error.find("Resistance") != std::string::npos, "resistance percent is range validated");
		definition.resistances.front().percent = 100;
		definition.summons.push_back({ "", 2000, 100 });
		Check(!ValidateMonsterDefinition(definition, error) && error.find("summon") != std::string::npos, "summons require a creature name");
		definition.summons.front().name = "Fire Elemental";
		definition.voices.entries.push_back({ "", false });
		Check(!ValidateMonsterDefinition(definition, error) && error.find("Voice") != std::string::npos, "voices require text");
		definition.voices.entries.front().text = "Burn!";
		Check(ValidateMonsterDefinition(definition, error), "valid advanced monster sections pass validation");
	}

	void TestNewMonsterCreation() {
		TemporaryDirectory xmlServer;
		const std::filesystem::path xmlDirectory = xmlServer.path / "data/monster/custom";
		std::filesystem::create_directories(xmlDirectory);
		xmlServer.write("data/monster/monsters.xml", "<?xml version=\"1.0\"?>\n<monsters>\n</monsters>\n");
		ServerWorkspace xmlWorkspace;
		xmlWorkspace.rootPath = xmlServer.path;
		// The workspace path may contain an alias or lexical segment (for example RUNNER~1 on CI).
		xmlWorkspace.monstersDirectory = xmlServer.path / "data/monster/../monster";
		ServerContentIndex xmlIndex = ServerContentIndex::Build(xmlWorkspace);
		MonsterCreationRequest request { "Test & Guardian", ServerContentFormat::Xml, xmlDirectory };
		MonsterCreationResult created;
		std::string error;
		const bool xmlCreated = CreateMonsterDefinition(xmlWorkspace, xmlIndex, request, created, error);
		Check(xmlCreated, "new XML monster and registry entry are created: " + error);
		Check(std::filesystem::is_regular_file(xmlDirectory / "test_guardian.xml"), "new XML monster receives a safe filename");
		const std::string registry = xmlServer.read("data/monster/monsters.xml");
		Check(registry.find("name=\"Test &amp; Guardian\"") != std::string::npos && registry.find("custom/test_guardian.xml") != std::string::npos, "new XML monster is registered with escaped metadata");
		auto document = MonsterDefinitionDocument::Load(created.source, error);
		Check(document != nullptr, "created XML monster opens in the visual editor provider: " + error);
		if (document) {
			Check(document->definition().capability(MonsterField::Health).editable, "created XML Main fields are editable");
			Check(document->definition().capability(MonsterSection::Attacks).editable, "created XML attack section is editable");
		}
		xmlIndex = ServerContentIndex::Build(xmlWorkspace, &xmlIndex);
		Check(!CreateMonsterDefinition(xmlWorkspace, xmlIndex, request, created, error) && error.find("already exists") != std::string::npos, "duplicate XML monster names are rejected");

		TemporaryDirectory luaServer;
		const std::filesystem::path luaDirectory = luaServer.path / "data/monsters/custom";
		std::filesystem::create_directories(luaDirectory);
		ServerWorkspace luaWorkspace;
		luaWorkspace.rootPath = luaServer.path;
		luaWorkspace.monstersDirectory = luaServer.path / "data/monsters";
		luaWorkspace.serverType = ServerType::Tfs;
		luaServer.write(
			"data/scripts/lib/register_monster_type.lua",
			"registerMonsterType.summons = function(mtype, mask)\n"
			" local summonData = mask.summons or (mask.summon and mask.summon.summons)\n"
			" for _, v in pairs(summonData or {}) do mtype:addSummon(v.name, v.interval, v.chance, v.count) end\nend\n"
		);
		const ServerContentIndex luaIndex = ServerContentIndex::Build(luaWorkspace);
		request = { "Lua Sentinel", ServerContentFormat::Lua, luaDirectory };
		const bool luaCreated = CreateMonsterDefinition(luaWorkspace, luaIndex, request, created, error);
		Check(luaCreated, "new Lua monster is created: " + error);
		const std::string tfsLua = luaServer.read("data/monsters/custom/lua_sentinel.lua");
		Check(created.provider == "TFS Lua registerMonsterType", "TFS Lua creation reports its detected provider");
		Check(tfsLua.find("monster.summons = {}") != std::string::npos && tfsLua.find("summons = { maxSummons") == std::string::npos, "TFS Lua uses a list for summons and cannot trigger registerMonsterType v.name on a number");
		document = MonsterDefinitionDocument::Load(created.source, error);
		Check(document != nullptr, "created Lua monster opens in the visual editor provider: " + error);
		if (document) {
			Check(document->definition().name == "Lua Sentinel" && document->definition().capability(MonsterField::LookType).editable, "created Lua Main and Look fields are editable");
			Check(document->definition().capability(MonsterSection::Loot).editable, "created Lua loot section is editable");
		}

		TemporaryDirectory canaryServer;
		const std::filesystem::path canaryDirectory = canaryServer.path / "data-canary/monster/custom";
		std::filesystem::create_directories(canaryDirectory);
		ServerWorkspace canaryWorkspace;
		canaryWorkspace.rootPath = canaryServer.path;
		canaryWorkspace.monstersDirectory = canaryServer.path / "data-canary/monster";
		canaryWorkspace.serverType = ServerType::Canary;
		const ServerContentIndex canaryIndex = ServerContentIndex::Build(canaryWorkspace);
		request = { "Canary Sentinel", ServerContentFormat::Lua, canaryDirectory };
		const bool canaryCreated = CreateMonsterDefinition(canaryWorkspace, canaryIndex, request, created, error);
		Check(canaryCreated, "new Canary Lua monster is created: " + error);
		const std::string canaryLua = canaryServer.read("data-canary/monster/custom/canary_sentinel.lua");
		Check(created.provider == "Canary/Crystal Lua", "Canary family selects the Canary Lua provider");
		Check(canaryLua.find("monster.summon = { maxSummons = 0, summons = {} }") != std::string::npos, "Canary Lua uses its nested summon provider shape");
		auto canaryDocument = MonsterDefinitionDocument::Load(created.source, error);
		Check(
			canaryDocument != nullptr && canaryDocument->definition().capability(MonsterSection::Summons).editable,
			"created Canary monster reopens with an editable nested summon section: " + error
		);
		if (canaryDocument) {
			MonsterDefinition edited = canaryDocument->definition();
			edited.maxSummons = 2;
			edited.summons.push_back({ "Fire Elemental", 2000, 50, 2 });
			Check(canaryDocument->save(edited, error), "Canary nested summons save through their provider: " + error);
			const std::string savedCanary = canaryServer.read("data-canary/monster/custom/canary_sentinel.lua");
			Check(
				savedCanary.find("monster.summon = {") != std::string::npos
					&& savedCanary.find("summons = {") != std::string::npos
					&& savedCanary.find("count = 2") != std::string::npos
					&& savedCanary.find("monster.summons =") == std::string::npos,
				"Canary summon edits preserve the nested registration shape and count field"
			);
		}
	}

	void TestAutosaveState() {
		using namespace std::chrono_literals;
		EditorAutosaveState state(500ms);
		const auto start = EditorAutosaveState::Clock::time_point(1s);
		Check(!state.dirty() && !state.ready(start), "autosave begins clean");
		state.changed(start);
		Check(state.dirty() && !state.ready(start + 499ms), "autosave waits for the debounce interval");
		Check(state.ready(start + 500ms), "autosave becomes ready at the debounce deadline");
		state.failed("invalid field");
		Check(state.dirty() && state.hasError() && !state.ready(start + 2s), "invalid autosave remains dirty with a visible error");
		state.changed(start + 2s);
		Check(!state.hasError() && state.ready(start + 2500ms), "editing after an error schedules validation again");
		state.saved();
		Check(!state.dirty() && !state.hasError(), "successful autosave clears dirty and error state");

		EditorChangeCoalescer changes;
		changes.changed(EditorChangeImpact::Dirty);
		Check(changes.hasDirty() && !changes.hasPreview(), "ordinary fields schedule autosave without preview work");
		changes.changed(EditorChangeImpact::Dirty | EditorChangeImpact::Preview);
		changes.changed(EditorChangeImpact::Preview);
		Check(changes.takePreview() && !changes.takePreview(), "repeated visual changes coalesce into one preview refresh");
		Check(changes.takeDirty() && !changes.takeDirty(), "repeated edits coalesce into one dirty action");

		TemporaryDirectory temporary;
		const auto monitoredPath = temporary.write("monster.lua", "original\n");
		EditorSourceMonitor monitor;
		monitor.reset({ { monitoredPath, "original\n" } });
		Check(monitor.poll().empty(), "source monitor stays quiet while the file is unchanged");
		temporary.write("monster.lua", "external change\n");
		const auto external = monitor.poll();
		Check(external.size() == 1 && external.front().loadedText == "original\n" && external.front().diskText == "external change\n", "source monitor retains loaded and disk versions for conflict comparison");
		monitor.reset({ { monitoredPath, "external change\n" } });
		Check(monitor.poll().empty(), "source monitor acknowledges a freshly saved baseline");
		std::filesystem::remove(monitoredPath);
		const auto deleted = monitor.poll();
		Check(deleted.size() == 1 && deleted.front().deleted, "source monitor reports an external deletion without overwriting it");
	}

	void TestRealServers(const std::filesystem::path& modernRoot, const std::filesystem::path& xmlRoot) {
		const ServerDetectionResult modernDetection = ServerResourceDetector::Detect(modernRoot);
		const ServerContentIndex modern = ServerContentIndex::Build(modernDetection.workspace);
		const auto modernDemon = modern.findExact(ServerContentKind::Monster, "Demon");
		Check(modernDemon.unique(), "real modern base resolves Demon uniquely");
		std::string error;
		auto document = modernDemon.unique() ? MonsterDefinitionDocument::Load(*modernDemon.value(), error) : nullptr;
		Check(document != nullptr, "real modern Lua Demon opens: " + error);
		if (document) {
			Check(document->definition().capability(MonsterField::Health).editable, "real modern Lua health is editable");
			Check(document->definition().capability(MonsterField::LookType).editable, "real modern Lua lookType is editable");
		}
		const auto modernDragon = modern.findExact(ServerContentKind::Monster, "Dragon");
		const ServerContentSource* modernDragonSource = modernDragon.value() ? modernDragon.value() : modernDragon.uniqueRegisteredValue();
		document = modernDragonSource ? MonsterDefinitionDocument::Load(*modernDragonSource, error) : nullptr;
		Check(document != nullptr, "real modern Lua Dragon advanced sections open: " + error);
		if (document) {
			Check(
				!document->definition().loot.empty() && !document->definition().defenseActions.empty() && !document->definition().attacks.empty()
					&& !document->definition().resistances.empty() && !document->definition().voices.entries.empty(),
				"real modern Lua Dragon exposes loot, defenses, elements and voices"
			);
			Check(
				document->definition().capability(MonsterSection::Loot).editable
					&& document->definition().capability(MonsterSection::Defenses).editable
					&& document->definition().capability(MonsterSection::Attacks).editable,
				"real modern Lua advanced and attack sections are structurally editable"
			);
		}

		const ServerDetectionResult xmlDetection = ServerResourceDetector::Detect(xmlRoot);
		const ServerContentIndex xml = ServerContentIndex::Build(xmlDetection.workspace);
		const auto xmlDemon = xml.findExact(ServerContentKind::Monster, "Demon");
		Check(xmlDemon.ambiguous() && xmlDemon.uniqueRegisteredValue() != nullptr, "real XML duplicate names resolve only through one authoritative registry entry");
		const ServerContentSource* uniqueXmlMonster = nullptr;
		document.reset();
		for (const ServerContentSource& source : xml.entries()) {
			if (source.kind == ServerContentKind::Monster && source.format == ServerContentFormat::Xml
				&& xml.findExact(ServerContentKind::Monster, source.name).unique()) {
				auto candidate = MonsterDefinitionDocument::Load(source, error);
				if (candidate && candidate->definition().capability(MonsterField::Health).editable
					&& candidate->definition().capability(MonsterField::LookType).editable) {
					uniqueXmlMonster = &source;
					document = std::move(candidate);
					break;
				}
			}
		}
		Check(uniqueXmlMonster != nullptr, "real XML base exposes an ambiguity-safe monster with Main and Look literals");
		Check(document != nullptr, "real XML monster opens: " + error);
		if (document) {
			Check(document->definition().capability(MonsterField::Health).editable, "real XML health is editable");
			Check(document->definition().capability(MonsterField::LookType).editable, "real XML lookType is editable");
		}
		const auto xmlDragon = xml.findExact(ServerContentKind::Monster, "Dragon");
		const ServerContentSource* xmlDragonSource = xmlDragon.value() ? xmlDragon.value() : xmlDragon.uniqueRegisteredValue();
		document = xmlDragonSource ? MonsterDefinitionDocument::Load(*xmlDragonSource, error) : nullptr;
		Check(document != nullptr, "real XML Dragon advanced sections open: " + error);
		if (document) {
			Check(
				!document->definition().loot.empty() && !document->definition().loot.back().children.empty() && !document->definition().attacks.empty()
					&& !document->definition().defenseActions.empty() && !document->definition().resistances.empty()
					&& !document->definition().immunities.empty() && !document->definition().voices.entries.empty(),
				"real XML Dragon exposes nested loot, defenses, elements, immunities and voices"
			);
			Check(
				document->definition().capability(MonsterSection::Loot).editable
					&& document->definition().capability(MonsterSection::Defenses).editable
					&& document->definition().capability(MonsterSection::Attacks).editable,
				"real XML advanced and attack sections are structurally editable"
			);
		}
	}

	void TestRealCanary(const std::filesystem::path& canaryRoot) {
		const ServerDetectionResult detection = ServerResourceDetector::Detect(canaryRoot);
		Check(detection.validRoot && detection.workspace.usesCanaryCrystalLoader(), "real Crystal/Canary base selects its engine profile");
		const ServerContentIndex index = ServerContentIndex::Build(detection.workspace);
		Check(
			DetectMonsterCreationProvider(detection.workspace, index, ServerContentFormat::Lua) == MonsterCreationProvider::CanaryLua,
			"real Crystal/Canary registration API selects the nested Lua creation provider"
		);
		const auto lich = index.findExact(ServerContentKind::Monster, "Lich");
		const ServerContentSource* source = lich.value() ? lich.value() : lich.uniqueRegisteredValue();
		std::string error;
		auto document = source ? MonsterDefinitionDocument::Load(*source, error) : nullptr;
		Check(document != nullptr, "real Crystal/Canary Lich opens: " + error);
		if (document) {
			Check(
				document->definition().maxSummons == 4 && !document->definition().summons.empty()
					&& document->definition().summons.front().max == 4,
				"real Crystal/Canary nested summons and count are normalized"
			);
			Check(document->definition().capability(MonsterSection::Summons).editable, "real Crystal/Canary nested summon section is editable");
		}
	}
}

int main(int argc, char** argv) {
	TestXmlPreservingSave();
	TestLuaPreservingSave();
	TestAmbiguousAndExternalChanges();
	TestAdvancedValidation();
	TestNewMonsterCreation();
	TestAutosaveState();
	if (argc == 4) {
		TestRealServers(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
		TestRealCanary(std::filesystem::path(argv[3]));
	} else if (argc == 3) {
		TestRealServers(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
	} else if (argc != 1) {
		std::cerr << "Usage: monster_definition_provider_tests [modern-lua-server xml-server [canary-crystal-server]]\n";
		return 2;
	}
	if (failures == 0) {
		std::cout << checks << " monster definition provider checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
