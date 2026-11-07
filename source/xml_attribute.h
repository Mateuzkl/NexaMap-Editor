#ifndef RME_XML_ATTRIBUTE_H_
#define RME_XML_ATTRIBUTE_H_

#include <cstdint>
#include <limits>

#include <pugixml.hpp>

// Reads a pugixml attribute as uint16_t. Item, look and group ids in XML files
// are 16-bit: a value that is absent or outside 0..65535 yields `fallback`
// instead of being truncated into a different valid id.
[[nodiscard]] inline uint16_t xmlAttributeAsUint16(const pugi::xml_attribute& attribute, uint16_t fallback = 0) {
	// as_llong saturates on overflow, so every out-of-range text stays out of range.
	const long long value = attribute.as_llong(-1);
	if (value < 0 || value > std::numeric_limits<uint16_t>::max()) {
		return fallback;
	}
	return static_cast<uint16_t>(value);
}

#endif
