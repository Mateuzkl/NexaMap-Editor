#ifndef NEXAMAP_CROSS_CLIENT_CLIPBOARD_H_
#define NEXAMAP_CROSS_CLIENT_CLIPBOARD_H_

#include "graphics.h"
#include "position.h"
#include "spawn_source_remap.h"
#include "house.h"
#include "session_id.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <wx/string.h>

class BaseMap;
class CopyBuffer;
class EditorResourceSession;

enum class CrossClientMatchState : uint8_t {
	Matched,
	Remapped,
	Missing,
};

struct CrossClientItemSnapshot {
	uint16_t sourceId = 0;
	uint16_t sourceClientId = 0;
	uint32_t occurrences = 0;
	uint16_t group = 0;
	uint16_t type = 0;
	uint64_t semanticFlags = 0;
	std::string name;
	SpriteVisualFingerprint renderedVisual;
	bool renderedVisualAvailable = false;
	SpriteVisualFingerprint previewVisual;
	bool previewAvailable = false;
	int previewWidth = 0;
	int previewHeight = 0;
	std::vector<uint8_t> previewRgba;
};

struct CrossClientItemRecommendation {
	uint16_t destinationId = 0;
	uint16_t destinationClientId = 0;
	std::string destinationName;
	uint8_t confidence = 0;
	bool automatic = false;
	bool visualCompared = false;
	double visualSimilarity = 0.0;
	bool allVariantsVerified = false;
};

struct CrossClientPasteRow {
	CrossClientItemSnapshot source;
	uint16_t destinationId = 0;
	uint16_t destinationClientId = 0;
	std::string destinationName;
	CrossClientMatchState state = CrossClientMatchState::Missing;
	wxString detail;
	std::vector<CrossClientItemRecommendation> recommendations;
};

struct CrossClientPasteAnalysis {
	bool valid = false;
	uint64_t clipboardGeneration = 0;
	SessionId destinationGraphicsIdentity = InvalidSessionId;
	uint64_t destinationWorkspaceGeneration = 0;
	std::weak_ptr<EditorResourceSession> sourceSession;
	std::weak_ptr<EditorResourceSession> destinationSession;
	wxString sourceClient;
	wxString sourceServer;
	wxString destinationClient;
	wxString destinationServer;
	std::vector<CrossClientPasteRow> rows;
	uint32_t matched = 0;
	uint32_t remapped = 0;
	uint32_t missing = 0;
	uint32_t totalOccurrences = 0;

	bool canApply() const noexcept {
		return valid && missing == 0;
	}
};

class CrossClientClipboard final {
public:
	using ProgressCallback = std::function<bool(size_t current, size_t total)>;

	CrossClientClipboard();
	~CrossClientClipboard();

	CrossClientClipboard(const CrossClientClipboard&) = delete;
	CrossClientClipboard& operator=(const CrossClientClipboard&) = delete;

	bool capture(CopyBuffer& source, const std::shared_ptr<EditorResourceSession>& session, wxString& error);
	void clear();

	bool canPaste() const noexcept;
	bool isFromSession(const std::shared_ptr<EditorResourceSession>& session) const noexcept;
	CrossClientPasteAnalysis analyze(
		const std::shared_ptr<EditorResourceSession>& destinationSession,
		const ProgressCallback& progress,
		wxString& error
	) const;
	static bool resolveMapping(CrossClientPasteAnalysis& analysis, size_t rowIndex, uint16_t destinationId, wxString& error);
	static bool isCompatibleDestination(const CrossClientItemSnapshot& source, uint16_t destinationId);
	static bool hasSafeAutomaticMapping(const CrossClientPasteRow& row);
	bool apply(const CrossClientPasteAnalysis& analysis, CopyBuffer& destination, wxString& error);

private:
	std::unique_ptr<BaseMap> map;
	Position copyPosition;
	std::weak_ptr<EditorResourceSession> sourceSession;
	wxString sourceClient;
	wxString sourceServer;
	std::vector<CrossClientItemSnapshot> items;
	SpawnDependencyMap spawnDependencies;
	std::map<uint32_t, HouseSnapshot> houses;
	SessionId copiedMapSession = InvalidSessionId;
	SessionId sourceGraphicsIdentity = InvalidSessionId;
	uint64_t generation = 0;
};

#endif // NEXAMAP_CROSS_CLIENT_CLIPBOARD_H_
