//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ITEM_ID_MAPPING_PROVIDER_H_
#define RME_ITEM_ID_MAPPING_PROVIDER_H_

#include "item_id_mapping.h"

#include <memory>
#include <string>

class ItemIdMappingProvider {
public:
	virtual ~ItemIdMappingProvider() = default;

	[[nodiscard]] virtual ItemIdMapping::Result convert(uint16_t id, ItemIdMapping::Direction direction) const noexcept = 0;
	[[nodiscard]] virtual bool validate() const noexcept = 0;
	[[nodiscard]] virtual std::string description() const = 0;
};

class BuiltInItemIdMappingProvider final : public ItemIdMappingProvider {
public:
	[[nodiscard]] ItemIdMapping::Result convert(uint16_t id, ItemIdMapping::Direction direction) const noexcept override;
	[[nodiscard]] bool validate() const noexcept override;
	[[nodiscard]] std::string description() const override;
};

[[nodiscard]] std::shared_ptr<const ItemIdMappingProvider> GetBuiltInItemIdMappingProvider();

#endif // RME_ITEM_ID_MAPPING_PROVIDER_H_
