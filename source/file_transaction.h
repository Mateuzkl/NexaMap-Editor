#ifndef RME_FILE_TRANSACTION_H_
#define RME_FILE_TRANSACTION_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

class FileSaveTransaction {
public:
	enum class PublicationMode { Replace,
								 CreateOnly };
	FileSaveTransaction() = default;
	explicit FileSaveTransaction(PublicationMode mode) :
		mode(mode) { }
	FileSaveTransaction(const FileSaveTransaction&) = delete;
	FileSaveTransaction& operator=(const FileSaveTransaction&) = delete;

	~FileSaveTransaction() {
		CleanupStagedFiles();
	}

	std::filesystem::path Stage(const std::filesystem::path& destination) {
		if (destination.empty()) {
			invalidDestination = true;
		}
		for (const Entry& entry : entries) {
			if (PathsReferToSameFile(entry.destination, destination)) {
				duplicateDestination = true;
			}
		}

		Entry entry;
		entry.destination = destination;
		entry.staged = MakeUniqueSibling(destination, ".rme-stage");
		entries.push_back(entry);
		return entry.staged;
	}

	bool Commit(std::string& error) {
		error.clear();
		if (invalidDestination) {
			error = "Cannot save to an empty destination path.";
			return false;
		}
		if (duplicateDestination) {
			error = "Multiple staged files resolve to the same destination.";
			return false;
		}

		for (const Entry& entry : entries) {
			std::error_code ec;
			if (!std::filesystem::exists(entry.staged, ec) || ec) {
				error = "Staged file is missing: " + PathForError(entry.staged);
				return false;
			}
		}

		if (mode == PublicationMode::CreateOnly) {
			for (Entry& entry : entries) {
				std::error_code ec;
				// A sibling hard link installs a complete file atomically and fails
				// if any file (including a dangling symlink) already owns the name.
				std::filesystem::create_hard_link(entry.staged, entry.destination, ec);
				if (ec) {
					error = "Could not create destination without replacement " + PathForError(entry.destination) + ": " + ec.message();
					Rollback(error);
					return false;
				}
				entry.installed = true;
			}
			CleanupStagedFiles();
			return true;
		}

		for (Entry& entry : entries) {
			std::error_code ec;
			const bool destinationExists = std::filesystem::exists(entry.destination, ec);
			if (ec) {
				error = "Could not inspect destination " + PathForError(entry.destination) + ": " + ec.message();
				Rollback(error);
				return false;
			}
			if (!destinationExists) {
				continue;
			}

			entry.backup = MakeUniqueSibling(entry.destination, ".rme-backup");
			std::filesystem::rename(entry.destination, entry.backup, ec);
			if (ec) {
				error = "Could not prepare destination " + PathForError(entry.destination) + ": " + ec.message();
				Rollback(error);
				return false;
			}
			entry.backedUp = true;
		}

		for (Entry& entry : entries) {
			std::error_code ec;
			std::filesystem::rename(entry.staged, entry.destination, ec);
			if (ec) {
				error = "Could not replace destination " + PathForError(entry.destination) + ": " + ec.message();
				Rollback(error);
				return false;
			}
			entry.installed = true;
		}

		for (Entry& entry : entries) {
			if (entry.backedUp) {
				std::error_code ec;
				std::filesystem::remove(entry.backup, ec);
			}
			entry.staged.clear();
		}
		return true;
	}

	static bool PathsReferToSameFile(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
		if (lhs.empty() || rhs.empty()) {
			return lhs.empty() && rhs.empty();
		}

		std::error_code ec;
		if (std::filesystem::exists(lhs, ec) && !ec) {
			ec.clear();
			if (std::filesystem::exists(rhs, ec) && !ec) {
				ec.clear();
				if (std::filesystem::equivalent(lhs, rhs, ec) && !ec) {
					return true;
				}
			}
		}

		const std::filesystem::path normalizedLhs = NormalizePath(lhs);
		const std::filesystem::path normalizedRhs = NormalizePath(rhs);
#ifdef _WIN32
		std::wstring lhsKey = normalizedLhs.wstring();
		std::wstring rhsKey = normalizedRhs.wstring();
		std::transform(lhsKey.begin(), lhsKey.end(), lhsKey.begin(), [](wchar_t character) { return std::towlower(character); });
		std::transform(rhsKey.begin(), rhsKey.end(), rhsKey.begin(), [](wchar_t character) { return std::towlower(character); });
		return lhsKey == rhsKey;
#else
		return normalizedLhs == normalizedRhs;
#endif
	}

	static std::filesystem::path NormalizePath(const std::filesystem::path& path) {
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
		if (!ec) {
			return normalized;
		}
		ec.clear();
		normalized = std::filesystem::absolute(path, ec);
		return (ec ? path : normalized).lexically_normal();
	}

	static bool IsSameOrWithin(const std::filesystem::path& candidatePath, const std::filesystem::path& parentPath) {
		if (candidatePath.empty() || parentPath.empty()) {
			return false;
		}
		const std::filesystem::path candidate = NormalizePath(candidatePath);
		const std::filesystem::path parent = NormalizePath(parentPath);
		auto candidatePart = candidate.begin();
		for (auto parentPart = parent.begin(); parentPart != parent.end(); ++parentPart, ++candidatePart) {
			if (candidatePart == candidate.end() || !PathComponentsEqual(*candidatePart, *parentPart)) {
				return false;
			}
		}
		return true;
	}

	static bool PathsOverlap(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
		return IsSameOrWithin(lhs, rhs) || IsSameOrWithin(rhs, lhs);
	}

private:
	static std::string PathForError(const std::filesystem::path& path) {
		const auto utf8 = path.u8string();
		return { utf8.begin(), utf8.end() };
	}

	struct Entry {
		std::filesystem::path destination;
		std::filesystem::path staged;
		std::filesystem::path backup;
		bool backedUp = false;
		bool installed = false;
	};

	static bool PathComponentsEqual(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
#ifdef _WIN32
		std::wstring lhsKey = lhs.wstring();
		std::wstring rhsKey = rhs.wstring();
		std::transform(lhsKey.begin(), lhsKey.end(), lhsKey.begin(), [](wchar_t character) { return std::towlower(character); });
		std::transform(rhsKey.begin(), rhsKey.end(), rhsKey.begin(), [](wchar_t character) { return std::towlower(character); });
		return lhsKey == rhsKey;
#else
		return lhs == rhs;
#endif
	}

	static std::filesystem::path MakeUniqueSibling(const std::filesystem::path& destination, const char* suffix) {
		static std::atomic_uint64_t sequence = 0;
		const uint64_t timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		for (;;) {
			std::filesystem::path candidate = destination;
			candidate += suffix + std::string(".") + std::to_string(timestamp) + "." + std::to_string(sequence.fetch_add(1));
			std::error_code ec;
			if (!std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
		}
	}

	void Rollback(std::string& error) {
		for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
			if (!iterator->installed) {
				continue;
			}
			std::error_code ec;
			if (mode == PublicationMode::CreateOnly && !std::filesystem::equivalent(iterator->staged, iterator->destination, ec)) {
				error += " Rollback retained a destination changed by another writer: " + PathForError(iterator->destination) + ".";
				continue;
			}
			ec.clear();
			std::filesystem::remove(iterator->destination, ec);
			if (ec) {
				error += " Rollback could not remove " + PathForError(iterator->destination) + ": " + ec.message() + ".";
			}
			iterator->installed = false;
		}

		for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
			if (!iterator->backedUp) {
				continue;
			}
			std::error_code ec;
			std::filesystem::rename(iterator->backup, iterator->destination, ec);
			if (ec) {
				error += " Original remains at " + PathForError(iterator->backup) + " because rollback failed: " + ec.message() + ".";
			} else {
				iterator->backedUp = false;
			}
		}
	}

	void CleanupStagedFiles() {
		for (const Entry& entry : entries) {
			if (entry.staged.empty()) {
				continue;
			}
			std::error_code ec;
			std::filesystem::remove(entry.staged, ec);
		}
	}

	std::vector<Entry> entries;
	PublicationMode mode = PublicationMode::Replace;
	bool invalidDestination = false;
	bool duplicateDestination = false;
};

#endif
