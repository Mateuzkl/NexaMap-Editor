#include "filehandle.h"
#include "file_transaction.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// filehandle.cpp normally gets these conversion helpers from common.cpp. Keep
// this focused unit test independent from the editor/UI implementation.
std::string i2s(int value) {
	return std::to_string(value);
}

std::wstring string2wstring(const std::string& value) {
	return std::filesystem::path(value).wstring();
}

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

	std::filesystem::path temporaryFile(const char* suffix) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() / ("nexamap-filehandle-" + std::to_string(stamp) + suffix);
	}
}

int main() {
	const std::filesystem::path output = temporaryFile(".otbm");
	{
		DiskNodeFileWriteHandle handle(output.string(), "OTBM");
		check(handle.isOk(), "disk node writer opens a valid target");
		check(handle.addNode(1), "disk node writer accepts a node");
		check(handle.endNode(), "disk node writer accepts an end marker");
		handle.close();
		check(handle.error_code == FILE_NO_ERROR, "successful close retains the no-error state");
		check(!handle.isOpen(), "successful close releases the FILE handle");
	}
	{
		std::ifstream input(output, std::ios::binary | std::ios::ate);
		check(input.good() && input.tellg() > 4, "close flushes buffered node bytes");
	}
	std::error_code cleanupError;
	std::filesystem::remove(output, cleanupError);

	const std::filesystem::path preserved = temporaryFile("-preserved.otbm");
	{
		DiskNodeFileWriteHandle handle(preserved.string(), "OTBM");
		check(handle.isOk(), "writer for preservation test opens");
		handle.error_code = FILE_WRITE_ERROR;
		handle.close();
		check(handle.error_code == FILE_WRITE_ERROR, "close does not erase a prior write error");
	}
	std::filesystem::remove(preserved, cleanupError);

	const std::filesystem::path invalid = temporaryFile("-invalid.otbm");
	{
		DiskNodeFileWriteHandle handle(invalid.string(), "BAD");
		check(handle.error_code == FILE_INVALID_IDENTIFIER, "invalid identifier is reported");
		handle.close();
		check(handle.error_code == FILE_INVALID_IDENTIFIER, "close preserves a constructor error");
	}
	std::filesystem::remove(invalid, cleanupError);

	const std::filesystem::path existingDestination = temporaryFile("-existing.otbm");
	{
		std::ofstream original(existingDestination, std::ios::binary);
		original << "original";
	}
	std::filesystem::path abandonedStage;
	{
		FileSaveTransaction transaction;
		abandonedStage = transaction.Stage(existingDestination);
		std::ofstream partial(abandonedStage, std::ios::binary);
		partial << "partial";
	}
	{
		std::ifstream original(existingDestination, std::ios::binary);
		std::string contents;
		original >> contents;
		check(contents == "original", "abandoned transaction preserves an existing destination");
		check(!std::filesystem::exists(abandonedStage), "abandoned transaction removes its staged output");
	}
	std::filesystem::remove(existingDestination, cleanupError);

	const std::filesystem::path newDestination = temporaryFile("-new.otbm");
	std::filesystem::path newStage;
	{
		FileSaveTransaction transaction;
		newStage = transaction.Stage(newDestination);
		std::ofstream partial(newStage, std::ios::binary);
		partial << "partial";
	}
	check(!std::filesystem::exists(newDestination), "abandoned transaction does not leave a new destination behind");
	check(!std::filesystem::exists(newStage), "abandoned new-file transaction removes its staged output");

	std::cout << "File Handle Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
