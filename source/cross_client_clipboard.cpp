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
#include <cstdlib>
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

	struct TargetSpriteCache {
		bool visualAvailable = false;
		SpriteVisualFingerprint visual;
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

	double PreviewSimilarity(
		const std::vector<uint8_t>& source,
		int sourceWidth,
		int sourceHeight,
		const std::vector<uint8_t>& target,
		int targetWidth,
		int targetHeight
	) {
		if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0
			|| source.size() != static_cast<size_t>(sourceWidth) * sourceHeight * 4
			|| target.size() != static_cast<size_t>(targetWidth) * targetHeight * 4) {
			return 0.0;
		}

		constexpr int SampleSize = 16;
		double sourceAlpha = 0.0;
		double targetAlpha = 0.0;
		double alphaIntersection = 0.0;
		double colourSimilarity = 0.0;
		double colourWeight = 0.0;
		for (int y = 0; y < SampleSize; ++y) {
			const int sourceY = std::min(sourceHeight - 1, y * sourceHeight / SampleSize);
			const int targetY = std::min(targetHeight - 1, y * targetHeight / SampleSize);
			for (int x = 0; x < SampleSize; ++x) {
				const int sourceX = std::min(sourceWidth - 1, x * sourceWidth / SampleSize);
				const int targetX = std::min(targetWidth - 1, x * targetWidth / SampleSize);
				const size_t sourceOffset = (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
				const size_t targetOffset = (static_cast<size_t>(targetY) * targetWidth + targetX) * 4;
				const double sourceOpacity = source[sourceOffset + 3] / 255.0;
				const double targetOpacity = target[targetOffset + 3] / 255.0;
				const double overlap = std::min(sourceOpacity, targetOpacity);
				sourceAlpha += sourceOpacity;
				targetAlpha += targetOpacity;
				alphaIntersection += overlap;
				if (overlap > 0.0) {
					const double colourDistance = (std::abs(static_cast<int>(source[sourceOffset + 0]) - static_cast<int>(target[targetOffset + 0]))
												   + std::abs(static_cast<int>(source[sourceOffset + 1]) - static_cast<int>(target[targetOffset + 1]))
												   + std::abs(static_cast<int>(source[sourceOffset + 2]) - static_cast<int>(target[targetOffset + 2])))
						/ (255.0 * 3.0);
					colourSimilarity += (1.0 - colourDistance) * overlap;
					colourWeight += overlap;
				}
			}
		}

		const double alphaScore = sourceAlpha + targetAlpha > 0.0 ? (2.0 * alphaIntersection) / (sourceAlpha + targetAlpha) : 0.0;
		const double colourScore = colourWeight > 0.0 ? colourSimilarity / colourWeight : 0.0;
		const double widthRatio = static_cast<double>(std::min(sourceWidth, targetWidth)) / std::max(sourceWidth, targetWidth);
		const double heightRatio = static_cast<double>(std::min(sourceHeight, targetHeight)) / std::max(sourceHeight, targetHeight);
		return std::clamp(alphaScore * 0.55 + colourScore * 0.4 + widthRatio * heightRatio * 0.05, 0.0, 1.0);
	}

	void AddRecommendation(std::vector<CrossClientItemRecommendation>& recommendations, const TargetCandidate& target, double similarity) {
		if (similarity < 0.45) {
			return;
		}
		CrossClientItemRecommendation recommendation;
		recommendation.destinationId = target.id;
		recommendation.destinationClientId = target.clientId;
		recommendation.destinationName = target.name;
		recommendation.confidence = static_cast<uint8_t>(std::clamp(static_cast<int>(similarity * 100.0 + 0.5), 0, 100));
		recommendations.push_back(std::move(recommendation));
		std::sort(recommendations.begin(), recommendations.end(), [](const CrossClientItemRecommendation& left, const CrossClientItemRecommendation& right) {
			return left.confidence == right.confidence ? left.destinationId < right.destinationId : left.confidence > right.confidence;
		});
		if (recommendations.size() > 16) {
			recommendations.resize(16);
		}
	}

	void Recount(CrossClientPasteAnalysis& analysis) {
		analysis.matched = 0;
		analysis.remapped = 0;
		analysis.missing = 0;
		for (const CrossClientPasteRow& row : analysis.rows) {
			switch (row.state) {
				case CrossClientMatchState::Matched:
					++analysis.matched;
					break;
				case CrossClientMatchState::Remapped:
					++analysis.remapped;
					break;
				case CrossClientMatchState::Missing:
					++analysis.missing;
					break;
			}
		}
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
	std::unordered_map<uint16_t, TargetSpriteCache> spriteCache;
	std::unordered_set<SpriteVisualFingerprint, FingerprintHash> sourcePreviews;
	for (const CrossClientItemSnapshot& source : items) {
		if (source.visualAvailable && source.previewAvailable) {
			sourcePreviews.insert(source.previewVisual);
		}
	}
	const size_t maximumId = std::min<size_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max());
	const size_t totalProgress = maximumId > std::numeric_limits<size_t>::max() / 2 ? maximumId : maximumId * 2;
	for (size_t id = 1; id <= maximumId; ++id) {
		if (progress && id % 32 == 0 && !progress(id, totalProgress)) {
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

		auto [cached, inserted] = spriteCache.try_emplace(item.clientID);
		TargetSpriteCache& spriteData = cached->second;
		if (inserted && !sourcePreviews.empty()) {
			GameSprite* sprite = item.sprite;
			if (!sprite) {
				sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			}
			bool pending = false;
			std::vector<uint8_t> previewRgba;
			int previewWidth = 0;
			int previewHeight = 0;
			const bool previewAvailable = sprite && sprite->getVisualPreviewRGBA(previewRgba, previewWidth, previewHeight, pending, false);
			if (previewAvailable && sourcePreviews.contains(PixelFingerprint(previewRgba, previewWidth, previewHeight))) {
				pending = false;
				spriteData.visualAvailable = sprite->getVisualFingerprint(spriteData.visual, pending, false);
			}
		}

		TargetCandidate candidate;
		candidate.id = static_cast<uint16_t>(id);
		candidate.clientId = item.clientID;
		candidate.group = static_cast<uint16_t>(item.group);
		candidate.type = static_cast<uint16_t>(item.type);
		candidate.semanticFlags = SemanticFlags(item);
		candidate.name = item.name;
		if (spriteData.visualAvailable) {
			candidates[spriteData.visual].push_back(std::move(candidate));
		}
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

	const bool needsRecommendations = std::any_of(analysis.rows.begin(), analysis.rows.end(), [](const CrossClientPasteRow& row) {
		return row.state == CrossClientMatchState::Missing && row.source.previewAvailable;
	});
	if (needsRecommendations) {
		for (size_t id = 1; id <= maximumId; ++id) {
			if (progress && id % 32 == 0 && !progress(maximumId + id, totalProgress)) {
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
			GameSprite* sprite = item.sprite;
			if (!sprite) {
				sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			}
			bool pending = false;
			std::vector<uint8_t> previewRgba;
			int previewWidth = 0;
			int previewHeight = 0;
			if (!sprite || !sprite->getVisualPreviewRGBA(previewRgba, previewWidth, previewHeight, pending, false)) {
				continue;
			}

			TargetCandidate candidate;
			candidate.id = static_cast<uint16_t>(id);
			candidate.clientId = item.clientID;
			candidate.group = static_cast<uint16_t>(item.group);
			candidate.type = static_cast<uint16_t>(item.type);
			candidate.semanticFlags = SemanticFlags(item);
			candidate.name = item.name;
			for (CrossClientPasteRow& row : analysis.rows) {
				if (row.state != CrossClientMatchState::Missing || !row.source.previewAvailable || !StructurallyCompatible(row.source, candidate)) {
					continue;
				}
				double similarity = PreviewSimilarity(
					row.source.previewRgba,
					row.source.previewWidth,
					row.source.previewHeight,
					previewRgba,
					previewWidth,
					previewHeight
				);
				if (row.source.semanticFlags == candidate.semanticFlags) {
					similarity = std::min(1.0, similarity + 0.03);
				}
				AddRecommendation(row.recommendations, candidate, similarity);
			}
		}
	}
	if (progress) {
		progress(totalProgress, totalProgress);
	}
	return analysis;
}

bool CrossClientClipboard::isCompatibleDestination(const CrossClientItemSnapshot& source, uint16_t destinationId) {
	if (destinationId == 0 || !g_items.typeExists(destinationId)) {
		return false;
	}
	const ItemType& item = g_items[destinationId];
	return source.group == static_cast<uint16_t>(item.group) && source.type == static_cast<uint16_t>(item.type);
}

bool CrossClientClipboard::resolveMapping(CrossClientPasteAnalysis& analysis, size_t rowIndex, uint16_t destinationId, wxString& error) {
	error.clear();
	if (!analysis.valid || rowIndex >= analysis.rows.size()) {
		error = "The selected cross-client row is no longer available.";
		return false;
	}
	CrossClientPasteRow& row = analysis.rows[rowIndex];
	if (!isCompatibleDestination(row.source, destinationId)) {
		error = "The selected destination ID does not exist or is not structurally compatible with the source item.";
		return false;
	}
	const ItemType& destination = g_items[destinationId];
	row.destinationId = destinationId;
	row.destinationClientId = destination.clientID;
	row.destinationName = destination.name;
	row.state = CrossClientMatchState::Remapped;
	row.detail = wxString::Format("Source map item ID %u was manually mapped to destination ID %u.", row.source.sourceId, destinationId);
	Recount(analysis);
	return true;
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
		if (!isCompatibleDestination(row.source, row.destinationId)) {
			error = wxString::Format("Destination item ID %u is missing or incompatible. Nothing was changed.", row.destinationId);
			return false;
		}
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
