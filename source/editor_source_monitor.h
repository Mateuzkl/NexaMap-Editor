//////////////////////////////////////////////////////////////////////
// Lightweight polling state for source files open in content editors.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_EDITOR_SOURCE_MONITOR_H_
#define NEXAMAP_EDITOR_SOURCE_MONITOR_H_

#include "server_workspace.h"
#include "source_text_utils.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct EditorSourceSnapshot {
	std::filesystem::path path;
	std::string text;
};

struct EditorSourceChange {
	std::filesystem::path path;
	std::string loadedText;
	std::string diskText;
	bool deleted = false;
};

class EditorSourceMonitor {
public:
	void reset(std::vector<EditorSourceSnapshot> files) {
		entries.clear();
		entries.reserve(files.size());
		for (EditorSourceSnapshot& file : files) {
			entries.push_back({ std::move(file.path), std::move(file.text), {} });
			entries.back().fingerprint = ResourceFingerprint::Read(entries.back().path);
		}
	}

	[[nodiscard]] std::vector<EditorSourceChange> poll() const {
		std::vector<EditorSourceChange> changes;
		for (const Entry& entry : entries) {
			const ResourceFingerprint current = ResourceFingerprint::Read(entry.path);
			if (entry.fingerprint == current) {
				continue;
			}
			const auto bytes = SourceText::ReadBoundedFile(entry.path, 32 * 1024 * 1024);
			if (bytes && *bytes == entry.text) {
				continue;
			}
			changes.push_back({ entry.path, entry.text, bytes.value_or(std::string()), !current.exists });
		}
		return changes;
	}

private:
	struct Entry {
		std::filesystem::path path;
		std::string text;
		ResourceFingerprint fingerprint;
	};
	std::vector<Entry> entries;
};

#endif // NEXAMAP_EDITOR_SOURCE_MONITOR_H_
