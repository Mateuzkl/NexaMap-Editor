#include "filehandle.h"
#include "file_transaction.h"
#include "path_encoding.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

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

	std::string bytes(std::initializer_list<int> values) {
		std::string result;
		for (int value : values) {
			result.push_back(static_cast<char>(value));
		}
		return result;
	}

	std::string repeated(char value, size_t count) {
		return std::string(count, value);
	}

	// Root node, then type 1 holding `nested` levels of unread nodes, then type 3.
	std::string nestedTree(size_t nested) {
		std::string tree = bytes({ 0xFE, 0x00, 0xFE, 0x01 });
		for (size_t i = 0; i < nested; ++i) {
			tree += bytes({ 0xFE, 0x02 });
		}
		tree += repeated(static_cast<char>(0xFF), nested + 1);
		tree += bytes({ 0xFE, 0x03, 0xFF, 0xFF });
		return tree;
	}

	void testReaders() {
		const auto memory = [](const std::string& tree) {
			return std::make_unique<MemoryNodeFileReadHandle>(reinterpret_cast<const uint8_t*>(tree.data()), tree.size());
		};
		uint8_t type = 0;

		// Unread children are skipped, whether they fit the depth limit or not.
		for (size_t nested : { size_t(0), size_t(1), size_t(255) }) {
			const std::string tree = nestedTree(nested);
			auto handle = memory(tree);
			BinaryNode* first = handle->getRootNode()->getChild();
			check(first != nullptr, "a skipped tree starts with its first node");
			BinaryNode* next = first ? first->advance() : nullptr;
			check(next != nullptr && next->getU8(type) && type == 3, "advance reaches the sibling behind unread children");
			check(next && next->advance() == nullptr && handle->isOk(), "a well formed tree ends without an error");
		}
		for (size_t nested : { size_t(256), size_t(300), size_t(2000000) }) {
			const std::string tree = nestedTree(nested);
			auto handle = memory(tree);
			BinaryNode* first = handle->getRootNode()->getChild();
			check(first != nullptr && first->advance() == nullptr, "the skip path stops at the depth limit");
			check(!handle->isOk(), "a tree deeper than the limit is an error");
		}

		// Reading descends through getChild, the path the map loaders recurse on.
		for (size_t nested : { size_t(255), size_t(256) }) {
			const std::string tree = nestedTree(nested);
			auto handle = memory(tree);
			BinaryNode* node = handle->getRootNode();
			size_t depth = 0;
			while (node && (node = node->getChild())) {
				++depth;
			}
			check(depth == std::min<size_t>(nested + 1, MAX_NODE_DEPTH), "getChild stops at the depth limit");
			check(handle->isOk() == (nested == 255), "getChild reports a tree past the limit");
		}

		// Abandoning an iteration halfway keeps the tree structure intact.
		{
			const std::string tree = nestedTree(8);
			auto handle = memory(tree);
			BinaryNode* first = handle->getRootNode()->getChild();
			BinaryNode* second = first->getChild();
			check(second && second->getChild() != nullptr, "open a few levels of the tree");
			check(first->getChild() == nullptr, "getChild refuses to replace an open child");
			BinaryNode* next = first->advance();
			check(next != nullptr && next->getU8(type) && type == 3, "advance recovers from an abandoned iteration");
		}

		// dowody/fh: truncated child and bad byte behind a child.
		for (const std::string& tree : { bytes({ 0xFE, 0x00, 0xFE, 0x02, 0xFE, 0x77, 0xFE, 0x01 }), bytes({ 0xFE, 0x00, 0xFE, 0x02, 0xFE, 0x77, 0xFE, 0x01, 0xFF, 0x55, 0xFF, 0xFF, 0xFF }) }) {
			const std::filesystem::path file = temporaryFile("-malformed.otbm");
			std::ofstream(file, std::ios::binary) << "OTBM" << tree;
			{
				DiskNodeFileReadHandle handle(file.string(), std::vector<std::string> { "OTBM" });
				BinaryNode* header = handle.getRootNode()->getChild();
				BinaryNode* node = header ? header->getChild() : nullptr;
				check(node != nullptr, "the malformed fixture opens");
				check(node && node->advance() == nullptr, "advance returns on a malformed child");
				check(handle.error_code != FILE_NO_ERROR, "advance reports a malformed child");
			}
			auto handle = memory(tree);
			BinaryNode* header = handle->getRootNode()->getChild();
			BinaryNode* node = header ? header->getChild() : nullptr;
			check(!node || node->advance() == nullptr, "advance returns on a malformed child in memory");
			check(!handle->isOk(), "a memory reader reports its errors");
			std::error_code ignored;
			std::filesystem::remove(file, ignored);
		}

		// Typed reads work at any offset, and lengths cannot run past the node.
		{
			const std::string tree = bytes({ 0xFE, 0x00, 0x78, 0x56, 0x34, 0x12, 0xEF, 0xFF });
			auto handle = memory(tree);
			BinaryNode* root = handle->getRootNode();
			uint32_t value = 0;
			check(root->getU8(type) && root->getU32(value) && value == 0x12345678u, "a misaligned value is read intact");
			check(!root->skip(size_t(-1)) && !root->getU8(type), "a huge skip stops at the node end");
		}
	}

	void testMemoryWriterBounds() {
		MemoryNodeFileWriteHandle writer;
		std::vector<uint8_t> expected;
		// Each round leaves the cache exactly full, which is where the node markers
		// used to be written before the bounds check.
		for (size_t capacity : { size_t(0x7FFF), size_t(0xFFFE), size_t(0x1FFFC) }) {
			const std::vector<uint8_t> filler(capacity - writer.getSize(), 0x11);
			check(writer.addRAW(filler.data(), filler.size()), "filler is written");
			check(writer.addNode(0x42) && writer.endNode(), "node markers fit a full cache");
			expected.insert(expected.end(), filler.begin(), filler.end());
			expected.insert(expected.end(), { NODE_START, 0x42, NODE_END });
		}
		check(writer.getSize() == expected.size() && std::equal(expected.begin(), expected.end(), writer.getData()), "growth keeps every byte");
	}

	void testSidecarBaseName() {
		using PathEncoding::SidecarBaseName;
		check(SidecarBaseName("world-house.xml") == "world-house.xml", "a plain name is unchanged");
		check(SidecarBaseName("S\xC3\xA3o Jo\xC3\xA3o-spawn.xml") == "S\xC3\xA3o Jo\xC3\xA3o-spawn.xml", "a UTF-8 name is unchanged");
		check(SidecarBaseName("..\\..\\windows\\evil.xml") == "evil.xml", "backslash paths are reduced on every platform");
		check(SidecarBaseName("../../etc/evil.xml") == "evil.xml", "relative paths are reduced");
		check(SidecarBaseName("/etc/evil.xml") == "evil.xml", "absolute paths are reduced");
		check(SidecarBaseName("sub/world-spawn.xml") == "world-spawn.xml", "folders are removed");
		check(SidecarBaseName(std::string("good.xml\0/../evil.xml", 21)) == "good.xml", "a name ends at the first NUL");
		for (const char* unusable : { "", ".", "..", "../..", "a/", "a\\" }) {
			check(SidecarBaseName(unusable).empty(), "nothing usable is left of a directory reference");
		}
	}
}

int main() {
	testReaders();
	testMemoryWriterBounds();
	testSidecarBaseName();

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

	const std::filesystem::path truncated = temporaryFile("-truncated.otbm");
	{
		std::ofstream header(truncated, std::ios::binary);
		header << "OTBM";
	}
	{
		DiskNodeFileReadHandle handle(truncated.string(), std::vector<std::string> { "OTBM" });
		check(handle.isOk(), "truncated fixture has a valid file identifier");
		check(handle.getRootNode() == nullptr, "missing root marker is rejected");
		check(handle.error_code == FILE_READ_ERROR, "missing root marker reports a read error");
	}
	std::filesystem::remove(truncated, cleanupError);

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

	// Publication: live files stay in place, are replaced atomically, and keep
	// their identity. Every case runs in its own scratch directory.
	const std::filesystem::path scratch = temporaryFile("-publication");
	std::filesystem::create_directories(scratch);
	const auto write = [](const std::filesystem::path& file, const std::string& text) {
		std::ofstream(file, std::ios::binary) << text;
	};
	const auto read = [](const std::filesystem::path& file) {
		std::ifstream stream(file, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), {});
	};
	const auto temporaries = [&] {
		int count = 0;
		for (const auto& entry : std::filesystem::directory_iterator(scratch)) {
			count += entry.path().filename().string().find(".rme-") != std::string::npos ? 1 : 0;
		}
		return count;
	};

	const std::filesystem::path live = scratch / "live.otbm";
	write(live, "old");
	{
		FileSaveTransaction transaction;
		const std::filesystem::path stage = transaction.Stage(live);
		write(stage, "new");
		check(read(live) == "old", "the destination keeps its contents until commit");
		std::string error;
		check(transaction.Commit(error), "replace commit succeeds");
		check(read(live) == "new", "commit publishes the staged contents");
		check(temporaries() == 0, "commit leaves no temporary files");
	}

#ifndef _WIN32
	const std::filesystem::path linkTarget = scratch / "target.otbm";
	const std::filesystem::path link = scratch / "link.otbm";
	write(linkTarget, "old");
	std::filesystem::permissions(linkTarget, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read);
	std::filesystem::create_symlink("target.otbm", link);
	{
		FileSaveTransaction transaction;
		write(transaction.Stage(link), "new");
		std::string error;
		check(transaction.Commit(error), "commit through a symlink succeeds");
		check(std::filesystem::is_symlink(link), "the symlink survives");
		check(read(linkTarget) == "new", "the link's target receives the contents");
		const auto mode = std::filesystem::status(linkTarget).permissions() & std::filesystem::perms::all;
		check(mode == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read), "the file mode survives");
	}
	const std::filesystem::path dangling = scratch / "dangling.otbm";
	std::filesystem::create_symlink("created-by-save.otbm", dangling);
	{
		FileSaveTransaction transaction;
		write(transaction.Stage(dangling), "new");
		std::string error;
		check(transaction.Commit(error), "commit through a dangling symlink succeeds");
		check(std::filesystem::is_symlink(dangling) && read(scratch / "created-by-save.otbm") == "new", "a dangling symlink's target is created");
	}
#endif

	// The second replace fails, so the first file must come back from its backup.
	const std::filesystem::path first = scratch / "first.otbm";
	const std::filesystem::path blocked = scratch / "blocked";
	write(first, "first old");
	std::filesystem::create_directory(blocked);
	{
		const FileSaveTransaction::HardLinkFunction linkOrSkip = [](const std::filesystem::path& source, const std::filesystem::path& target, std::error_code& error) {
			if (source.filename() != "blocked") {
				std::filesystem::create_hard_link(source, target, error);
			}
		};
		FileSaveTransaction transaction(FileSaveTransaction::PublicationMode::Replace, linkOrSkip);
		write(transaction.Stage(first), "first new");
		write(transaction.Stage(blocked), "blocked new");
		std::string error;
		check(!transaction.Commit(error) && !error.empty(), "a failed replace fails the commit");
		check(read(first) == "first old", "a failed commit restores earlier files");
	}
	check(std::filesystem::is_directory(blocked), "a failed commit leaves the blocked path alone");
	check(temporaries() == 0, "rollback leaves no temporary files");
	std::filesystem::remove(blocked, cleanupError);

	// FAT and exFAT refuse hard links with EPERM; publication must still work.
	const std::filesystem::path noLinks = scratch / "no-links.otbm";
	{
		const FileSaveTransaction::HardLinkFunction refuse = [](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error) {
			error = std::make_error_code(std::errc::operation_not_permitted);
		};
		FileSaveTransaction transaction(FileSaveTransaction::PublicationMode::CreateOnly, refuse);
		write(transaction.Stage(noLinks), "new");
		std::string error;
		check(transaction.Commit(error), "create-only publication survives EPERM from link");
		check(read(noLinks) == "new", "the exclusive fallback publishes the file");
	}
	{
		const FileSaveTransaction::HardLinkFunction refuse = [](const std::filesystem::path&, const std::filesystem::path&, std::error_code& error) {
			error = std::make_error_code(std::errc::operation_not_permitted);
		};
		FileSaveTransaction transaction(FileSaveTransaction::PublicationMode::CreateOnly, refuse);
		write(transaction.Stage(noLinks), "other");
		std::string error;
		check(!transaction.Commit(error), "the exclusive fallback never overwrites");
		check(read(noLinks) == "new", "the existing file survives");
	}

	// Stale temporaries go; recent ones and the only copy of a file stay.
	const std::filesystem::path swept = scratch / "swept.otbm";
	write(swept, "live");
	const std::filesystem::path staleStage = scratch / "swept.otbm.rme-stage.1.2";
	const std::filesystem::path staleBackup = scratch / "swept.otbm.rme-backup.1.3";
	const std::filesystem::path recentStage = scratch / "swept.otbm.rme-stage.1.4";
	const std::filesystem::path unrelated = scratch / "swept.otbm2.rme-stage.1.5";
	const std::filesystem::path onlyCopy = scratch / "gone.otbm.rme-backup.1.6";
	for (const auto& file : { staleStage, staleBackup, unrelated, onlyCopy }) {
		write(file, "x");
		std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
	}
	write(recentStage, "x");
	FileSaveTransaction::RemoveStaleTemporaryFiles(swept);
	FileSaveTransaction::RemoveStaleTemporaryFiles(scratch / "gone.otbm");
	check(!std::filesystem::exists(staleStage) && !std::filesystem::exists(staleBackup), "stale temporaries are removed");
	check(std::filesystem::exists(recentStage), "a running save keeps its temporaries");
	check(std::filesystem::exists(unrelated), "other files' temporaries are left alone");
	check(std::filesystem::exists(onlyCopy), "a backup of a missing destination is kept");
	std::filesystem::remove_all(scratch, cleanupError);

	std::cout << "File Handle Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
