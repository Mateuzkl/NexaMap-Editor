#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

void trim(std::string& str);
void replaceString(std::string& str, const std::string& sought, const std::string& replacement);

#include "otml.h"

// Implement required helpers for otml.h in unit tests
void trim(std::string& str) {
	auto start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) {
		str.clear();
		return;
	}
	auto end = str.find_last_not_of(" \t\r\n");
	str = str.substr(start, end - start + 1);
}

void replaceString(std::string& str, const std::string& sought, const std::string& replacement) {
	if (sought.empty()) {
		return;
	}
	size_t pos = 0;
	while ((pos = str.find(sought, pos)) != std::string::npos) {
		str.replace(pos, sought.length(), replacement);
		pos += replacement.length();
	}
}

namespace {
	int failures = 0;

	void check(bool condition, const std::string& name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}
} // namespace

int main() {
	// Test 1: Tab-indented block scalar at depth 0
	{
		std::string input = "description: |\n\tLine one\n\tLine two\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();

		check(doc->hasChildAt("description"), "has child description (tab depth 0)");
		if (doc->hasChildAt("description")) {
			std::string val = doc->valueAt<std::string>("description");
			check(val == "Line one\nLine two\n", "multiline text extracted correctly without truncation (tab depth 0)");
		}
	}

	// Test 2: Tab-indented block scalar at depth 1
	{
		std::string input = "parent:\n\tnotes: |\n\t\tFirst nested line\n\t\tSecond nested line\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();

		check(doc->hasChildAt("parent"), "has parent (tab depth 1)");
		if (doc->hasChildAt("parent")) {
			OTMLNodePtr parent = doc->get("parent");
			check(parent->hasChildAt("notes"), "has child notes (tab depth 1)");
			if (parent->hasChildAt("notes")) {
				std::string val = parent->valueAt<std::string>("notes");
				check(val == "First nested line\nSecond nested line\n", "multiline text extracted correctly without truncation (tab depth 1)");
			}
		}
	}

	// Test 3: Tab-indented block scalar with extra internal indentation
	{
		std::string input = "parent:\n\tcode: |\n\t\tfunction test()\n\t\t\treturn 42\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();

		if (doc->hasChildAt("parent")) {
			OTMLNodePtr parent = doc->get("parent");
			if (parent->hasChildAt("code")) {
				std::string val = parent->valueAt<std::string>("code");
				check(val == "function test()\n\treturn 42\n", "internal extra tab indentation preserved in block scalar");
			}
		}
	}

	// Test 4: Space-indented block scalar at depth 1 (2 spaces per level)
	{
		std::string input = "parent:\n  notes: |\n    First space line\n    Second space line\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();

		if (doc->hasChildAt("parent")) {
			OTMLNodePtr parent = doc->get("parent");
			if (parent->hasChildAt("notes")) {
				std::string val = parent->valueAt<std::string>("notes");
				check(val == "First space line\nSecond space line\n", "multiline text extracted correctly with space indentation");
			}
		}
	}

	// Test 5: Tab-indented .otfi structure
	{
		std::string input = "DatSpr:\n\textended: true\n\ttransparency: false\n\tmetadata-file: Tibia.dat\n\tsprites-file: Tibia.spr\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();

		check(doc->hasChildAt("DatSpr"), "has child DatSpr");
		if (doc->hasChildAt("DatSpr")) {
			OTMLNodePtr datSpr = doc->get("DatSpr");
			check(datSpr->valueAt<bool>("extended") == true, "extended is true");
			check(datSpr->valueAt<bool>("transparency") == false, "transparency is false");
			check(datSpr->valueAt<std::string>("metadata-file") == "Tibia.dat", "metadata-file is Tibia.dat");
			check(datSpr->valueAt<std::string>("sprites-file") == "Tibia.spr", "sprites-file is Tibia.spr");
		}
	}

	// Test 6: Empty block scalar (|)
	{
		std::string input = "description: |\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();
		check(doc->hasChildAt("description"), "has child description (empty block scalar)");
		if (doc->hasChildAt("description")) {
			std::string val = doc->valueAt<std::string>("description");
			check(val == "\n", "empty block scalar value is single newline");
		}
	}

	// Test 7: Empty strip block scalar (|-)
	{
		std::string input = "description: |-\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();
		check(doc->hasChildAt("description"), "has child description (empty strip block scalar)");
		if (doc->hasChildAt("description")) {
			std::string val = doc->valueAt<std::string>("description");
			check(val.empty(), "empty strip block scalar value is empty");
		}
	}

	// Test 8: Empty keep block scalar (|+)
	{
		std::string input = "description: |+\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();
		check(doc->hasChildAt("description"), "has child description (empty keep block scalar)");
		if (doc->hasChildAt("description")) {
			std::string val = doc->valueAt<std::string>("description");
			check(val == "\n", "empty keep block scalar without trailing newlines is newline");
		}
	}

	// Test 9: Blank-only multiline body
	{
		std::string input = "description: |\n  \n  \n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		parser.parse();
		check(doc->hasChildAt("description"), "has child description (blank-only multiline body)");
		if (doc->hasChildAt("description")) {
			std::string val = doc->valueAt<std::string>("description");
			check(val == "\n", "blank-only multiline body trimmed correctly with |");
		}
	}

	// Test 10: Invalid root indentation with spaces throws OTMLException
	{
		std::string input = "  DatSpr:\n    extended: true\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		bool caught = false;
		try {
			parser.parse();
		} catch (const OTMLException&) {
			caught = true;
		} catch (...) {
		}
		check(caught, "invalid root indentation with spaces throws OTMLException");
	}

	// Test 11: Invalid root indentation with tabs throws OTMLException
	{
		std::string input = "\tDatSpr:\n\t\textended: true\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		bool caught = false;
		try {
			parser.parse();
		} catch (const OTMLException&) {
			caught = true;
		} catch (...) {
		}
		check(caught, "invalid root indentation with tabs throws OTMLException");
	}

	// Test 12: Invalid indentation jump throws OTMLException without crash
	{
		std::string input = "root:\n      child: value\n";
		std::istringstream stream(input);
		OTMLDocumentPtr doc = OTMLDocument::create();
		OTMLParser parser(doc, stream);
		bool caught = false;
		try {
			parser.parse();
		} catch (const OTMLException&) {
			caught = true;
		} catch (...) {
		}
		check(caught, "invalid indentation jump throws OTMLException");
	}

	if (failures != 0) {
		std::cerr << failures << " OTML test(s) failed.\n";
		return 1;
	}
	std::cout << "OTML tests passed.\n";
	return 0;
}
