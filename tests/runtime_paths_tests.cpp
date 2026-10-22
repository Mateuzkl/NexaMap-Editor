#include "runtime_paths.h"
#include "path_encoding.h"
#include "file_transaction.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	const fs::path temporary = fs::temp_directory_path() / ("nexamap-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try {
		const auto unicode = u8"Meus Mapas/São João/عربي";
		const std::u8string utf8(unicode);
		const std::string bytes(utf8.begin(), utf8.end());
		require(PathEncoding::FromUtf8(bytes) == fs::path(unicode), "UTF-8 bytes produce native Unicode paths");
		require(PathEncoding::FromUtf8(std::string_view(bytes).substr(0, 11)) == fs::path(u8"Meus Mapas/"), "UTF-8 path conversion respects string-view bounds");
		require(PathEncoding::FromUtf8({}).empty(), "empty UTF-8 path stays empty");
		const auto directory = temporary / fs::path(u8"Meus Mapas/São João/عربي");
		fs::create_directories(directory);
#ifndef _WIN32
		setenv("HOME", temporary.c_str(), 1);
		unsetenv("XDG_CONFIG_HOME");
		require(RuntimePaths::UserDirectory("XDG_CONFIG_HOME", ".config") == temporary / ".config/nexamap", "default XDG config directory");
		setenv("XDG_CONFIG_HOME", "relative-is-invalid", 1);
		require(RuntimePaths::UserDirectory("XDG_CONFIG_HOME", ".config") == temporary / ".config/nexamap", "relative XDG directory ignored");
		setenv("XDG_CONFIG_HOME", directory.c_str(), 1);
		require(RuntimePaths::UserDirectory("XDG_CONFIG_HOME", ".config") == directory / "nexamap", "absolute Unicode XDG config directory");
#endif
		const auto prefix = directory / "Installed NexaMap";
		fs::create_directories(prefix / "bin");
		fs::create_directories(prefix / "share/nexamap/data");
		std::ofstream(prefix / "share/nexamap/data/clients.xml") << "fixture";
		const auto paths = RuntimePaths::DataCandidates(prefix / "bin", temporary / "unrelated");
		require(fs::is_regular_file(paths.at(1) / "clients.xml"), "relocated install data found without working directory");
		const auto destination = directory / fs::path(u8"Mundo São João.otbm");
		std::ofstream(destination) << "original";
		{
			FileSaveTransaction transaction;
			std::ofstream(transaction.Stage(destination)) << "edited";
			std::string error;
			require(transaction.Commit(error), "Unicode save transaction commits");
		}
		std::string content;
		std::ifstream(destination) >> content;
		require(content == "edited", "saved contents survive reopening");
		{
			FileSaveTransaction transaction;
			transaction.Stage(destination); // Missing staged output simulates a write failure.
			std::string error;
			require(!transaction.Commit(error) && !error.empty(), "failed stage reports an explicit error");
			const auto utf8Destination = destination.u8string();
			require(error.find(std::string(utf8Destination.begin(), utf8Destination.end())) != std::string::npos, "failed stage preserves Unicode destination in error");
		}
		std::ifstream(destination) >> content;
		require(content == "edited", "failed transaction preserves original");
		for (const auto& entry : fs::directory_iterator(directory)) {
			const auto filename = entry.path().filename().u8string();
			require(filename.find(u8".rme-stage") == std::u8string::npos && filename.find(u8".rme-backup") == std::u8string::npos, "transaction leaves no temporary siblings");
		}
#ifndef _WIN32
		const auto readonly = directory / "read only";
		fs::create_directory(readonly);
		fs::permissions(readonly, fs::perms::owner_read | fs::perms::owner_exec);
		require(!std::ofstream(readonly / "denied.otbm").good(), "permission denied when saving to read-only directory (run unprivileged)");
		fs::permissions(readonly, fs::perms::owner_all);
		fs::create_symlink(destination, directory / "map-link.otbm");
		require(FileSaveTransaction::PathsReferToSameFile(destination, directory / "map-link.otbm"), "symlink identity is respected");
#endif
		fs::remove_all(temporary);
		std::cout << "PASS XDG, installed resources, Unicode, permissions and transactional saves\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL " << error.what() << " (fixture " << temporary << ")\n";
		fs::remove_all(temporary);
		return 1;
	}
}
