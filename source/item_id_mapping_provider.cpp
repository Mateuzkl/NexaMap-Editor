//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "item_id_mapping_provider.h"

ItemIdMapping::Result BuiltInItemIdMappingProvider::convert(uint16_t id, ItemIdMapping::Direction direction) const noexcept {
	return ItemIdMapping::convert(id, direction);
}

bool BuiltInItemIdMappingProvider::validate() const noexcept {
	return ItemIdMapping::validateTables();
}

std::string BuiltInItemIdMappingProvider::description() const {
	return std::string(ItemIdMapping::sourceVersion());
}

std::shared_ptr<const ItemIdMappingProvider> GetBuiltInItemIdMappingProvider() {
	static const auto provider = std::make_shared<const BuiltInItemIdMappingProvider>();
	return provider;
}
