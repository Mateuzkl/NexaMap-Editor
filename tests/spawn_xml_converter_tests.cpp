#include "spawn_xml_converter.h"

#include "ext/pugixml.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int failures = 0;

	void Check(bool condition, const std::string& name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}

	struct TemporaryDirectory {
		TemporaryDirectory() {
			path = std::filesystem::temp_directory_path() / ("rme_spawn_xml_converter_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};

	void Write(const std::filesystem::path& file, const std::string& contents) {
		std::ofstream output(file, std::ios::binary);
		output << contents;
	}

	void Read(const std::filesystem::path& file, pugi::xml_document& document) {
		Check(static_cast<bool>(document.load_file(file.c_str())), "generated XML can be parsed");
	}

	size_t ChildElementCount(const pugi::xml_node& node, const char* name = nullptr) {
		size_t count = 0;
		for (const pugi::xml_node& child : node.children()) {
			if (child.type() == pugi::node_element && (name == nullptr || std::string(child.name()) == name)) {
				++count;
			}
		}
		return count;
	}

	void TestNamingAndInspection(const TemporaryDirectory& temporary) {
		const std::filesystem::path oddMonster = temporary.path / "unexpected.xml";
		Write(oddMonster, "<?xml version=\"1.0\"?><monsters/>");
		const SpawnXmlInspection inspection = SpawnXmlConverter::Inspect(oddMonster);
		Check(inspection.layout == SpawnXmlLayout::CanaryMonsters, "format detection uses XML root instead of filename");

		SpawnXmlSuggestedPaths paths = SpawnXmlConverter::SuggestFromCanaryFile(temporary.path / "MapName-monster.xml", SpawnXmlLayout::CanaryMonsters);
		Check(paths.npcFile.filename() == "MapName-npc.xml" && paths.tfsFile.filename() == "MapName-spawn.xml", "singular Canary filenames produce standard companions");
		paths = SpawnXmlConverter::SuggestFromCanaryFile(temporary.path / "MapName-monsters.xml", SpawnXmlLayout::CanaryMonsters);
		Check(paths.npcFile.filename() == "MapName-npc.xml" && paths.tfsFile.filename() == "MapName-spawn.xml", "plural monster filename is accepted");
		paths = SpawnXmlConverter::SuggestFromTfsFile(temporary.path / "MapName-spawn.xml", temporary.path / "out", false);
		Check(paths.monsterFile.filename() == "MapName-monster.xml" && paths.npcFile.filename() == "MapName-npc.xml", "TFS filename produces singular Canary outputs");
		paths = SpawnXmlConverter::SuggestFromTfsFile(temporary.path / "spawn.xml", temporary.path, true);
		Check(paths.monsterFile.filename() == "monsters.xml" && paths.npcFile.filename() == "npc.xml", "plain spawn.xml and plural option produce expected names");
	}

	void TestCanaryToTfs(const TemporaryDirectory& temporary) {
		const std::filesystem::path monsters = temporary.path / "world-monster.xml";
		const std::filesystem::path npcs = temporary.path / "world-npc.xml";
		const std::filesystem::path output = temporary.path / "world-spawn.xml";
		Write(monsters,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<monsters>"
			"<monster centerx=\"238\" centery=\"644\" centerz=\"0\" radius=\"0\" customArea=\"keep\">"
			"<monster name=\"Cobra &amp; Assassin\" x=\"-1\" y=\"0\" z=\"0\" spawntime=\"60\" custom=\"\xC3\xA1\xC3\xA9\"/>"
			"<monster name=\"Cobra &amp; Assassin\" x=\"-1\" y=\"0\" z=\"0\" spawntime=\"60\" custom=\"\xC3\xA1\xC3\xA9\"/>"
			"</monster>"
			"<monster centerx=\"500\" centery=\"600\" centerz=\"7\" radius=\"5\">"
			"<monster name=\"Dragon\" x=\"3\" y=\"-4\" z=\"7\" spawntime=\"120\" direction=\"1\"/>"
			"</monster>"
			"</monsters>");
		Write(npcs,
			"<?xml version=\"1.0\"?><npcs>"
			"<npc centerx=\"388\" centery=\"419\" centerz=\"5\" radius=\"5\" zone=\"market\">"
			"<npc name=\"Julian\" x=\"0\" y=\"-5\" z=\"5\" spawntime=\"60\" direction=\"2\" script=\"julian.lua\"/>"
			"<npc name=\"Orlan\" x=\"0\" y=\"0\" z=\"5\" spawntime=\"60\"/>"
			"</npc></npcs>");

		const SpawnXmlConversionResult result = SpawnXmlConverter::ConvertCanaryToTfs(monsters, npcs, output, false);
		Check(result.success && result.report.validationPassed, "Canary monster.xml and npc.xml convert and reopen successfully");
		Check(result.report.monsterAreas == 2 && result.report.npcAreas == 1, "Canary area counts are preserved");
		Check(result.report.monsters == 3 && result.report.npcs == 2, "Canary creature counts including duplicates are preserved");
		Check(result.report.additionalAttributes == 5, "additional area and creature attributes are reported");

		pugi::xml_document xml;
		Read(output, xml);
		const pugi::xml_node root = xml.document_element();
		Check(std::string(root.name()) == "spawns", "Canary conversion writes TFS root");
		Check(ChildElementCount(root, "spawn") == 3, "Canary areas are not merged by matching coordinates");
		const pugi::xml_node firstArea = root.first_child();
		Check(std::string(firstArea.attribute("radius").value()) == "0", "radius zero is copied exactly");
		Check(std::string(firstArea.attribute("customArea").value()) == "keep", "additional area attribute is copied");
		Check(ChildElementCount(firstArea, "monster") == 2, "original duplicate monsters remain duplicated");
		Check(std::string(firstArea.first_child().attribute("x").value()) == "-1", "negative x offset is not recalculated");
		Check(std::string(firstArea.first_child().attribute("custom").value()) == "\xC3\xA1\xC3\xA9", "UTF-8 and extra creature attributes are preserved");
		const pugi::xml_node npcArea = firstArea.next_sibling().next_sibling();
		Check(std::string(npcArea.attribute("centerx").value()) == "388" && std::string(npcArea.attribute("centerz").value()) == "5", "NPC center coordinates are copied from the NPC file");
		Check(std::string(npcArea.first_child().attribute("z").value()) == "5", "creature z is copied without centerz arithmetic");
		Check(std::string(npcArea.first_child().attribute("direction").value()) == "2", "NPC direction is preserved");
		Check(!npcArea.first_child().next_sibling().attribute("direction"), "missing NPC direction remains absent");
	}

	void TestOptionalCanaryFiles(const TemporaryDirectory& temporary) {
		const std::filesystem::path monsters = temporary.path / "only-monster.xml";
		const std::filesystem::path npcs = temporary.path / "only-npc.xml";
		Write(monsters, "<monsters><monster centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"><monster name=\"Rat\" x=\"0\" y=\"0\" z=\"3\" spawntime=\"30\"/></monster></monsters>");
		Write(npcs, "<npcs><npc centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"><npc name=\"Guide\" x=\"0\" y=\"0\" z=\"3\" spawntime=\"30\"/></npc></npcs>");

		SpawnXmlConversionResult result = SpawnXmlConverter::ConvertCanaryToTfs(monsters, temporary.path / "missing-npc.xml", temporary.path / "monster-only-spawn.xml", false);
		Check(result.success && result.report.monsters == 1 && result.report.npcs == 0 && result.report.warnings.size() == 1, "missing optional NPC file produces monster-only output and warning");
		result = SpawnXmlConverter::ConvertCanaryToTfs(temporary.path / "missing-monster.xml", npcs, temporary.path / "npc-only-spawn.xml", false);
		Check(result.success && result.report.monsters == 0 && result.report.npcs == 1 && result.report.warnings.size() == 1, "missing optional monster file produces NPC-only output and warning");
		result = SpawnXmlConverter::ConvertCanaryToTfs(temporary.path / "missing-a.xml", temporary.path / "missing-b.xml", temporary.path / "none.xml", false);
		Check(!result.success && !result.error.empty(), "conversion rejects two missing Canary inputs");
	}

	void TestTfsToCanary(const TemporaryDirectory& temporary) {
		const std::filesystem::path source = temporary.path / "mixed-spawn.xml";
		const std::filesystem::path monsters = temporary.path / "mixed-monster.xml";
		const std::filesystem::path npcs = temporary.path / "mixed-npc.xml";
		Write(source,
			"<spawns>"
			"<spawn centerx=\"100\" centery=\"200\" centerz=\"4\" radius=\"6\" region=\"one\">"
			"<monster name=\"First\" x=\"-2\" y=\"3\" z=\"4\" spawntime=\"10\" direction=\"3\" loot=\"yes\"/>"
			"<npc name=\"Middle\" x=\"0\" y=\"0\" z=\"4\" spawntime=\"20\" direction=\"2\"/>"
			"<monster name=\"Last\" x=\"2\" y=\"-3\" z=\"4\" spawntime=\"30\"/>"
			"</spawn>"
			"<spawn centerx=\"300\" centery=\"400\" centerz=\"8\" radius=\"1\"><npc name=\"Only NPC\" x=\"0\" y=\"0\" z=\"8\" spawntime=\"60\"/></spawn>"
			"<spawn centerx=\"500\" centery=\"600\" centerz=\"9\" radius=\"2\"><monster name=\"Only Monster\" x=\"1\" y=\"1\" z=\"9\" spawntime=\"60\"/></spawn>"
			"<spawn centerx=\"700\" centery=\"800\" centerz=\"10\" radius=\"0\"/>"
			"</spawns>");

		const SpawnXmlConversionResult result = SpawnXmlConverter::ConvertTfsToCanary(source, monsters, npcs, false);
		Check(result.success && result.report.validationPassed, "TFS combined XML splits and validates");
		Check(result.report.monsterAreas == 2 && result.report.npcAreas == 2, "mixed and single-kind TFS area counts are reported");
		Check(result.report.monsters == 3 && result.report.npcs == 2, "TFS monster and NPC counts are preserved");
		Check(result.report.warnings.size() == 1, "empty TFS area is reported and skipped");

		pugi::xml_document monsterXml;
		pugi::xml_document npcXml;
		Read(monsters, monsterXml);
		Read(npcs, npcXml);
		Check(std::string(monsterXml.document_element().name()) == "monsters" && std::string(npcXml.document_element().name()) == "npcs", "split files use correct roots");
		Check(ChildElementCount(monsterXml.document_element(), "monster") == 2 && ChildElementCount(npcXml.document_element(), "npc") == 2, "split output creates no empty or unnecessary areas");
		const pugi::xml_node monsterArea = monsterXml.document_element().first_child();
		const pugi::xml_node npcArea = npcXml.document_element().first_child();
		Check(std::string(monsterArea.attribute("region").value()) == "one" && std::string(npcArea.attribute("region").value()) == "one", "mixed area attributes are copied to both outputs");
		Check(ChildElementCount(monsterArea, "monster") == 2 && ChildElementCount(npcArea, "npc") == 1, "mixed TFS area is separated by creature kind");
		Check(std::string(monsterArea.first_child().attribute("name").value()) == "First" && std::string(monsterArea.last_child().attribute("name").value()) == "Last", "relative monster order inside a mixed area is preserved");
		Check(std::string(monsterArea.first_child().attribute("direction").value()) == "3", "monster direction is preserved");
		Check(std::string(monsterArea.first_child().attribute("loot").value()) == "yes", "additional TFS creature attribute is preserved");
	}

	void TestFailuresAndSafety(const TemporaryDirectory& temporary) {
		const std::filesystem::path output = temporary.path / "failure-output.xml";
		const auto expectFailure = [&](const std::string& filename, const std::string& xml, const std::string& label) {
			const std::filesystem::path source = temporary.path / filename;
			Write(source, xml);
			const SpawnXmlConversionResult result = SpawnXmlConverter::ConvertCanaryToTfs(source, {}, output, false);
			Check(!result.success && !result.error.empty(), label);
		};
		expectFailure("empty.xml", "", "empty XML is rejected with a detailed error");
		expectFailure("malformed.xml", "<monsters><monster>", "malformed XML is rejected with parse location");
		expectFailure("wrong-root.xml", "<spawns/>", "filename slot and XML root mismatch is rejected");
		expectFailure("missing-centerx.xml", "<monsters><monster centery=\"2\" centerz=\"3\" radius=\"4\"><monster name=\"Rat\"/></monster></monsters>", "area missing centerx is rejected");
		expectFailure("missing-name.xml", "<monsters><monster centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"><monster x=\"0\"/></monster></monsters>", "creature missing name is rejected");
		expectFailure("invalid-number.xml", "<monsters><monster centerx=\"one\" centery=\"2\" centerz=\"3\" radius=\"4\"><monster name=\"Rat\"/></monster></monsters>", "invalid numeric area attribute is rejected");
		expectFailure("invalid-creature-number.xml", "<monsters><monster centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"><monster name=\"Rat\" x=\"left\"/></monster></monsters>", "invalid numeric creature attribute is rejected");
		expectFailure("invalid-group.xml", "<monsters><npc centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"/></monsters>", "invalid Canary grouping node is rejected");

		const std::filesystem::path valid = temporary.path / "safe-monster.xml";
		Write(valid, "<monsters><monster centerx=\"1\" centery=\"2\" centerz=\"3\" radius=\"4\"><monster name=\"Rat\" x=\"0\" y=\"0\" z=\"3\" spawntime=\"30\"/></monster></monsters>");
		Write(output, "do not overwrite");
		SpawnXmlConversionResult result = SpawnXmlConverter::ConvertCanaryToTfs(valid, {}, output, false);
		Check(!result.success && result.overwriteRequired, "existing output requires explicit overwrite confirmation");
		std::ifstream unchanged(output, std::ios::binary);
		Check(std::string(std::istreambuf_iterator<char>(unchanged), std::istreambuf_iterator<char>()) == "do not overwrite", "declined overwrite leaves existing output unchanged");
		result = SpawnXmlConverter::ConvertCanaryToTfs(valid, {}, output, true);
		Check(result.success, "confirmed overwrite commits validated output");

		const std::filesystem::path canceledOutput = temporary.path / "canceled.xml";
		result = SpawnXmlConverter::ConvertCanaryToTfs(valid, {}, canceledOutput, false, [](int progress, const std::string&) { return progress < 80; });
		Check(result.canceled && !std::filesystem::exists(canceledOutput), "cancel before writing leaves no output or staged XML");
		result = SpawnXmlConverter::ConvertCanaryToTfs(valid, {}, valid, true);
		Check(!result.success, "source and destination equality is rejected");
	}

	void TestRoundTrip(const TemporaryDirectory& temporary) {
		const std::filesystem::path sourceMonster = temporary.path / "round-monster.xml";
		const std::filesystem::path sourceNpc = temporary.path / "round-npc.xml";
		const std::filesystem::path combined = temporary.path / "round-spawn.xml";
		const std::filesystem::path finalMonster = temporary.path / "round-final-monster.xml";
		const std::filesystem::path finalNpc = temporary.path / "round-final-npc.xml";
		Write(sourceMonster, "<monsters><monster centerx=\"10\" centery=\"20\" centerz=\"6\" radius=\"3\"><monster name=\"A\" x=\"-1\" y=\"2\" z=\"006\" spawntime=\"060\"/><monster name=\"A\" x=\"-1\" y=\"2\" z=\"006\" spawntime=\"060\"/></monster></monsters>");
		Write(sourceNpc, "<npcs><npc centerx=\"30\" centery=\"40\" centerz=\"7\" radius=\"4\"><npc name=\"B\" x=\"0\" y=\"0\" z=\"7\" spawntime=\"60\" direction=\"0\"/></npc></npcs>");
		SpawnXmlConversionResult result = SpawnXmlConverter::ConvertCanaryToTfs(sourceMonster, sourceNpc, combined, false);
		Check(result.success, "forward half of round trip succeeds");
		result = SpawnXmlConverter::ConvertTfsToCanary(combined, finalMonster, finalNpc, false);
		Check(result.success && result.report.monsters == 2 && result.report.npcs == 1, "round trip keeps all monster and NPC records");
		pugi::xml_document monsterXml;
		Read(finalMonster, monsterXml);
		const pugi::xml_node first = monsterXml.document_element().first_child().first_child();
		Check(std::string(first.attribute("z").value()) == "006" && std::string(first.attribute("spawntime").value()) == "060", "round trip preserves numeric attribute text without normalization");
		Check(ChildElementCount(monsterXml.document_element().first_child(), "monster") == 2, "round trip neither removes nor creates duplicates");
	}
}

int main() {
	TemporaryDirectory temporary;
	TestNamingAndInspection(temporary);
	TestCanaryToTfs(temporary);
	TestOptionalCanaryFiles(temporary);
	TestTfsToCanary(temporary);
	TestFailuresAndSafety(temporary);
	TestRoundTrip(temporary);
	if (failures == 0) {
		std::cout << "All spawn XML converter tests passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
