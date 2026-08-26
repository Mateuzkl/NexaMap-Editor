#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int failures = 0;
	int checks = 0;

	void check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	bool IsNumericVersion(const std::filesystem::path& path) {
		const std::string name = path.filename().string();
		return !name.empty() && name.find_first_not_of("0123456789") == std::string::npos;
	}
}

int main() {
#ifdef NEXAMAP_SOURCE_DIR
	const std::filesystem::path sourceRoot = NEXAMAP_SOURCE_DIR;
#else
	const std::filesystem::path sourceRoot = std::filesystem::current_path();
#endif
	const std::filesystem::path dataRoot = sourceRoot / "data";
	const std::filesystem::path editorRoot = dataRoot / "editor";

	check(std::filesystem::is_directory(editorRoot), "canonical data/editor directory exists");
	for (const char* file : { "materials.xml", "borders.xml", "grounds.xml", "walls.xml", "doodads.xml", "tilesets.xml", "creatures.xml" }) {
		check(std::filesystem::is_regular_file(editorRoot / file), std::string("canonical ") + file + " exists");
	}

	std::ifstream materialsFile(editorRoot / "materials.xml", std::ios::binary);
	const std::string materials((std::istreambuf_iterator<char>(materialsFile)), std::istreambuf_iterator<char>());
	for (const char* include : { "borders.xml", "grounds.xml", "walls.xml", "doodads.xml", "tilesets.xml" }) {
		check(materials.find(std::string("file=\"") + include + "\"") != std::string::npos, std::string("materials.xml includes ") + include);
	}

	std::size_t numericDirectories = 0;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dataRoot)) {
		if (entry.is_directory() && IsNumericVersion(entry.path())) {
			++numericDirectories;
		}
	}
	check(numericDirectories == 0, "numeric version directories have been fully retired");
	check(std::filesystem::is_regular_file(dataRoot / "canary-crystal" / "items" / "items.xml"), "Canary/Crystal compatibility data remains isolated");

	if (failures == 0) {
		std::cout << checks << " editor data layout checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
