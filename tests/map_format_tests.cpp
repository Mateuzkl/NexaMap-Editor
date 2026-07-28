#include "map_format.h"

#include <iostream>
#include <string_view>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}
}

int main() {
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_5,
			4,
			true,
			SpawnFormat::CanaryCrystal,
			false,
			false,
		});
		check(result.format == MapStorageFormat::CanaryCrystal && !result.ambiguous, "OTBM 5 identifies Canary");
	}
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_3,
			CLIENT_VERSION_860,
			true,
			SpawnFormat::Tfs,
			false,
			false,
		});
		check(result.format == MapStorageFormat::Tfs && !result.ambiguous, "known client and combined spawn identify TFS");
	}
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_3,
			4,
			true,
			SpawnFormat::CanaryCrystal,
			false,
			false,
		});
		check(result.format == MapStorageFormat::CanaryCrystal, "split spawns override a misleading legacy item header");
	}
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_3,
			999,
			false,
			SpawnFormat::Unknown,
			false,
			false,
		});
		check(result.format == MapStorageFormat::Unknown && result.ambiguous, "unknown evidence requires user choice");
	}
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_5,
			4,
			true,
			SpawnFormat::Tfs,
			true,
			false,
		});
		check(result.format == MapStorageFormat::Unknown && result.ambiguous && result.conflict, "conflicting sidecars require user choice");
	}
	{
		const MapFormatDetection result = MapFormatDetector::Detect({
			MAP_OTBM_3,
			999,
			false,
			SpawnFormat::Unknown,
			false,
			true,
		});
		check(result.format == MapStorageFormat::CanaryCrystal && !result.ambiguous, "zone sidecar header identifies Canary");
	}

	if (failures != 0) {
		std::cerr << failures << " map format test(s) failed.\n";
		return 1;
	}
	std::cout << "6 map format tests passed.\n";
	return 0;
}
