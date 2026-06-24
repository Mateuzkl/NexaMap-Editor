//////////////////////////////////////////////////////////////////////
// Lightweight, zero-dependency, opt-in timing instrumentation.
//
// Completely compiled out (no-op, zero overhead) unless RME_PROFILE is
// defined at compile time (e.g. add /D RME_PROFILE or -DRME_PROFILE).
//
// Usage:
//     void Foo::bar() {
//         RME_PROFILE_SCOPE("Foo::bar");
//         ...
//     }
//
// When RME_PROFILE is defined, per-scope call counts and accumulated
// time are written to "rme_profile.txt" (working directory) on exit,
// sorted by total time. RME_PROFILE_DUMP() forces an early dump.
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROFILING_H_
#define RME_PROFILING_H_

#ifdef RME_PROFILE

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rme_profiling {

struct ProfileStat {
	uint64_t calls = 0;
	uint64_t nanos = 0;
};

class ProfileRegistry {
public:
	void add(const char* name, uint64_t nanos) {
		std::lock_guard<std::mutex> lock(mutex_);
		ProfileStat& stat = stats_[name];
		++stat.calls;
		stat.nanos += nanos;
	}

	~ProfileRegistry() {
		dump();
	}

	void dump() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (stats_.empty()) {
			return;
		}

		std::vector<std::pair<std::string, ProfileStat>> rows(stats_.begin(), stats_.end());
		std::sort(rows.begin(), rows.end(), [](const std::pair<std::string, ProfileStat>& a, const std::pair<std::string, ProfileStat>& b) {
			return a.second.nanos > b.second.nanos;
		});

		FILE* out = std::fopen("rme_profile.txt", "w");
		FILE* dest = out ? out : stdout;
		std::fprintf(dest, "%-44s %12s %14s %12s\n", "scope", "calls", "total_ms", "avg_us");
		for (const std::pair<std::string, ProfileStat>& row : rows) {
			const double totalMs = static_cast<double>(row.second.nanos) / 1e6;
			const double avgUs = row.second.calls ? (static_cast<double>(row.second.nanos) / 1e3) / static_cast<double>(row.second.calls) : 0.0;
			std::fprintf(dest, "%-44s %12llu %14.3f %12.3f\n",
				row.first.c_str(),
				static_cast<unsigned long long>(row.second.calls),
				totalMs,
				avgUs);
		}
		if (out) {
			std::fclose(out);
		}
	}

private:
	std::map<std::string, ProfileStat> stats_;
	std::mutex mutex_;
};

inline ProfileRegistry& registry() {
	static ProfileRegistry instance;
	return instance;
}

class ScopedTimer {
public:
	explicit ScopedTimer(const char* name) :
		name_(name),
		start_(std::chrono::steady_clock::now()) {
	}

	~ScopedTimer() {
		const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start_;
		registry().add(name_, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
	}

	ScopedTimer(const ScopedTimer&) = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
	const char* name_;
	std::chrono::steady_clock::time_point start_;
};

} // namespace rme_profiling

#define RME_PROFILE_CONCAT_INNER(a, b) a##b
#define RME_PROFILE_CONCAT(a, b) RME_PROFILE_CONCAT_INNER(a, b)
#define RME_PROFILE_SCOPE(name) ::rme_profiling::ScopedTimer RME_PROFILE_CONCAT(rme_profile_timer_, __LINE__)(name)
#define RME_PROFILE_DUMP() ::rme_profiling::registry().dump()

#else // RME_PROFILE not defined

#define RME_PROFILE_SCOPE(name) ((void)0)
#define RME_PROFILE_DUMP() ((void)0)

#endif // RME_PROFILE

#endif // RME_PROFILING_H_
