//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ITEM_ID_MAPPING_H_
#define RME_ITEM_ID_MAPPING_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ItemIdMapping {

	enum class Direction : uint8_t {
		ServerToClient,
		ClientToServer,
	};

	struct Result {
		uint16_t original;
		uint16_t converted;
		bool found;
		bool ambiguous;
		std::span<const uint16_t> candidates;
	};

	struct Statistics {
		std::size_t mapping_count;
		std::size_t unique_client_count;
		std::size_t collision_count;
		std::size_t ambiguous_candidate_count;
	};

	[[nodiscard]] Result convert(uint16_t id, Direction direction) noexcept;
	[[nodiscard]] Result serverToClient(uint16_t server_id) noexcept;
	[[nodiscard]] Result clientToServer(uint16_t client_id) noexcept;

	[[nodiscard]] Statistics statistics() noexcept;
	[[nodiscard]] std::string_view sourceVersion() noexcept;
	[[nodiscard]] bool validateTables() noexcept;

} // namespace ItemIdMapping

#endif // RME_ITEM_ID_MAPPING_H_
