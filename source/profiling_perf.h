#ifndef NEXAMAP_PROFILING_PERF_H_
#define NEXAMAP_PROFILING_PERF_H_

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

class NexaPerf {
public:
	static bool isEnabled() noexcept {
		static const bool enabled = []() {
			const char* env = std::getenv("NEXAMAP_PERF");
			return env && env[0] != '0';
		}();
		return enabled;
	}

	static void log(std::string_view tag, double durationMs) {
		if (isEnabled()) {
			std::printf("[NexaPerf] %.*s: %.2f ms\n", static_cast<int>(tag.size()), tag.data(), durationMs);
			std::fflush(stdout);
		}
	}

	static void logMessage(std::string_view message) {
		if (isEnabled()) {
			std::printf("[NexaPerf] %.*s\n", static_cast<int>(message.size()), message.data());
			std::fflush(stdout);
		}
	}
};

class NexaPerfScope {
public:
	explicit NexaPerfScope(std::string_view tag) :
		tag(tag),
		enabled(NexaPerf::isEnabled()),
		start(enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {}) {
	}

	~NexaPerfScope() {
		if (enabled) {
			const auto elapsed = std::chrono::steady_clock::now() - start;
			const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
			NexaPerf::log(tag, ms);
		}
	}

	NexaPerfScope(const NexaPerfScope&) = delete;
	NexaPerfScope& operator=(const NexaPerfScope&) = delete;

private:
	std::string_view tag;
	bool enabled;
	std::chrono::steady_clock::time_point start;
};

#endif // NEXAMAP_PROFILING_PERF_H_
