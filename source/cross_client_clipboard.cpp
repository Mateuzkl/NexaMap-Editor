#include "main.h"

#include "cross_client_clipboard.h"

#include "basemap.h"
#include "complexitem.h"
#include "copybuffer.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "items.h"
#include "workspace_session.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
	struct FingerprintHash {
		size_t operator()(const SpriteVisualFingerprint& fingerprint) const noexcept {
			const uint64_t mixed = fingerprint.primary ^ (fingerprint.secondary + 0x9E3779B97F4A7C15ull + (fingerprint.primary << 6) + (fingerprint.primary >> 2));
			return static_cast<size_t>(mixed ^ fingerprint.pixelBytes);
		}
	};

	struct TargetCandidate {
		uint16_t id = 0;
		uint16_t clientId = 0;
		uint16_t group = 0;
		uint16_t type = 0;
		uint64_t semanticFlags = 0;
		std::string name;
	};

	wxString DisplayPath(const std::filesystem::path& path) {
		if (path.empty()) {
			return "Not configured";
		}
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	uint64_t SemanticFlags(const ItemType& item) {
		uint64_t flags = 0;
		auto add = [&](bool value, int bit) {
			if (value) {
				flags |= uint64_t(1) << bit;
			}
		};
		add(item.stackable, 0);
		add(item.moveable, 1);
		add(item.pickupable, 2);
		add(item.alwaysOnBottom, 3);
		add(item.unpassable, 4);
		add(item.blockMissiles, 5);
		add(item.blockPathfinder, 6);
		add(item.hasElevation, 7);
		add(item.isHangable, 8);
		add(item.hookEast, 9);
		add(item.hookSouth, 10);
		add(item.canReadText, 11);
		add(item.canWriteText, 12);
		add(item.rotable, 13);
		add(item.isGroundTile(), 14);
		add(item.isContainer(), 15);
		add(item.isTeleport(), 16);
		add(item.isDoor(), 17);
		add(item.isDepot(), 18);
		add(item.isFluidContainer(), 19);
		add(item.isSplash(), 20);
		add(item.isPodium(), 21);
		flags |= static_cast<uint64_t>(item.alwaysOnTopOrder & 0x7) << 24;
		return flags;
	}

	void CountItem(const Item* item, std::map<uint16_t, uint32_t>& counts) {
		if (!item) {
			return;
		}
		++counts[item->getID()];
		if (const auto* container = dynamic_cast<const Container*>(item)) {
			for (size_t index = 0; index < container->getItemCount(); ++index) {
				CountItem(container->getItem(index), counts);
			}
		}
	}

	void VisitMapItems(BaseMap& map, const std::function<void(Item*)>& visitor) {
		std::function<void(Item*)> visitItem = [&](Item* item) {
			if (!item) {
				return;
			}
			visitor(item);
			if (auto* container = dynamic_cast<Container*>(item)) {
				for (size_t index = 0; index < container->getItemCount(); ++index) {
					visitItem(container->getItem(index));
				}
			}
		};
		for (MapIterator iterator = map.begin(); iterator != map.end(); ++iterator) {
			Tile* tile = (*iterator)->get();
			visitItem(tile->ground);
			for (Item* item : tile->items) {
				visitItem(item);
			}
		}
	}

	std::unique_ptr<BaseMap> CloneMap(BaseMap& source) {
		auto clone = std::make_unique<BaseMap>();
		for (MapIterator iterator = source.begin(); iterator != source.end(); ++iterator) {
			Tile* sourceTile = (*iterator)->get();
			TileLocation* location = clone->createTileL(sourceTile->getPosition());
			Tile* copiedTile = sourceTile->deepCopy(*clone);
			copiedTile->setLocation(location);
			clone->setTile(location, copiedTile);
		}
		return clone;
	}

	int MatchScore(const CrossClientItemSnapshot& source, const TargetCandidate& target) {
		int score = 0;
		score += source.group == target.group ? 30 : 0;
		score += source.type == target.type ? 30 : 0;
		score += source.semanticFlags == target.semanticFlags ? 50 : 0;
		score += source.sourceClientId == target.clientId ? 15 : 0;
		score += source.sourceId == target.id ? 20 : 0;
		if (!source.name.empty() && !target.name.empty() && wxString::FromUTF8(source.name).CmpNoCase(wxString::FromUTF8(target.name)) == 0) {
			score += 25;
		}
		return score;
	}

	bool StructurallyCompatible(const CrossClientItemSnapshot& source, const TargetCandidate& target) {
		return source.group == target.group && source.type == target.type;
	}

	SpriteVisualFingerprint PixelFingerprint(const std::vector<uint8_t>& pixels, int width, int height) {
		SpriteVisualFingerprint fingerprint;
		if (width <= 0 || height <= 0 || pixels.size() != static_cast<size_t>(width) * height * 4) {
			return fingerprint;
		}
		constexpr uint64_t FnvOffset = 14695981039346656037ull;
		constexpr uint64_t FnvPrime = 1099511628211ull;
		fingerprint.primary = FnvOffset;
		fingerprint.secondary = 0x9E3779B97F4A7C15ull;
		auto hashByte = [&](uint8_t byte) {
			fingerprint.primary = (fingerprint.primary ^ byte) * FnvPrime;
			fingerprint.secondary ^= static_cast<uint64_t>(byte) + 0x9E3779B97F4A7C15ull + (fingerprint.secondary << 6) + (fingerprint.secondary >> 2);
		};
		for (int shift = 0; shift < 32; shift += 8) {
			hashByte(static_cast<uint8_t>((static_cast<uint32_t>(width) >> shift) & 0xFF));
			hashByte(static_cast<uint8_t>((static_cast<uint32_t>(height) >> shift) & 0xFF));
		}
		for (uint8_t byte : pixels) {
			hashByte(byte);
		}
		fingerprint.pixelBytes = pixels.size();
		return fingerprint;
	}
}

CrossClientClipboard::CrossClientClipboard() = default;
CrossClientClipboard::~CrossClientClipboard() = default;

bool CrossClientClipboard::capture(CopyBuffer& source, const std::shared_ptr<EditorResourceSession>& session, wxString& error) {
	error.clear();
	if (!session || !source.canPaste()) {
		error = "There is no copied map area to place in the cross-client clipboard.";
		return false;
	}

	auto capturedMap = CloneMap(source.getBufferMap());
	std::map<uint16_t, uint32_t> counts;
	for (MapIterator iterator = capturedMap->begin(); iterator != capturedMap->end(); ++iterator) {
		Tile* tile = (*iterator)->get();
		CountItem(tile->ground, counts);
		for (Item* item : tile->items) {
			CountItem(item, counts);
		}
	}

	std::vector<CrossClientItemSnapshot> capturedItems;
	capturedItems.reserve(counts.size());
	for (const auto& [id, occurrences] : counts) {
		CrossClientItemSnapshot snapshot;
		snapshot.sourceId = id;
		snapshot.occurrences = occurrences;
		if (g_items.typeExists(id)) {
			const ItemType& item = g_items[id];
			snapshot.sourceClientId = item.clientID;
			snapshot.group = static_cast<uint16_t>(item.group);
			snapshot.type = static_cast<uint16_t>(item.type);
			snapshot.semanticFlags = SemanticFlags(item);
			snapshot.name = item.name;
			GameSprite* sprite = item.sprite;
			if (!sprite && item.clientID != 0) {
				sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			}
			if (sprite) {
				bool pending = false;
				snapshot.visualAvailable = sprite->getVisualFingerprint(snapshot.visual, pending, false);
				pending = false;
				snapshot.previewAvailable = sprite->getVisualPreviewRGBA(snapshot.previewRgba, snapshot.previewWidth, snapshot.previewHeight, pending, false);
				if (snapshot.previewAvailable) {
					snapshot.previewVisual = PixelFingerprint(snapshot.previewRgba, snapshot.previewWidth, snapshot.previewHeight);
				}
			}
		}
		capturedItems.push_back(std::move(snapshot));
	}

	map = std::move(capturedMap);
	copyPosition = source.getPosition();
	sourceSession = session;
	const WorkspaceClientSelection& client = g_workspace.getClient();
	sourceClient = client.rootPath.empty() ? wxString("Not configured") : client.rootPath;
	sourceServer = DisplayPath(g_workspace.getServer().rootPath);
	items = std::move(capturedItems);
	if (++generation == 0) {
		++generation;
	}
	return true;
}

void CrossClientClipboard::clear() {
	map.reset();
	sourceSession.reset();
	sourceClient.clear();
	sourceServer.clear();
	items.clear();
	if (++generation == 0) {
		++generation;
	}
}

bool CrossClientClipboard::canPaste() const noexcept {
	return map && map->size() != 0 && sourceSession;
}

bool CrossClientClipboard::isFromSession(const std::shared_ptr<EditorResourceSession>& session) const noexcept {
	return canPaste() && session && sourceSession == session;
}

CrossClientPasteAnalysis CrossClientClipboard::analyze(
	const std::shared_ptr<EditorResourceSession>& destinationSession,
	const ProgressCallback& progress,
	wxString& error
) const {
	CrossClientPasteAnalysis analysis;
	error.clear();
	if (!canPaste() || !destinationSession) {
		error = "The cross-client clipboard is empty or the destination tab has no resource session.";
		return analysis;
	}

	analysis.clipboardGeneration = generation;
	analysis.valid = true;
	analysis.sourceSession = sourceSession;
	analysis.destinationSession = destinationSession;
	analysis.sourceClient = sourceClient;
	analysis.sourceServer = sourceServer;
	const WorkspaceClientSelection& targetClient = g_workspace.getClient();
	analysis.destinationClient = targetClient.rootPath.empty() ? wxString("Not configured") : targetClient.rootPath;
	analysis.destinationServer = DisplayPath(g_workspace.getServer().rootPath);

	std::unordered_map<SpriteVisualFingerprint, std::vector<TargetCandidate>, FingerprintHash> candidates;
	std::unordered_map<uint16_t, SpriteVisualFingerprint> spriteFingerprints;
	std::unordered_map<uint16_t, bool> spriteAvailability;
	std::unordered_set<SpriteVisualFingerprint, FingerprintHash> sourcePreviews;
	for (const CrossClientItemSnapshot& source : items) {
		if (source.visualAvailable && source.previewAvailable) {
			sourcePreviews.insert(source.previewVisual);
		}
	}
	const size_t maximumId = g_items.getMaxID();
	for (size_t id = 1; !sourcePreviews.empty() && id <= maximumId; ++id) {
		if (progress && id % 32 == 0 && !progress(id, maximumId)) {
			error = "Cross-client comparison was cancelled.";
			return {};
		}
		if (!g_items.typeExists(static_cast<int>(id))) {
			continue;
		}
		const ItemType& item = g_items[id];
		if (item.clientID == 0) {
			continue;
		}

		SpriteVisualFingerprint fingerprint;
		bool available = false;
		const auto cached = spriteAvailability.find(item.clientID);
		if (cached != spriteAvailability.end()) {
			available = cached->second;
			if (available) {
				fingerprint = spriteFingerprints[item.clientID];
			}
		} else {
			GameSprite* sprite = item.sprite;
			if (!sprite) {
				sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			}
			bool pending = false;
			std::vector<uint8_t> previewPixels;
			int previewWidth = 0;
			int previewHeight = 0;
			const bool previewAvailable = sprite && sprite->getVisualPreviewRGBA(previewPixels, previewWidth, previewHeight, pending, false);
			if (previewAvailable && sourcePreviews.contains(PixelFingerprint(previewPixels, previewWidth, previewHeight))) {
				pending = false;
				available = sprite->getVisualFingerprint(fingerprint, pending, false);
			}
			spriteAvailability[item.clientID] = available;
			if (available) {
				spriteFingerprints[item.clientID] = fingerprint;
			}
		}
		if (!available) {
			continue;
		}

		TargetCandidate candidate;
		candidate.id = static_cast<uint16_t>(id);
		candidate.clientId = item.clientID;
		candidate.group = static_cast<uint16_t>(item.group);
		candidate.type = static_cast<uint16_t>(item.type);
		candidate.semanticFlags = SemanticFlags(item);
		candidate.name = item.name;
		candidates[fingerprint].push_back(std::move(candidate));
	}
	if (progress) {
		progress(maximumId, maximumId);
	}

	analysis.rows.reserve(items.size());
	for (const CrossClientItemSnapshot& source : items) {
		CrossClientPasteRow row;
		row.source = source;
		analysis.totalOccurrences += source.occurrences;
		if (!source.visualAvailable) {
			row.detail = "The source sprite could not be read, so it cannot be remapped safely.";
			++analysis.missing;
			analysis.rows.push_back(std::move(row));
			continue;
		}

		const auto matches = candidates.find(source.visual);
		if (matches == candidates.end() || matches->second.empty()) {
			row.detail = "No byte-identical sprite/item exists in the destination client.";
			++analysis.missing;
			analysis.rows.push_back(std::move(row));
			continue;
		}

		const TargetCandidate* best = nullptr;
		int bestScore = std::numeric_limits<int>::min();
		for (const TargetCandidate& candidate : matches->second) {
			if (!StructurallyCompatible(source, candidate)) {
				continue;
			}
			const int score = MatchScore(source, candidate);
			if (!best || score > bestScore || (score == bestScore && candidate.id < best->id)) {
				best = &candidate;
				bestScore = score;
			}
		}
		if (!best) {
			row.detail = "Identical graphics were found, but the destination item type is incompatible.";
			++analysis.missing;
			analysis.rows.push_back(std::move(row));
			continue;
		}
		row.destinationId = best->id;
		row.destinationClientId = best->clientId;
		row.destinationName = best->name;
		if (source.sourceId == best->id) {
			row.state = CrossClientMatchState::Matched;
			row.detail = "The destination already uses the same map item ID and identical graphics.";
			++analysis.matched;
		} else {
			row.state = CrossClientMatchState::Remapped;
			row.detail = wxString::Format("Map item ID %u will be replaced with %u.", source.sourceId, best->id);
			++analysis.remapped;
		}
		analysis.rows.push_back(std::move(row));
	}
	return analysis;
}

bool CrossClientClipboard::apply(const CrossClientPasteAnalysis& analysis, CopyBuffer& destination, wxString& error) {
	error.clear();
	if (!canPaste() || analysis.clipboardGeneration != generation || analysis.sourceSession != sourceSession) {
		error = "The copied area changed while the cross-client verification window was open.";
		return false;
	}
	if (analysis.destinationSession != GetActiveEditorResourceSession()) {
		error = "The destination resource session changed before the paste was applied.";
		return false;
	}
	if (!analysis.canApply()) {
		error = "The copied area contains missing resources. No map or client file was changed.";
		return false;
	}

	std::unordered_map<uint16_t, uint16_t> remap;
	for (const CrossClientPasteRow& row : analysis.rows) {
		remap[row.source.sourceId] = row.destinationId;
	}
	std::vector<std::pair<Item*, uint16_t>> changedItems;
	VisitMapItems(*map, [&](Item* item) {
		const auto mapping = remap.find(item->getID());
		if (mapping != remap.end()) {
			changedItems.emplace_back(item, item->getID());
			item->setID(mapping->second);
		}
	});
	struct RestoreItemIds {
		std::vector<std::pair<Item*, uint16_t>>& items;
		~RestoreItemIds() {
			for (auto iterator = items.rbegin(); iterator != items.rend(); ++iterator) {
				iterator->first->setID(iterator->second);
			}
		}
	} restore { changedItems };

	std::unique_ptr<BaseMap> converted = CloneMap(*map);
	if (!converted || converted->size() == 0) {
		error = "The converted paste buffer is empty.";
		return false;
	}
	destination.replace(converted.release(), copyPosition);
	return true;
}
