//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "items_otb_id_converter.h"

#include "file_transaction.h"
#include "filehandle.h"
#include "otb_item_id_mapping_provider.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
	constexpr uint8_t DeprecatedItemGroup = 0x0E;
	constexpr uint8_t ItemAttributeServerId = 0x10;
	constexpr uint8_t ItemAttributeClientId = 0x11;

	std::string PrintablePath(const std::filesystem::path& path) {
#ifdef _WIN32
		return nstr(wxString(path.wstring()));
#else
		return path.string();
#endif
	}

	uint16_t ReadU16(const std::string& data, std::size_t offset) {
		return static_cast<uint16_t>(static_cast<uint8_t>(data[offset])) |
			(static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1])) << 8);
	}

	void WriteU16(std::string& data, std::size_t offset, uint16_t value) {
		data[offset] = static_cast<char>(value & 0xFF);
		data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
	}

	struct ItemNode {
		std::string data;
		uint16_t serverId = 0;
		uint16_t clientId = 0;
		std::size_t serverIdOffset = 0;
		bool deprecated = false;
		bool hasServerId = false;
		bool hasClientId = false;
	};

	bool ParseItemNode(BinaryNode& node, ItemNode& item, std::string& error) {
		item.data = node.getNodeData();
		if (item.data.size() < 5) {
			error = "items.otb contains a truncated item group/flags header.";
			return false;
		}
		item.deprecated = static_cast<uint8_t>(item.data[0]) == DeprecatedItemGroup;
		std::size_t offset = 5; // group byte + four-byte flags
		while (offset < item.data.size()) {
			if (item.data.size() - offset < 3) {
				error = "items.otb contains a truncated item attribute header.";
				return false;
			}
			const uint8_t attribute = static_cast<uint8_t>(item.data[offset]);
			const uint16_t length = ReadU16(item.data, offset + 1);
			const std::size_t valueOffset = offset + 3;
			if (valueOffset > item.data.size() || length > item.data.size() - valueOffset) {
				error = "items.otb contains an attribute extending beyond its node.";
				return false;
			}
			if (attribute == ItemAttributeServerId || attribute == ItemAttributeClientId) {
				if (length != sizeof(uint16_t)) {
					error = "items.otb contains a ServerID/ClientID attribute with invalid length.";
					return false;
				}
				const uint16_t value = ReadU16(item.data, valueOffset);
				if (attribute == ItemAttributeServerId) {
					item.serverId = value;
					item.serverIdOffset = valueOffset;
					item.hasServerId = true;
				} else {
					item.clientId = value;
					item.hasClientId = true;
				}
			}
			offset = valueOffset + length;
		}
		if (!item.hasServerId) {
			error = "items.otb contains an item without ServerID.";
			return false;
		}
		if (!item.deprecated && (!item.hasClientId || item.clientId == 0)) {
			error = "items.otb contains a non-deprecated item without ClientID.";
			return false;
		}
		return true;
	}

	bool WriteNodeData(NodeFileWriteHandle& output, const std::string& data) {
		if (data.empty() || !output.addNode(static_cast<uint8_t>(data[0]))) {
			return false;
		}
		if (data.size() > 1 && !output.addRAW(reinterpret_cast<const uint8_t*>(data.data() + 1), data.size() - 1)) {
			return false;
		}
		return output.endNode();
	}
}

ItemsOtbIdConversionReport ConvertItemsOtbToClientIds(const std::filesystem::path& source, const std::filesystem::path& destination) {
	ItemsOtbIdConversionReport report;
	if (source.empty() || destination.empty() || FileSaveTransaction::PathsReferToSameFile(source, destination)) {
		report.error = "Source and destination items.otb files must be different.";
		return report;
	}

	DiskNodeFileReadHandle input(PrintablePath(source), StringVector(1, "OTBI"));
	if (!input.isOk()) {
		report.error = "Could not open source items.otb: " + input.getErrorMessage();
		return report;
	}
	BinaryNode* root = input.getRootNode();
	if (!root) {
		report.error = "Source items.otb has no root node.";
		return report;
	}
	const std::string rootData = root->getNodeData();
	if (rootData.empty()) {
		report.error = "Source items.otb has an empty root node.";
		return report;
	}

	std::vector<ItemNode> nodes;
	std::unordered_map<uint16_t, std::size_t> canonicalByClientId;
	for (BinaryNode* node = root->getChild(); node != nullptr; node = node->advance()) {
		if (node->getChild() != nullptr) {
			report.error = "Nested nodes are not supported in items.otb item entries.";
			return report;
		}
		ItemNode item;
		if (!ParseItemNode(*node, item, report.error)) {
			return report;
		}
		++report.sourceNodes;
		if (item.deprecated) {
			++report.deprecatedItemsPreserved;
			nodes.push_back(std::move(item));
			continue;
		}

		const auto [position, inserted] = canonicalByClientId.emplace(item.clientId, nodes.size());
		if (inserted) {
			nodes.push_back(std::move(item));
			continue;
		}

		++report.aliasesMerged;
		ItemNode& existing = nodes[position->second];
		if (item.serverId == item.clientId && existing.serverId != existing.clientId) {
			existing = std::move(item);
		}
	}
	if (!input.isOk()) {
		report.error = "Source items.otb is corrupt or truncated: " + input.getErrorMessage();
		return report;
	}

	std::error_code filesystemError;
	if (!destination.parent_path().empty()) {
		std::filesystem::create_directories(destination.parent_path(), filesystemError);
	}
	if (filesystemError) {
		report.error = "Could not create items.otb destination directory: " + filesystemError.message();
		return report;
	}
	FileSaveTransaction transaction;
	const std::filesystem::path staged = transaction.Stage(destination);
	{
		DiskNodeFileWriteHandle output(PrintablePath(staged), "OTBI");
		if (!output.isOk() || !output.addNode(static_cast<uint8_t>(rootData[0])) ||
			(rootData.size() > 1 && !output.addRAW(reinterpret_cast<const uint8_t*>(rootData.data() + 1), rootData.size() - 1))) {
			report.error = "Could not write the staged items.otb root node.";
			return report;
		}
		for (ItemNode& item : nodes) {
			if (!item.deprecated) {
				WriteU16(item.data, item.serverIdOffset, item.clientId);
				++report.writtenItems;
			}
			if (!WriteNodeData(output, item.data)) {
				report.error = "Could not write a staged items.otb item node.";
				return report;
			}
		}
		if (!output.endNode() || !output.isOk()) {
			report.error = "Could not finish the staged items.otb file.";
			return report;
		}
		output.close();
	}

	const OtbItemIdMappingLoadResult validation = OtbItemIdMappingProvider::Load(staged);
	if (!validation.ready() || validation.stats.entries != report.writtenItems) {
		report.error = validation.error.empty() ? "Generated items.otb failed identity-mapping validation." : validation.error;
		return report;
	}
	for (const auto& [clientId, index] : canonicalByClientId) {
		(void)index;
		const ItemIdMapping::Result mapping = validation.provider->convert(clientId, ItemIdMapping::Direction::ServerToClient);
		if (!mapping.found || mapping.ambiguous || mapping.converted != clientId) {
			report.error = "Generated items.otb contains a non-identity ServerID/ClientID mapping.";
			return report;
		}
	}

	std::string commitError;
	if (!transaction.Commit(commitError)) {
		report.error = commitError;
		return report;
	}
	report.outputValidated = true;
	report.success = true;
	return report;
}
