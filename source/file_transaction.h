#ifndef RME_FILE_TRANSACTION_H_
#define RME_FILE_TRANSACTION_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <cerrno>
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <unistd.h>
	#ifdef __linux__
		#include <linux/fs.h>
		#include <sys/syscall.h>
		// <linux/fs.h> defines BLOCK_SIZE, which collides with MapCanvas::BLOCK_SIZE.
		#undef BLOCK_SIZE
	#elif defined(__APPLE__)
		#include <stdio.h>
	#endif
#endif

class FileSaveTransaction {
public:
	enum class PublicationMode { Replace,
								 CreateOnly };
	using HardLinkFunction = void (*)(const std::filesystem::path&, const std::filesystem::path&, std::error_code&);
	FileSaveTransaction() = default;
	explicit FileSaveTransaction(PublicationMode mode, HardLinkFunction createLink = std::filesystem::create_hard_link) :
		mode(mode), createLink(createLink) { }
	FileSaveTransaction(const FileSaveTransaction&) = delete;
	FileSaveTransaction& operator=(const FileSaveTransaction&) = delete;

	~FileSaveTransaction() {
		CleanupStagedFiles();
	}

	std::filesystem::path Stage(const std::filesystem::path& destination) {
		if (destination.empty()) {
			invalidDestination = true;
		}
		// A replaced symlink must keep pointing at the same file, so the target is
		// what gets published. Staging beside it also keeps the final rename on one
		// filesystem. CreateOnly must instead fail on any name that already exists.
		const std::filesystem::path target = mode == PublicationMode::Replace ? ResolveSymlinks(destination) : destination;
		for (const Entry& entry : entries) {
			if (PathsReferToSameFile(entry.destination, target)) {
				duplicateDestination = true;
			}
		}

		Entry entry;
		entry.destination = target;
		entry.staged = MakeUniqueSibling(target, ".rme-stage");
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
			for (const Entry& entry : entries) {
				std::error_code ec;
				SyncFile(entry.staged, ec);
				if (ec) {
					error = "Could not flush staged file " + PathForError(entry.staged) + ": " + ec.message();
					return false;
				}
			}
			for (Entry& entry : entries) {
				std::error_code ec;
				// A sibling hard link installs a complete file atomically and fails
				// if any file (including a dangling symlink) already owns the name.
				createLink(entry.staged, entry.destination, ec);
				if (HardLinksUnsupported(ec) && ReadIdentity(entry.staged, entry.identity, ec)) {
					RenameExclusive(entry.staged, entry.destination, ec);
					entry.renamed = !ec;
				}
				if (ec) {
					error = "Could not create destination without replacement " + PathForError(entry.destination) + ": " + ec.message();
					Rollback(error);
					return false;
				}
				entry.installed = true;
			}
			if (!SyncDestinationDirectories(error)) {
				Rollback(error);
				return false;
			}
			CleanupStagedFiles();
			return true;
		}

		// Prepare everything before the first destination changes. Live files are
		// never moved away: each one stays readable until the atomic replace, and
		// its link or copy only exists so that a later failure can restore it.
		for (Entry& entry : entries) {
			std::error_code ec;
			const bool destinationExists = std::filesystem::exists(entry.destination, ec);
			if (ec) {
				error = "Could not inspect destination " + PathForError(entry.destination) + ": " + ec.message();
				Rollback(error);
				return false;
			}
			if (destinationExists) {
				CopyOwnerAndMode(entry.destination, entry.staged);
				entry.backup = MakeUniqueSibling(entry.destination, ".rme-backup");
				createLink(entry.destination, entry.backup, ec);
				if (ec) {
					ec.clear();
					std::filesystem::copy_file(entry.destination, entry.backup, ec);
				}
				if (ec) {
					error = "Could not prepare destination " + PathForError(entry.destination) + ": " + ec.message();
					Rollback(error);
					return false;
				}
				entry.backedUp = true;
			}
			SyncFile(entry.staged, ec);
			if (ec) {
				error = "Could not flush staged file " + PathForError(entry.staged) + ": " + ec.message();
				Rollback(error);
				return false;
			}
		}

		for (Entry& entry : entries) {
			std::error_code ec;
			ReplaceAtomically(entry.staged, entry.destination, ec);
			if (ec) {
				error = "Could not replace destination " + PathForError(entry.destination) + ": " + ec.message();
				Rollback(error);
				return false;
			}
			entry.installed = true;
		}
		if (!SyncDestinationDirectories(error)) {
			Rollback(error);
			return false;
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

	// A crash can strand staging and rollback files beside a destination. They
	// are only removed once old enough not to belong to a running save, and only
	// while the destination exists, because older saves could have left the
	// sole copy of a file in a backup sibling.
	static void RemoveStaleTemporaryFiles(const std::filesystem::path& destination) {
		const std::filesystem::path target = ResolveSymlinks(destination);
		std::error_code ec;
		if (target.empty() || !std::filesystem::exists(target, ec) || ec) {
			return;
		}
		const std::u8string name = target.filename().u8string();
		const std::u8string stagePrefix = name + u8".rme-stage.";
		const std::u8string backupPrefix = name + u8".rme-backup.";
		const std::filesystem::path directory = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
		const auto now = std::filesystem::file_time_type::clock::now();
		for (std::filesystem::directory_iterator iterator(directory, ec), end; !ec && iterator != end; iterator.increment(ec)) {
			const std::u8string candidate = iterator->path().filename().u8string();
			if (candidate.rfind(stagePrefix, 0) != 0 && candidate.rfind(backupPrefix, 0) != 0) {
				continue;
			}
			std::error_code entryError;
			const auto modified = std::filesystem::last_write_time(iterator->path(), entryError);
			if (!entryError && now - modified > std::chrono::hours(1)) {
				std::filesystem::remove(iterator->path(), entryError);
			}
		}
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
	struct FileIdentity {
		uint64_t device = 0, file = 0;
		bool operator==(const FileIdentity& other) const {
			return device == other.device && file == other.file;
		}
	};
	static bool HardLinksUnsupported(const std::error_code& error) {
		if (error == std::errc::operation_not_supported || error == std::errc::function_not_supported) {
			return true;
		}
#ifdef _WIN32
		return error.category() == std::system_category() && (error.value() == ERROR_NOT_SUPPORTED || error.value() == ERROR_INVALID_FUNCTION);
#else
		// FAT and exFAT report EPERM. The staged file is our own, so this cannot
		// be a protected_hardlinks refusal, and the fallback is still exclusive.
		return error == std::errc::operation_not_permitted;
#endif
	}
	static std::filesystem::path ResolveSymlinks(const std::filesystem::path& destination) {
		constexpr int MaxLinkDepth = 40;
		std::filesystem::path current = destination;
		for (int depth = 0; depth < MaxLinkDepth; ++depth) {
			std::error_code ec;
			if (!std::filesystem::is_symlink(current, ec) || ec) {
				break;
			}
			const std::filesystem::path target = std::filesystem::read_symlink(current, ec);
			if (ec) {
				break;
			}
			current = target.is_absolute() ? target : current.parent_path() / target;
		}
		return current;
	}
	// Best effort: a file that cannot take the old owner still saves correctly,
	// and it is the map contents that must not be lost over a permission bit.
	static void CopyOwnerAndMode(const std::filesystem::path& source, const std::filesystem::path& target) {
#ifndef _WIN32
		struct stat info { };
		if (::stat(source.c_str(), &info) != 0) {
			return;
		}
		// Owner first: chown can clear the setuid/setgid bits restored by chmod.
		if (::chown(target.c_str(), info.st_uid, info.st_gid) != 0 && ::chown(target.c_str(), static_cast<uid_t>(-1), info.st_gid) != 0) {
			errno = 0;
		}
		if (::chmod(target.c_str(), info.st_mode & 07777) != 0) {
			errno = 0;
		}
#else
		// ReplaceFileW carries the replaced file's ACL and attributes over.
		(void)source;
		(void)target;
#endif
	}
	static void SyncFile(const std::filesystem::path& path, std::error_code& error) {
		error.clear();
#ifdef _WIN32
		const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			error = std::error_code(GetLastError(), std::system_category());
			return;
		}
		if (!FlushFileBuffers(handle)) {
			error = std::error_code(GetLastError(), std::system_category());
		}
		CloseHandle(handle);
#else
		const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor < 0) {
			error = std::error_code(errno, std::generic_category());
			return;
		}
		SyncDescriptor(descriptor, error);
		::close(descriptor);
#endif
	}
#ifndef _WIN32
	static void SyncDescriptor(int descriptor, std::error_code& error) {
		int result = 0;
		do {
			result = ::fsync(descriptor);
		} while (result != 0 && errno == EINTR);
		// Some network and FUSE filesystems have no fsync at all.
		if (result != 0 && errno != EINVAL && errno != ENOTSUP) {
			error = std::error_code(errno, std::generic_category());
		}
	}
#endif
	static void SyncDirectory(const std::filesystem::path& directory, std::error_code& error) {
		error.clear();
#ifdef _WIN32
		// MOVEFILE_WRITE_THROUGH and REPLACEFILE_WRITE_THROUGH already cover this.
		(void)directory;
#else
		const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (descriptor < 0) {
			// Search-only directories cannot be opened; the rename itself succeeded.
			if (errno != EACCES) {
				error = std::error_code(errno, std::generic_category());
			}
			return;
		}
		SyncDescriptor(descriptor, error);
		::close(descriptor);
#endif
	}
	bool SyncDestinationDirectories(std::string& error) const {
		std::set<std::filesystem::path> directories;
		for (const Entry& entry : entries) {
			directories.insert(entry.destination.has_parent_path() ? entry.destination.parent_path() : std::filesystem::path("."));
		}
		for (const std::filesystem::path& directory : directories) {
			std::error_code ec;
			SyncDirectory(directory, ec);
			if (ec) {
				error = "Could not flush directory " + PathForError(directory) + ": " + ec.message();
				return false;
			}
		}
		return true;
	}
	static void ReplaceAtomically(const std::filesystem::path& source, const std::filesystem::path& destination, std::error_code& error) {
		error.clear();
#ifdef _WIN32
		if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES
			&& ReplaceFileW(destination.c_str(), source.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
			return;
		}
		if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			error = std::error_code(GetLastError(), std::system_category());
		}
#else
		// std::filesystem::rename is rename(2), which replaces atomically.
		std::filesystem::rename(source, destination, error);
#endif
	}
	static bool ReadIdentity(const std::filesystem::path& path, FileIdentity& identity, std::error_code& error) {
		error.clear();
#ifdef _WIN32
		const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			error = std::error_code(GetLastError(), std::system_category());
			return false;
		}
		BY_HANDLE_FILE_INFORMATION info {};
		const bool read = GetFileInformationByHandle(handle, &info) != 0;
		if (!read) {
			error = std::error_code(GetLastError(), std::system_category());
		}
		CloseHandle(handle);
		if (read) {
			identity = { info.dwVolumeSerialNumber, (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow };
		}
		return read;
#else
		struct stat info { };
		if (::lstat(path.c_str(), &info) != 0) {
			error = std::error_code(errno, std::generic_category());
			return false;
		}
		identity = { static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino) };
		return true;
#endif
	}
	static void RenameExclusive(const std::filesystem::path& source, const std::filesystem::path& destination, std::error_code& error) {
		error.clear();
#ifdef _WIN32
		if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
			error = std::error_code(GetLastError(), std::system_category());
		}
#elif defined(__linux__) && defined(SYS_renameat2)
		if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) != 0) {
			error = std::error_code(errno, std::generic_category());
		}
#elif defined(__APPLE__) && defined(RENAME_EXCL)
		if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) != 0) {
			error = std::error_code(errno, std::generic_category());
		}
#else
		error = std::make_error_code(std::errc::operation_not_supported);
#endif
	}
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
		bool renamed = false;
		FileIdentity identity;
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
			if (iterator->backedUp) {
				ReplaceAtomically(iterator->backup, iterator->destination, ec);
				if (ec) {
					error += " Original remains at " + PathForError(iterator->backup) + " because rollback failed: " + ec.message() + ".";
				}
				// Either restored, which consumed the backup, or it is the only copy.
				iterator->backedUp = false;
				iterator->installed = false;
				continue;
			}
			FileIdentity current;
			const bool sameFile = mode != PublicationMode::CreateOnly || (iterator->renamed ? ReadIdentity(iterator->destination, current, ec) && current == iterator->identity : std::filesystem::equivalent(iterator->staged, iterator->destination, ec));
			if (!sameFile) {
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

		for (Entry& entry : entries) {
			if (entry.backedUp) {
				std::error_code ec;
				std::filesystem::remove(entry.backup, ec);
				entry.backedUp = false;
			}
		}
		std::string syncError;
		SyncDestinationDirectories(syncError);
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
	HardLinkFunction createLink = std::filesystem::create_hard_link;
	bool invalidDestination = false;
	bool duplicateDestination = false;
};

#endif
