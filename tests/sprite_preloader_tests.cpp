#include "sprite_preloader.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

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
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "nexamap_sprite_preloader_test.spr";
	std::vector<uint8_t> contents(20, 0);
	constexpr uint32_t spriteOffset = 8;
	const std::vector<uint8_t> dump = { 1, 0, 1, 0, 10, 20, 30 };
	contents[spriteOffset + 3] = static_cast<uint8_t>(dump.size());
	contents[spriteOffset + 4] = 0;
	std::copy(dump.begin(), dump.end(), contents.begin() + spriteOffset + 5);
	{
		std::ofstream stream(file, std::ios::binary);
		stream.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
	}

	SpritePreloader preloader;
	preloader.configure(file, { 0, spriteOffset }, false, 1);
	std::vector<uint8_t> pixels;
	SpritePreloadStatus status = preloader.getOrRequest(1, pixels);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (status == SpritePreloadStatus::Pending && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		status = preloader.getOrRequest(1, pixels);
	}

	check(status == SpritePreloadStatus::Ready, "sprite must be decoded by the worker");
	check(pixels.size() == 32 * 32 * 4, "worker must prepare a full RGBA sprite");
	if (pixels.size() >= 8) {
		check(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 0, "transparent run must stay transparent");
		check(pixels[4] == 10 && pixels[5] == 20 && pixels[6] == 30 && pixels[7] == 0xFF, "colored run must preserve RGB and add alpha");
	}
	check(preloader.getOrRequest(2, pixels) == SpritePreloadStatus::Unavailable, "out-of-range sprite must not be queued");
	preloader.clear();
	std::error_code removeError;
	std::filesystem::remove(file, removeError);

	if (failures != 0) {
		std::cerr << failures << " sprite preloader test(s) failed.\n";
		return 1;
	}
	std::cout << "All sprite preloader tests passed.\n";
	return 0;
}
