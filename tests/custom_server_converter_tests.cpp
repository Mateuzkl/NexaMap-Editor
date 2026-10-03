#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ext/pugixml.hpp"
#include "filehandle.h"
#include "items_xml_id_converter.h"
#include "items_otb_id_converter.h"
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

	std::filesystem::path TempFile(const char* name) {
		return std::filesystem::temp_directory_path() / name;
	}

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
		check(aliases.ready(), "duplicate ClientID aliases can be explicitly allowed");
		check(aliases.provider->convert(10, ItemIdMapping::Direction::ClientToServer).ambiguous, "allowed alias remains diagnostically ambiguous in reverse");
		check(!OtbItemIdMappingProvider::FromPairs({ { 0, 10 } }).ready(), "zero ServerID blocks mapping");
		check(!OtbItemIdMappingProvider::FromPairs({ { 1, 0 } }).ready(), "zero ClientID blocks mapping");
	}

	void TestOtbReader() {
		const auto validPath = TempFile("nexamap_custom_mapping_valid.otb");
		const auto invalidPath = TempFile("nexamap_custom_mapping_invalid.otb");
		WriteOtb(validPath);
		WriteOtb(invalidPath, true);
		const auto valid = OtbItemIdMappingProvider::Load(validPath);
		check(valid.ready(), "OTB reader accepts mapping and skips unknown attributes");
		check(valid.provider && valid.provider->convert(1000, ItemIdMapping::Direction::ServerToClient).converted == 3000, "OTB reader extracts ServerID and ClientID");
		const auto invalid = OtbItemIdMappingProvider::Load(invalidPath);
		check(!invalid.ready(), "invalid OTB ID attribute length is rejected");
		std::error_code ignored;
		std::filesystem::remove(validPath, ignored);
		std::filesystem::remove(invalidPath, ignored);
	}

	void TestOtbIdentityConversion() {
		const auto source = TempFile("nexamap_custom_alias_source.otb");
		const auto destination = TempFile("nexamap_custom_alias_clientid.otb");
		WriteAliasOtb(source);
		const auto report = ConvertItemsOtbToClientIds(source, destination);
		check(report.success && report.outputValidated, "items.otb identity conversion validates");
		check(report.sourceNodes == 3 && report.writtenItems == 1, "items.otb aliases are deduplicated");
		check(report.aliasesMerged == 1 && report.deprecatedItemsPreserved == 1, "items.otb alias and deprecated counts are reported");
		const auto converted = OtbItemIdMappingProvider::Load(destination);
		check(converted.ready() && converted.stats.entries == 1, "converted items.otb reopens with one canonical item");
		const auto identity = converted.provider->convert(3000, ItemIdMapping::Direction::ServerToClient);
		check(identity.found && identity.converted == 3000, "converted items.otb writes ServerID equal to ClientID");
		std::error_code ignored;
		std::filesystem::remove(source, ignored);
		std::filesystem::remove(destination, ignored);
	}

	void TestXmlConversion() {
		auto mapping = OtbItemIdMappingProvider::FromPairs({
			{ 1000, 3000 }, { 1001, 8500 }, { 1002, 3001 }, { 1003, 3002 }, { 1004, 3003 }, { 4000, 4000 }
		});
		check(mapping.ready(), "XML test mapping validates");
		pugi::xml_document document;
		const char* xml = R"xml(<?xml version="1.0"?>
<items>
  <!-- preserved -->
  <item id="1000" name="Custom">
    <attribute key="rotateTo" value="1001"/>
    <attribute key="writeOnceItemId" value="1002"/>
    <attribute key="decayTo" value="-1"/>
    <attribute key="transformEquipTo" value="1003"/>
    <attribute key="transformDeEquipTo" value="1004"/>
    <attribute key="attack" value="1001"/>
    <custom value="unchanged"/>
  </item>
  <item fromid="1001" toid="1003" article="a"><attribute key="decayTo" value="0"/></item>
  <item id="4000"/>
</items>)xml";
	check(document.load_buffer(xml, std::strlen(xml), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8), "XML fixture parses");
	const auto report = ConvertItemsXmlDocument(document, *mapping.provider, true);
	check(report.success, "semantic XML conversion succeeds");
	check(report.expandedRanges == 1, "non-contiguous range expands");
	check(report.declarations == 5 && report.mappedDeclarations == 5, "logical declaration count is preserved");
	const pugi::xml_node first = document.child("items").child("item");
	check(first.attribute("id").as_uint() == 3000, "single item ID is converted");
	check(std::string(first.child("attribute").attribute("value").value()) == "8500", "rotateTo is converted case-insensitively");
	check(std::string(first.find_child_by_attribute("attribute", "key", "attack").attribute("value").value()) == "1001", "ordinary numeric attributes are untouched");
	check(std::string(first.find_child_by_attribute("attribute", "key", "decayTo").attribute("value").value()) == "-1", "negative sentinel is preserved");
	check(first.child("custom"), "unknown child XML is preserved");

		pugi::xml_document missingDocument;
		constexpr auto missingXml = "<items><item id='999'/></items>";
		missingDocument.load_buffer(missingXml, std::strlen(missingXml), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8);
		const auto missing = ConvertItemsXmlDocument(missingDocument, *mapping.provider, true);
		check(!missing.success && missing.missingDeclarations == 1, "strict XML conversion blocks a missing ID");

		auto aliases = OtbItemIdMappingProvider::FromPairs({ { 1, 10 }, { 2, 10 } }, "XML aliases", true);
		constexpr auto collisionXml = "<items><item id='1'/><item id='2'/></items>";
		pugi::xml_document collisionDocument;
		collisionDocument.load_buffer(collisionXml, std::strlen(collisionXml));
		check(!ConvertItemsXmlDocument(collisionDocument, *aliases.provider, true).success, "XML ClientID collision blocks by default");
		pugi::xml_document allowedCollisionDocument;
		allowedCollisionDocument.load_buffer(collisionXml, std::strlen(collisionXml));
		const auto allowedCollision = ConvertItemsXmlDocument(allowedCollisionDocument, *aliases.provider, true, true);
		check(allowedCollision.success && allowedCollision.collisions == 1, "XML ClientID collision can be explicitly allowed");
	}
}

int main(int argc, char** argv) {
	if (argc == 4 && std::string(argv[2]) == "--normalize-otb") {
		const auto report = ConvertItemsOtbToClientIds(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[3]));
		std::cout << "sourceNodes=" << report.sourceNodes
			<< " writtenItems=" << report.writtenItems
			<< " aliasesMerged=" << report.aliasesMerged
			<< " deprecatedPreserved=" << report.deprecatedItemsPreserved
			<< " validated=" << report.outputValidated << '\n';
		if (!report.success) {
			std::cerr << report.error << '\n';
			return 5;
		}
		return 0;
	}
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
			for (std::size_t index = 0; index < std::min<std::size_t>(loaded.issues.size(), 12); ++index) {
				std::cerr << loaded.issues[index].message << " server=" << loaded.issues[index].serverId << " client=" << loaded.issues[index].clientId << '\n';
			}
			return 2;
		}
		if (argc >= 4) {
			pugi::xml_document document;
			const auto parsed = document.load_file(argv[3], pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8);
			if (!parsed) {
				std::cerr << "Could not parse diagnostic items.xml: " << parsed.description() << '\n';
				return 3;
			}
			const auto xml = ConvertItemsXmlDocument(document, *loaded.provider, true, allowDuplicates);
			std::cout << "xmlDeclarations=" << xml.declarations << " mapped=" << xml.mappedDeclarations
				<< " missing=" << xml.missingDeclarations << " referencesMissing=" << xml.missingReferences
				<< " collisions=" << xml.collisions << '\n';
			if (!xml.success) {
				std::cerr << xml.error << '\n';
				return 4;
			}
		}
		return 0;
	}
	TestMappingProvider();
	TestOtbReader();
	TestOtbIdentityConversion();
	TestXmlConversion();
	if (failures != 0) {
		std::cerr << failures << " custom converter test(s) failed.\n";
		return 1;
	}
	std::cout << "Custom server converter tests passed.\n";
	return 0;
}
