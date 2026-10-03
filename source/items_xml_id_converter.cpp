//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "items_xml_id_converter.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {
	std::string Lowercase(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	bool ParseInteger(const char* text, int64_t& value) {
		if (!text || *text == '\0') {
			return false;
		}
		const char* end = text + std::char_traits<char>::length(text);
		const auto result = std::from_chars(text, end, value);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool IsItemReferenceKey(const char* key) {
		static constexpr std::array<const char*, 11> keys {
			"rotateto",
			"writeonceitemid",
			"decayto",
			"transformequipto",
			"transformdeequipto",
			"maletransformto",
			"malesleeper",
			"femaletransformto",
			"femalesleeper",
			"transformto",
			"destroyto",
		};
		const std::string normalized = Lowercase(key ? key : "");
		return std::find(keys.begin(), keys.end(), normalized) != keys.end();
	}

	void AddMissing(ItemsXmlIdConversionReport& report, uint16_t sourceId, std::string context, bool reference) {
		if (reference) {
			++report.missingReferences;
		} else {
			++report.missingDeclarations;
		}
		std::ostringstream message;
		message << "No items.otb mapping exists for ServerID " << sourceId << '.';
		report.issues.push_back({ sourceId, std::move(context), message.str() });
	}

	bool MapId(const ItemIdMappingProvider& provider, uint16_t sourceId, uint16_t& destinationId) {
		const ItemIdMapping::Result mapping = provider.convert(sourceId, ItemIdMapping::Direction::ServerToClient);
		if (!mapping.found || mapping.ambiguous) {
			return false;
		}
		destinationId = mapping.converted;
		return true;
	}

	void ConvertReferenceAttributes(pugi::xml_node item, const ItemIdMappingProvider& provider, ItemsXmlIdConversionReport& report) {
		for (pugi::xml_node attribute : item.children("attribute")) {
			if (!IsItemReferenceKey(attribute.attribute("key").value())) {
				continue;
			}
			pugi::xml_attribute valueAttribute = attribute.attribute("value");
			int64_t value = 0;
			if (!valueAttribute || !ParseInteger(valueAttribute.value(), value)) {
				continue;
			}
			// Preserve TFS sentinel values such as decayTo=-1 and zero-valued optional references.
			if (value <= 0 || value > std::numeric_limits<uint16_t>::max()) {
				continue;
			}
			uint16_t converted = 0;
			if (!MapId(provider, static_cast<uint16_t>(value), converted)) {
				AddMissing(report, static_cast<uint16_t>(value), std::string("attribute ") + attribute.attribute("key").value(), true);
				continue;
			}
			if (converted != value) {
				valueAttribute.set_value(converted);
				++report.convertedReferences;
			}
		}
	}

	bool ReadFile(const std::filesystem::path& path, std::string& contents, std::string& error) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			error = "Could not open items.xml.";
			return false;
		}
		contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		if (!input.good() && !input.eof()) {
			error = "Could not read items.xml.";
			return false;
		}
		return true;
	}

	struct XmlSemanticSummary {
		std::vector<uint16_t> declarationIds;
		std::string error;
	};

	bool IsFormattingWhitespace(const pugi::xml_node& node) {
		if (node.type() == pugi::node_pcdata) {
			const std::string_view value(node.value());
			return std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
		}
		return false;
	}

	pugi::xml_node NextSemanticChild(pugi::xml_node child) {
		while (child && IsFormattingWhitespace(child)) {
			child = child.next_sibling();
		}
		return child;
	}

	std::string_view SemanticNodeValue(const pugi::xml_node& node) {
		std::string_view value(node.value());
		if (node.type() != pugi::node_pcdata) {
			return value;
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
			value.remove_prefix(1);
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
			value.remove_suffix(1);
		}
		return value;
	}

	bool SemanticNodesEqual(const pugi::xml_node& expected, const pugi::xml_node& actual, const std::string& path, std::string& error) {
		const std::string_view expectedValue = SemanticNodeValue(expected);
		const std::string_view actualValue = SemanticNodeValue(actual);
		if (expected.type() != actual.type() || std::string_view(expected.name()) != actual.name() || expectedValue != actualValue) {
			error = "Generated items.xml changed XML content at " + path + " (node types " + std::to_string(expected.type()) + " and " + std::to_string(actual.type()) + ", value lengths " + std::to_string(expectedValue.size()) + " and " + std::to_string(actualValue.size()) + ").";
			return false;
		}

		std::size_t expectedAttributeCount = 0;
		for (pugi::xml_attribute attribute : expected.attributes()) {
			++expectedAttributeCount;
			const pugi::xml_attribute reparsed = actual.attribute(attribute.name());
			if (!reparsed || std::string_view(attribute.value()) != reparsed.value()) {
				error = "Generated items.xml changed attribute '" + std::string(attribute.name()) + "' at " + path + ".";
				return false;
			}
		}
		std::size_t actualAttributeCount = 0;
		for ([[maybe_unused]] pugi::xml_attribute attribute : actual.attributes()) {
			++actualAttributeCount;
		}
		if (expectedAttributeCount != actualAttributeCount) {
			error = "Generated items.xml changed the attribute count at " + path + ".";
			return false;
		}

		pugi::xml_node expectedChild = NextSemanticChild(expected.first_child());
		pugi::xml_node actualChild = NextSemanticChild(actual.first_child());
		std::size_t childIndex = 0;
		while (expectedChild && actualChild) {
			++childIndex;
			const std::string childName = *expectedChild.name() ? expectedChild.name() : "node";
			if (!SemanticNodesEqual(expectedChild, actualChild, path + "/" + childName + "[" + std::to_string(childIndex) + "]", error)) {
				return false;
			}
			expectedChild = NextSemanticChild(expectedChild.next_sibling());
			actualChild = NextSemanticChild(actualChild.next_sibling());
		}
		if (expectedChild || actualChild) {
			error = "Generated items.xml changed the child-node count at " + path + ".";
			return false;
		}
		return true;
	}

	XmlSemanticSummary SummarizeDocument(const pugi::xml_document& document, bool allowDestinationCollisions, bool allowMalformedRanges) {
		XmlSemanticSummary summary;
		const pugi::xml_node items = document.child("items");
		if (!items) {
			summary.error = "items.xml does not contain an <items> root element.";
			return summary;
		}
		std::unordered_set<uint16_t> seen;
		for (pugi::xml_node item : items.children("item")) {
			const pugi::xml_attribute id = item.attribute("id");
			const pugi::xml_attribute from = item.attribute("fromid");
			const pugi::xml_attribute to = item.attribute("toid");
			int64_t first = 0;
			int64_t last = 0;
			if (id && !from && !to) {
				if (!ParseInteger(id.value(), first) || first <= 0 || first > std::numeric_limits<uint16_t>::max()) {
					summary.error = "Generated items.xml contains an invalid item id declaration.";
					return summary;
				}
				last = first;
			} else if (!id && from && to) {
				if (!ParseInteger(from.value(), first) || !ParseInteger(to.value(), last) || first <= 0 || last < first || last > std::numeric_limits<uint16_t>::max()) {
					if (allowMalformedRanges) {
						continue;
					}
					summary.error = "Generated items.xml contains an invalid fromid/toid declaration.";
					return summary;
				}
			} else {
				summary.error = "Generated items.xml contains an ambiguous or incomplete item declaration.";
				return summary;
			}
			for (int64_t value = first; value <= last; ++value) {
				const uint16_t itemId = static_cast<uint16_t>(value);
				if (!seen.insert(itemId).second && !allowDestinationCollisions) {
					summary.error = "Generated items.xml contains duplicate destination item IDs.";
					return summary;
				}
				summary.declarationIds.push_back(itemId);
			}
		}
		return summary;
	}
}

ItemsXmlIdConversionReport ConvertItemsXmlDocument(
	pugi::xml_document& document,
	const ItemIdMappingProvider& provider,
	bool strictMapping,
	bool allowDestinationCollisions,
	bool allowMalformedRanges
) {
	ItemsXmlIdConversionReport report;
	if (!provider.validate()) {
		report.error = "The selected item ID mapping is invalid.";
		return report;
	}
	pugi::xml_node items = document.child("items");
	if (!items) {
		report.error = "items.xml does not contain an <items> root element.";
		return report;
	}

	std::unordered_map<uint16_t, uint16_t> destinationSources;
	for (pugi::xml_node item = items.child("item"); item;) {
		pugi::xml_node next = item.next_sibling("item");
		pugi::xml_attribute idAttribute = item.attribute("id");
		pugi::xml_attribute fromAttribute = item.attribute("fromid");
		pugi::xml_attribute toAttribute = item.attribute("toid");
		std::vector<std::pair<uint16_t, uint16_t>> mapped;

		if (idAttribute) {
			int64_t sourceValue = 0;
			if (!ParseInteger(idAttribute.value(), sourceValue) || sourceValue <= 0 || sourceValue > std::numeric_limits<uint16_t>::max()) {
				report.error = "items.xml contains an invalid item id declaration.";
				return report;
			}
			++report.declarations;
			uint16_t converted = 0;
			if (!MapId(provider, static_cast<uint16_t>(sourceValue), converted)) {
				AddMissing(report, static_cast<uint16_t>(sourceValue), "item id", false);
			} else {
				++report.mappedDeclarations;
				mapped.emplace_back(static_cast<uint16_t>(sourceValue), converted);
				if (converted != sourceValue) {
					idAttribute.set_value(converted);
					++report.changedDeclarations;
				}
			}
			ConvertReferenceAttributes(item, provider, report);
		} else if (fromAttribute && toAttribute) {
			int64_t first = 0;
			int64_t last = 0;
			if (!ParseInteger(fromAttribute.value(), first) || !ParseInteger(toAttribute.value(), last) || first <= 0 || last < first || last > std::numeric_limits<uint16_t>::max()) {
				if (allowMalformedRanges) {
					std::ostringstream message;
					message << "Invalid source range fromid='" << fromAttribute.value() << "' toid='" << toAttribute.value()
							<< "' was preserved unchanged by the compatibility override.";
					report.issues.push_back({ 0, "item range", message.str() });
					item = next;
					continue;
				}
				report.error = "items.xml contains an invalid fromid/toid declaration.";
				return report;
			}
			mapped.reserve(static_cast<std::size_t>(last - first + 1));
			for (int64_t sourceValue = first; sourceValue <= last; ++sourceValue) {
				++report.declarations;
				uint16_t converted = 0;
				if (!MapId(provider, static_cast<uint16_t>(sourceValue), converted)) {
					AddMissing(report, static_cast<uint16_t>(sourceValue), "item range", false);
					continue;
				}
				++report.mappedDeclarations;
				if (converted != sourceValue) {
					++report.changedDeclarations;
				}
				mapped.emplace_back(static_cast<uint16_t>(sourceValue), converted);
			}
			if (mapped.size() == static_cast<std::size_t>(last - first + 1)) {
				bool contiguous = true;
				for (std::size_t index = 1; index < mapped.size(); ++index) {
					if (mapped[index].second != static_cast<uint16_t>(mapped.front().second + index)) {
						contiguous = false;
						break;
					}
				}
				if (contiguous) {
					fromAttribute.set_value(mapped.front().second);
					toAttribute.set_value(mapped.back().second);
					ConvertReferenceAttributes(item, provider, report);
				} else {
					for (const auto& [sourceId, destinationId] : mapped) {
						pugi::xml_node expanded = items.insert_copy_before(item, item);
						expanded.remove_attribute("fromid");
						expanded.remove_attribute("toid");
						expanded.append_attribute("id").set_value(destinationId);
						ConvertReferenceAttributes(expanded, provider, report);
					}
					items.remove_child(item);
					++report.expandedRanges;
				}
			}
		} else {
			report.error = "items.xml contains an <item> without id or fromid/toid.";
			return report;
		}

		for (const auto& [sourceId, destinationId] : mapped) {
			const auto [position, inserted] = destinationSources.emplace(destinationId, sourceId);
			if (!inserted) {
				++report.collisions;
				std::ostringstream message;
				message << "XML declarations " << position->second << " and " << sourceId << " both map to ClientID " << destinationId << '.';
				report.issues.push_back({ sourceId, "item declaration", message.str() });
			}
		}
		item = next;
	}

	if (report.collisions != 0 && !allowDestinationCollisions) {
		report.error = "items.xml conversion produced duplicate destination IDs.";
		return report;
	}
	if (strictMapping && (report.missingDeclarations != 0 || report.missingReferences != 0)) {
		report.error = "Strict mapping blocked items.xml conversion because IDs are missing from items.otb.";
		return report;
	}
	report.success = true;
	return report;
}

ItemsXmlIdConversionReport ConvertItemsXmlFile(
	const std::filesystem::path& source,
	const std::filesystem::path& destination,
	const ItemIdMappingProvider& provider,
	bool strictMapping,
	bool allowDestinationCollisions,
	bool allowMalformedRanges
) {
	ItemsXmlIdConversionReport report;
	if (source.empty() || destination.empty() || FileSaveTransaction::PathsReferToSameFile(source, destination)) {
		report.error = "Source and destination items.xml files must be different.";
		return report;
	}
	std::string contents;
	if (!ReadFile(source, contents, report.error)) {
		return report;
	}
	pugi::xml_document document;
	const pugi::xml_parse_result parsed = document.load_buffer(contents.data(), contents.size(), pugi::parse_full, pugi::encoding_auto);
	if (!parsed) {
		report.error = std::string("Could not parse items.xml: ") + parsed.description();
		return report;
	}
	report = ConvertItemsXmlDocument(document, provider, strictMapping, allowDestinationCollisions, allowMalformedRanges);
	if (!report.success) {
		return report;
	}
	const XmlSemanticSummary expected = SummarizeDocument(document, allowDestinationCollisions, allowMalformedRanges);
	if (!expected.error.empty() || expected.declarationIds.size() != report.declarations) {
		report.success = false;
		report.error = expected.error.empty() ? "Converted items.xml changed the logical declaration count." : expected.error;
		return report;
	}

	std::error_code filesystemError;
	if (!destination.parent_path().empty()) {
		std::filesystem::create_directories(destination.parent_path(), filesystemError);
		if (filesystemError) {
			report.success = false;
			report.error = "Could not create items.xml destination directory: " + filesystemError.message();
			return report;
		}
	}
	FileSaveTransaction transaction;
	const std::filesystem::path staged = transaction.Stage(destination);
#ifdef _WIN32
	const bool hadBom = contents.size() >= 3 && static_cast<uint8_t>(contents[0]) == 0xEF && static_cast<uint8_t>(contents[1]) == 0xBB && static_cast<uint8_t>(contents[2]) == 0xBF;
	bool hasDeclaration = false;
	for (pugi::xml_node child : document.children()) {
		hasDeclaration = hasDeclaration || child.type() == pugi::node_declaration;
	}
	const unsigned int format = pugi::format_default | (hadBom ? pugi::format_write_bom : 0u) | (hasDeclaration ? 0u : pugi::format_no_declaration);
	const pugi::xml_encoding outputEncoding = parsed.encoding == pugi::encoding_auto ? pugi::encoding_utf8 : parsed.encoding;
	const bool saved = document.save_file(staged.wstring().c_str(), "\t", format, outputEncoding);
#else
	const bool hadBom = contents.size() >= 3 && static_cast<uint8_t>(contents[0]) == 0xEF && static_cast<uint8_t>(contents[1]) == 0xBB && static_cast<uint8_t>(contents[2]) == 0xBF;
	bool hasDeclaration = false;
	for (pugi::xml_node child : document.children()) {
		hasDeclaration = hasDeclaration || child.type() == pugi::node_declaration;
	}
	const unsigned int format = pugi::format_default | (hadBom ? pugi::format_write_bom : 0u) | (hasDeclaration ? 0u : pugi::format_no_declaration);
	const pugi::xml_encoding outputEncoding = parsed.encoding == pugi::encoding_auto ? pugi::encoding_utf8 : parsed.encoding;
	const bool saved = document.save_file(staged.string().c_str(), "\t", format, outputEncoding);
#endif
	if (!saved) {
		report.success = false;
		report.error = "Could not write staged items.xml output.";
		return report;
	}
	std::string validationContents;
	if (!ReadFile(staged, validationContents, report.error)) {
		report.success = false;
		return report;
	}
	pugi::xml_document validation;
	if (!validation.load_buffer(validationContents.data(), validationContents.size(), pugi::parse_full, pugi::encoding_auto)) {
		report.success = false;
		report.error = "Generated items.xml could not be parsed.";
		return report;
	}
	const XmlSemanticSummary actual = SummarizeDocument(validation, allowDestinationCollisions, allowMalformedRanges);
	std::string semanticError;
	const bool semanticNodesMatch = SemanticNodesEqual(document.child("items"), validation.child("items"), "/items", semanticError);
	if (!actual.error.empty() || actual.declarationIds != expected.declarationIds || !semanticNodesMatch) {
		report.success = false;
		report.error = !actual.error.empty() ? actual.error : (!semanticError.empty() ? semanticError : "Generated items.xml changed its logical item declarations.");
		return report;
	}
	std::string commitError;
	if (!transaction.Commit(commitError)) {
		report.success = false;
		report.error = commitError;
		return report;
	}
	report.outputValidated = true;
	return report;
}
