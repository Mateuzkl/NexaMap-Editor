// Range checks of xmlAttributeAsUint16, and the parser input that overflowed the
// heap in the formerly vendored pugixml 1.2.
#include "xml_attribute.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {
	int failures = 0;

	void expect(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL " << message << '\n';
			++failures;
		}
	}

	uint16_t parsed(const std::string& value, uint16_t fallback = 0) {
		pugi::xml_document document;
		document.append_child("node").append_attribute("value") = value.c_str();
		return xmlAttributeAsUint16(document.child("node").attribute("value"), fallback);
	}

	void rangeChecks() {
		expect(parsed("0", 7) == 0, "zero is a value, not the fallback");
		expect(parsed("65535") == 65535, "largest id");
		expect(parsed("1234") == 1234, "plain id");
		expect(parsed("65536", 7) == 7, "one past the range must not wrap to 0");
		expect(parsed("70000") == 0, "out-of-range id must not truncate to 4464");
		expect(parsed("200237") == 0, "out-of-range id must not truncate to 3165");
		expect(parsed("-1") == 0, "negative id must not wrap to 65535");
		expect(parsed("99999999999999999999999") == 0, "overflowing text");

		pugi::xml_document document;
		const pugi::xml_node node = document.append_child("node");
		expect(xmlAttributeAsUint16(node.attribute("missing"), 9) == 9, "missing attribute");
	}

	void malformedDoctype() {
		const char input[] = "<!DOCTYPE <!-- -->x";
		pugi::xml_document document;
		expect(!document.load_buffer(input, std::strlen(input)), "unterminated DOCTYPE is an error");
	}

	void xpathStaysDisabled() {
#ifndef PUGIXML_NO_XPATH
		expect(false, "PUGIXML_NO_XPATH must be defined for project code");
#endif
	}
}

int main() {
	rangeChecks();
	malformedDoctype();
	xpathStaysDisabled();
	if (failures == 0) {
		std::cout << "XML attribute tests passed\n";
	}
	return failures == 0 ? 0 : 1;
}
