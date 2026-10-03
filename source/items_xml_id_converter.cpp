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
#include <unordered_map>

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
		static constexpr std::array<const char*, 5> keys {
			"rotateto",
			"writeonceitemid",
			"decayto",
			"transformequipto",
			"transformdeequipto",
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
}

ItemsXmlIdConversionReport ConvertItemsXmlDocument(pugi::xml_document& document, const ItemIdMappingProvider& provider, bool strictMapping, bool allowDestinationCollisions) {
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
				if (allowDestinationCollisions) {
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
			if (!inserted && position->second != sourceId) {
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

ItemsXmlIdConversionReport ConvertItemsXmlFile(const std::filesystem::path& source, const std::filesystem::path& destination, const ItemIdMappingProvider& provider, bool strictMapping, bool allowDestinationCollisions) {
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
	const pugi::xml_parse_result parsed = document.load_buffer(contents.data(), contents.size(), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8);
	if (!parsed) {
		report.error = std::string("Could not parse items.xml: ") + parsed.description();
		return report;
	}
	report = ConvertItemsXmlDocument(document, provider, strictMapping, allowDestinationCollisions);
	if (!report.success) {
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
	const bool saved = document.save_file(staged.wstring().c_str(), "\t", pugi::format_default, pugi::encoding_utf8);
#else
	const bool saved = document.save_file(staged.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8);
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
	if (!validation.load_buffer(validationContents.data(), validationContents.size(), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8)) {
		report.success = false;
		report.error = "Generated items.xml could not be parsed.";
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
