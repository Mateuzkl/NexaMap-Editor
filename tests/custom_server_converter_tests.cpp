#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ext/pugixml.hpp"
#include "custom_server_converter.h"
#include "file_transaction.h"
#include "filehandle.h"
#include "items_otb_id_converter.h"
#include "items_xml_id_converter.h"
#include "map_item_id_converter.h"
#include "otb_item_id_mapping_provider.h"

// filehandle.cpp normally gets these helpers from the application target.
std::string i2s(int value) {
	return std::to_string(value);
}

std::wstring string2wstring(const std::string& value) {
	return std::filesystem::u8path(value).wstring();
}

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	class TemporaryDirectory final {
	public:
		TemporaryDirectory() {
			static std::atomic_uint64_t sequence = 0;
			const std::filesystem::path root = std::filesystem::temp_directory_path();
			for (unsigned int attempt = 0; attempt < 128; ++attempt) {
				path = root / ("nexamap-custom-converter-test-" + std::to_string(sequence.fetch_add(1)));
				std::error_code error;
				if (std::filesystem::create_directory(path, error)) {
					return;
				}
			}
			throw std::runtime_error("Could not create a unique test directory.");
		}

		~TemporaryDirectory() {
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		}

		TemporaryDirectory(const TemporaryDirectory&) = delete;
		TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

		std::filesystem::path path;
	};

	void WriteOtb(const std::filesystem::path& path, bool corruptLength = false) {
		DiskNodeFileWriteHandle output(path.string(), "OTBI");
		output.addNode(0);
		output.addU32(0);
		output.addU8(0x01);
		output.addU16(140);
		output.addU32(3);
		output.addU32(860);
		output.addU32(1);
		std::string version(128, '\0');
		output.addRAW(version);

		output.addNode(1);
		output.addU32(0);
		output.addU8(0x10);
		output.addU16(corruptLength ? 1 : 2);
		if (corruptLength) {
			output.addU8(10);
		} else {
			output.addU16(1000);
		}
		output.addU8(0x99);
		output.addU16(3);
		const uint8_t unknown[] { 1, 2, 3 };
		output.addRAW(unknown, 3);
		output.addU8(0x11);
		output.addU16(2);
		output.addU16(3000);
		output.endNode();
		output.endNode();
		output.close();
	}

	void WriteAliasOtb(const std::filesystem::path& path) {
		DiskNodeFileWriteHandle output(path.string(), "OTBI");
		output.addNode(0);
		output.addU32(0);
		output.addU8(0x01);
		output.addU16(140);
		output.addU32(3);
		output.addU32(860);
		output.addU32(1);
		std::string version(128, '\0');
		output.addRAW(version);
		for (const auto& [serverId, clientId] : std::vector<std::pair<uint16_t, uint16_t>> { { 1000, 3000 }, { 3000, 3000 } }) {
			output.addNode(1);
			output.addU32(0x1234);
			output.addU8(0x10);
			output.addU16(2);
			output.addU16(serverId);
			output.addU8(0x11);
			output.addU16(2);
			output.addU16(clientId);
			output.addU8(0x22);
			output.addU16(3);
			const uint8_t unknown[] { 7, 8, 9 };
			output.addRAW(unknown, 3);
			output.endNode();
		}
		output.addNode(14);
		output.addU32(0);
		output.addU8(0x10);
		output.addU16(2);
		output.addU16(2000);
		output.endNode();
		output.endNode();
		output.close();
	}

	void TestMappingProvider() {
		auto loaded = OtbItemIdMappingProvider::FromPairs({ { 1000, 3000 }, { 1001, 8500 }, { 1002, 3001 }, { 4000, 4000 } });
		check(loaded.ready(), "one-to-one and identity mappings validate");
		check(loaded.provider->convert(1000, ItemIdMapping::Direction::ServerToClient).converted == 3000, "forward mapping works");
		check(loaded.provider->convert(3000, ItemIdMapping::Direction::ClientToServer).converted == 1000, "reverse mapping works");
		check(!loaded.provider->convert(999, ItemIdMapping::Direction::ServerToClient).found, "missing ServerID is reported");
		check(loaded.provider->convert(4000, ItemIdMapping::Direction::ServerToClient).found && loaded.provider->convert(4000, ItemIdMapping::Direction::ServerToClient).converted == 4000, "identity mapping is found and unchanged");

		check(!OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 1, 11 } }).ready(), "duplicate ServerID blocks mapping");
		check(!OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 10 } }).ready(), "duplicate ClientID blocks mapping");
		auto aliases = OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 10 } }, "allowed aliases", true);
		check(aliases.ready(), "ClientID aliases can be explicitly allowed for forward map conversion");
		check(aliases.provider->convert(10, ItemIdMapping::Direction::ClientToServer).ambiguous, "allowed alias remains diagnostically ambiguous in reverse");
		check(!OtbItemIdMappingProvider::FromPairs({ { 0, 10 } }).ready(), "zero ServerID blocks mapping");
		check(!OtbItemIdMappingProvider::FromPairs({ { 1, 0 } }).ready(), "zero ClientID blocks mapping");
	}

	void TestOtbReader() {
		TemporaryDirectory temporary;
		const auto validPath = temporary.path / "valid.otb";
		const auto invalidPath = temporary.path / "invalid.otb";
		WriteOtb(validPath);
		WriteOtb(invalidPath, true);
		const auto valid = OtbItemIdMappingProvider::Load(validPath);
		check(valid.ready(), "OTB reader accepts mapping and skips unknown attributes");
		check(valid.provider && valid.provider->convert(1000, ItemIdMapping::Direction::ServerToClient).converted == 3000, "OTB reader extracts ServerID and ClientID");
		const auto invalid = OtbItemIdMappingProvider::Load(invalidPath);
		check(!invalid.ready(), "invalid OTB ID attribute length is rejected");
	}

	void TestOtbIdentityConversion() {
		TemporaryDirectory temporary;
		const auto source = temporary.path / "alias-source.otb";
		const auto destination = temporary.path / "alias-clientid.otb";
		WriteAliasOtb(source);
		const auto report = ConvertItemsOtbToClientIds(source, destination);
		check(report.success && report.outputValidated, "items.otb identity conversion validates");
		check(report.sourceNodes == 3 && report.writtenItems == 1, "items.otb aliases are deduplicated");
		check(report.aliasesMerged == 1 && report.deprecatedItemsPreserved == 1, "items.otb alias and deprecated counts are reported");
		const auto converted = OtbItemIdMappingProvider::Load(destination);
		check(converted.ready() && converted.stats.entries == 1, "converted items.otb reopens with one canonical item");
		const auto identity = converted.provider->convert(3000, ItemIdMapping::Direction::ServerToClient);
		check(identity.found && identity.converted == 3000, "converted items.otb writes ServerID equal to ClientID");
	}

	pugi::xml_node FindAttribute(pugi::xml_node item, const char* key) {
		return item.find_child_by_attribute("attribute", "key", key);
	}

	void TestXmlReferenceKeys() {
		auto mapping = OtbItemIdMappingProvider::FromPairs({
			{ 1000, 3000 },
			{ 1001, 3001 },
			{ 1002, 3002 },
			{ 1003, 3003 },
			{ 1004, 3004 },
			{ 1005, 3005 },
			{ 1006, 3006 },
			{ 1007, 3007 },
			{ 1008, 3008 },
			{ 1009, 3009 },
			{ 1010, 3010 },
			{ 1011, 3011 },
		});
		check(mapping.ready(), "XML reference mapping validates");
		pugi::xml_document document;
		const char* xml = R"xml(<?xml version="1.0"?>
<items>
  <!-- comentário preservado -->
  <item id="1000" name="Poção Mágica">
    <attribute key="rotateTo" value="1001"/>
    <attribute key="writeOnceItemId" value="1002"/>
    <attribute key="decayTo" value="-1"/>
    <attribute key="transformEquipTo" value="1003"/>
    <attribute key="transformDeEquipTo" value="1004"/>
    <attribute key="maleTransformTo" value="1005"/>
    <attribute key="maleSleeper" value="1006"/>
    <attribute key="femaleTransformTo" value="1007"/>
    <attribute key="femaleSleeper" value="1008"/>
    <attribute key="transformTo" value="1009"/>
    <attribute key="destroyTo" value="1010"/>
    <attribute key="attack" value="1001"/>
    <attribute key="defense" value="1002"/>
    <attribute key="armor" value="1003"/>
    <attribute key="weight" value="1004"/>
    <attribute key="duration" value="1005"/>
    <attribute key="charges" value="1006"/>
    <attribute key="speed" value="1007"/>
    <attribute key="range" value="1008"/>
    <attribute key="chance" value="1009"/>
    <attribute key="leveldoor" value="1010"/>
    <custom value="unchanged"/>
  </item>
  <item fromid="1010" toid="1011" article="a"><attribute key="decayTo" value="0"/></item>
</items>)xml";
		check(document.load_buffer(xml, std::strlen(xml), pugi::parse_full, pugi::encoding_utf8), "XML fixture parses");
		const auto report = ConvertItemsXmlDocument(document, *mapping.provider, true);
		check(report.success, "semantic XML conversion succeeds");
		check(report.declarations == 3 && report.mappedDeclarations == 3, "logical declaration count is preserved");
		const pugi::xml_node item = document.child("items").child("item");
		check(item.attribute("id").as_uint() == 3000, "single item ID is converted");
		const std::vector<std::pair<const char*, const char*>> references {
			{ "rotateTo", "3001" },
			{ "writeOnceItemId", "3002" },
			{ "transformEquipTo", "3003" },
			{ "transformDeEquipTo", "3004" },
			{ "maleTransformTo", "3005" },
			{ "maleSleeper", "3006" },
			{ "femaleTransformTo", "3007" },
			{ "femaleSleeper", "3008" },
			{ "transformTo", "3009" },
			{ "destroyTo", "3010" },
		};
		for (const auto& [key, value] : references) {
			check(std::string(FindAttribute(item, key).attribute("value").value()) == value, key);
		}
		check(std::string(FindAttribute(item, "decayTo").attribute("value").value()) == "-1", "negative sentinel is preserved");
		for (const char* key : { "attack", "defense", "armor", "weight", "duration", "charges", "speed", "range", "chance", "leveldoor" }) {
			check(std::string(FindAttribute(item, key).attribute("value").value()).starts_with("10"), "ordinary numeric field is unchanged");
		}
		check(item.child("custom"), "unknown child XML is preserved");
		check(std::string(item.attribute("name").value()) == "Poção Mágica", "Unicode text is preserved");
	}

	void TestXmlRangesAndCollisions() {
		auto mapping = OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 30 }, { 3, 31 }, { 4, 40 } });
		pugi::xml_document expanded;
		expanded.load("<items><item fromid='1' toid='3'/></items>");
		const auto expandedReport = ConvertItemsXmlDocument(expanded, *mapping.provider, true);
		check(expandedReport.success && expandedReport.expandedRanges == 1, "non-contiguous ranges expand deterministically");

		pugi::xml_document malformed;
		malformed.load("<items><item fromid='bad' toid='3'/></items>");
		check(!ConvertItemsXmlDocument(malformed, *mapping.provider, true).success, "malformed range is blocked");
		auto aliases = OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 10 } }, "allowed aliases", true);
		pugi::xml_document malformedWithAliases;
		malformedWithAliases.load("<items><item fromid='bad' toid='2'/></items>");
		check(!ConvertItemsXmlDocument(malformedWithAliases, *aliases.provider, true).success, "alias mode cannot permit a malformed range");
		pugi::xml_document compatibleMalformed;
		compatibleMalformed.load("<items><item fromid='bad' toid='2'/></items>");
		check(ConvertItemsXmlDocument(compatibleMalformed, *aliases.provider, true, true, true).success, "explicit compatibility override preserves malformed ranges");

		pugi::xml_document collision;
		collision.load("<items><item id='1'/><item id='2'/></items>");
		const auto collisionReport = ConvertItemsXmlDocument(collision, *aliases.provider, true);
		check(!collisionReport.success && collisionReport.collisions == 1, "ClientID alias declarations remain blocking");
		pugi::xml_document compatibleCollision;
		compatibleCollision.load("<items><item id='1'/><item id='2'/></items>");
		const auto compatibleCollisionReport = ConvertItemsXmlDocument(compatibleCollision, *aliases.provider, true, true, true);
		check(compatibleCollisionReport.success && compatibleCollisionReport.collisions == 1, "explicit compatibility override allows ClientID alias declarations");

		pugi::xml_document duplicateSource;
		duplicateSource.load("<items><item id='4'/><item id='4'/></items>");
		check(!ConvertItemsXmlDocument(duplicateSource, *mapping.provider, true).success, "duplicate destination IDs block even when source IDs are identical");
	}

	void TestXmlFileSemanticValidation() {
		TemporaryDirectory temporary;
		const auto source = temporary.path / std::filesystem::u8path(u8"itens-ação.xml");
		const auto destination = temporary.path / std::filesystem::u8path(u8"saída-convertida.xml");
		{
			std::ofstream output(source, std::ios::binary);
			output << "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?><items><!--preserve--><item id=\"1\" name=\"Ação\"><attribute key=\"transformTo\" value=\"2\"/></item><item id=\"2\"/></items>";
		}
		auto mapping = OtbItemIdMappingProvider::FromPairs({ { 1, 101 }, { 2, 102 } });
		const auto report = ConvertItemsXmlFile(source, destination, *mapping.provider, true);
		check(report.success && report.outputValidated, "written items.xml passes semantic validation");
		std::ifstream input(destination, std::ios::binary);
		const std::string output { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		check(output.starts_with("\xEF\xBB\xBF"), "UTF-8 BOM is preserved");
		check(output.find("Ação") != std::string::npos, "accented text survives file conversion");
	}

	void TestXmlCompatibilityFileValidation() {
		TemporaryDirectory temporary;
		const auto source = temporary.path / "compatibility-items.xml";
		const auto destination = temporary.path / "compatibility-items-converted.xml";
		{
			std::ofstream output(source, std::ios::binary);
			output << R"xml(<?xml version="1.0" encoding="UTF-8"?>
<items>
	<!-- formatting whitespace must not affect semantic validation -->
	<item id="1" name="canonical">
		&gt;
	</item>
	<item id="2" name="alias" />
	<item fromid="98259825" toid="9828" name="preserved invalid range" />
</items>)xml";
		}
		auto aliases = OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 10 } }, "compatibility aliases", true);
		const auto report = ConvertItemsXmlFile(source, destination, *aliases.provider, true, true, true);
		check(report.success && report.outputValidated, "compatibility XML survives semantic file validation");
		check(report.collisions == 1, "compatibility XML reports its allowed alias collision");
	}

	void TestPathSafety() {
		TemporaryDirectory temporary;
		const auto parent = temporary.path / std::filesystem::u8path(u8"Servidor Ação");
		const auto child = parent / "data" / "world";
		check(FileSaveTransaction::IsSameOrWithin(child / ".." / "items", parent), "relative dot segments cannot bypass containment");
		check(FileSaveTransaction::PathsOverlap(parent, child), "parent and child paths overlap");
		check(!FileSaveTransaction::PathsOverlap(parent, temporary.path / "other"), "separate sibling paths do not overlap");
#ifdef _WIN32
		check(FileSaveTransaction::IsSameOrWithin("c:\\dbo\\server\\output", "C:\\DBO\\Server"), "Windows containment is case-insensitive");
		check(!FileSaveTransaction::PathsOverlap("C:\\DBO\\Server", "D:\\DBO\\Server"), "paths on different Windows drives do not overlap");
#endif
	}

	void TestConversionScope() {
		check(CustomServerConversionScope {}.any(), "default scope converts maps, items.xml, and items.otb");
		check(!CustomServerConversionScope { false, false, false }.any(), "empty conversion scope is rejected by the core predicate");
		check(CustomServerConversionScope { true, false, false }.any(), "map-only scope is valid");
		check(CustomServerConversionScope { false, true, false }.any(), "XML-only scope is valid");
		check(CustomServerConversionScope { false, false, true }.any(), "OTB-only scope is valid");
	}

	void TestStandardStreamingPolicy() {
		check(ShouldAttemptMapItemIdStreaming(true, true, false, false), "standard converter retains same-directory streaming");
		check(!ShouldAttemptMapItemIdStreaming(true, false, false, false), "standard converter retains cross-directory native fallback");
		check(ShouldAttemptMapItemIdStreaming(true, false, true, false), "custom converter can explicitly request detached streaming");
		check(ShouldAttemptMapItemIdStreaming(true, false, false, true), "preflight can inspect a map without a destination");
		check(!ShouldAttemptMapItemIdStreaming(false, true, true, true), "version mismatch never uses raw streaming");
	}
}

int main(int argc, char** argv) {
	if (argc >= 2) {
		const bool allowDuplicates = argc >= 3 && std::string(argv[2]) == "--allow-duplicates";
		const auto loaded = OtbItemIdMappingProvider::Load(std::filesystem::u8path(argv[1]), allowDuplicates);
		std::cout << "entries=" << loaded.stats.entries
				  << " serverIds=" << loaded.stats.uniqueServerIds
				  << " clientIds=" << loaded.stats.uniqueClientIds
				  << " duplicateServerIds=" << loaded.stats.duplicateServerIds
				  << " duplicateClientIds=" << loaded.stats.duplicateClientIds
				  << " issues=" << loaded.issues.size() << '\n';
		if (!loaded.ready()) {
			std::cerr << loaded.error << '\n';
			return 2;
		}
		if (argc >= 4) {
			pugi::xml_document document;
			const auto parsed = document.load_file(argv[3], pugi::parse_full, pugi::encoding_auto);
			if (!parsed) {
				std::cerr << "Could not parse diagnostic items.xml: " << parsed.description() << '\n';
				return 3;
			}
			const auto xml = ConvertItemsXmlDocument(document, *loaded.provider, true, allowDuplicates, allowDuplicates);
			std::cout << "xmlDeclarations=" << xml.declarations << " mapped=" << xml.mappedDeclarations
					  << " missing=" << xml.missingDeclarations << " referencesMissing=" << xml.missingReferences
					  << " collisions=" << xml.collisions << '\n';
			if (!xml.success) {
				std::cerr << xml.error << '\n';
				return 4;
			}
			if (argc >= 5) {
				const auto converted = ConvertItemsXmlFile(
					std::filesystem::u8path(argv[3]),
					std::filesystem::u8path(argv[4]),
					*loaded.provider,
					true,
					allowDuplicates,
					allowDuplicates
				);
				std::cout << "xmlOutputValidated=" << converted.outputValidated << '\n';
				if (!converted.success) {
					std::cerr << converted.error << '\n';
					return 5;
				}
			}
		}
		return 0;
	}
	TestMappingProvider();
	TestOtbReader();
	TestOtbIdentityConversion();
	TestXmlReferenceKeys();
	TestXmlRangesAndCollisions();
	TestXmlFileSemanticValidation();
	TestXmlCompatibilityFileValidation();
	TestPathSafety();
	TestConversionScope();
	TestStandardStreamingPolicy();
	if (failures != 0) {
		std::cerr << failures << " custom converter test(s) failed.\n";
		return 1;
	}
	std::cout << "Custom server converter tests passed.\n";
	return 0;
}
