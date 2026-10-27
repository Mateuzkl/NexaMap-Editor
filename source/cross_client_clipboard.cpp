#include "main.h"

#include "cross_client_clipboard.h"
#include "cross_client_match_policy.h"

#include "basemap.h"
#include "complexitem.h"
#include "copybuffer.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "items.h"
#include "workspace_session.h"

#include <algorithm>
#include <bit>
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
		bool available = false;
		SpriteVisualFingerprint visual;
	};

	// Keep enough source detail for the review dialog's enlarged hover preview.
	// Similarity checks still sample a fixed 16x16 grid, so this does not make
	// candidate comparison more expensive.
	constexpr int StoredPreviewExtent = 64;
	constexpr size_t MaximumRetainedPreviewBytes = 64u * 1024u * 1024u;
	constexpr uint64_t SemanticPropertyMask = (uint64_t(0x3FFF) << 0) | (uint64_t(0xFF) << 14) | (uint64_t(0x7) << 24) | (uint64_t(0x7FFF) << 27);

	size_t CrossClientMaximumItemId() {
		const size_t storedMaximum = g_items.items.size() > 0 ? g_items.items.size() - 1 : 0;
		return std::min<size_t>(std::max<size_t>(g_items.getMaxID(), storedMaximum), std::numeric_limits<uint16_t>::max());
	}

	bool LoadStoredPreview(GameSprite* sprite, std::vector<uint8_t>& pixels, int& width, int& height, bool& pending) {
		if (!sprite || !sprite->getVisualPreviewRGBA(pixels, width, height, pending, false)) {
			return false;
		}
		if (width <= StoredPreviewExtent && height <= StoredPreviewExtent) {
			return true;
		}

		int storedWidth = StoredPreviewExtent;
		int storedHeight = StoredPreviewExtent;
		if (width > height) {
			storedHeight = std::max(1, height * StoredPreviewExtent / width);
		} else if (height > width) {
			storedWidth = std::max(1, width * StoredPreviewExtent / height);
		}
		std::vector<uint8_t> stored(static_cast<size_t>(storedWidth) * storedHeight * 4);
		for (int y = 0; y < storedHeight; ++y) {
			const int sourceY = std::min(height - 1, y * height / storedHeight);
			for (int x = 0; x < storedWidth; ++x) {
				const int sourceX = std::min(width - 1, x * width / storedWidth);
				const size_t source = (static_cast<size_t>(sourceY) * width + sourceX) * 4;
				const size_t destination = (static_cast<size_t>(y) * storedWidth + x) * 4;
				std::copy_n(pixels.data() + source, 4, stored.data() + destination);
			}
		}
		pixels = std::move(stored);
		width = storedWidth;
		height = storedHeight;
		return true;
	}

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
		add(item.isWall, 27);
		add(item.isBorder, 28);
		add(item.isOptionalBorder, 29);
		add(item.isBrushDoor, 30);
		add(item.isOpen, 31);
		add(item.isLocked, 32);
		add(item.isTable, 33);
		add(item.isCarpet, 34);
		add(item.floorChange, 35);
		add(item.floorChangeDown, 36);
		add(item.floorChangeNorth, 37);
		add(item.floorChangeSouth, 38);
		add(item.floorChangeEast, 39);
		add(item.floorChangeWest, 40);
		add(item.blockPickupable, 41);
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

	bool StructurallyCompatible(const CrossClientItemSnapshot& source, const TargetCandidate& target) {
		return CrossClientMatchPolicy::StructurallyCompatible(
			source.group, source.type, source.semanticFlags,
			target.group, target.type, target.semanticFlags
		);
	}

	bool SameName(const std::string& source, const std::string& target) {
		return !source.empty() && !target.empty()
			&& wxString::FromUTF8(source).CmpNoCase(wxString::FromUTF8(target)) == 0;
	}

	int PropertySimilarity(uint64_t source, uint64_t target) {
		const unsigned int propertyCount = std::popcount(SemanticPropertyMask);
		const unsigned int differences = std::popcount((source ^ target) & SemanticPropertyMask);
		return propertyCount == 0 ? 100 : static_cast<int>((propertyCount - differences) * 100 / propertyCount);
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

	void AddRecommendation(
		std::vector<CrossClientItemRecommendation>& recommendations,
		const CrossClientItemSnapshot& source,
		const TargetCandidate& target,
		double visualSimilarity,
		bool visualCompared
	) {
		const bool sameName = SameName(source.name, target.name);
		const int propertySimilarity = PropertySimilarity(source.semanticFlags, target.semanticFlags);
		// A shared name or incomplete XML metadata must never be enough for auto-mapping.
		if (!visualCompared || visualSimilarity < 0.50) {
			return;
		}
		// A sampled preview is a review aid, never proof of full equivalence.
		CrossClientItemRecommendation recommendation;
		recommendation.destinationId = target.id;
		recommendation.destinationClientId = target.clientId;
		recommendation.destinationName = target.name;
		const int visualScore = visualCompared ? static_cast<int>(visualSimilarity * 65.0 + 0.5) : 0;
		const int propertyWeight = visualCompared ? 25 : 75;
		const int nameScore = sameName ? (visualCompared ? 10 : 25) : 0;
		const int confidence = visualScore + propertySimilarity * propertyWeight / 100 + nameScore;
		recommendation.confidence = static_cast<uint8_t>(std::clamp(confidence, 0, 100));
		recommendation.automatic = false;
		recommendation.visualCompared = visualCompared;
		recommendation.visualSimilarity = visualSimilarity;
		recommendations.push_back(std::move(recommendation));
		std::sort(recommendations.begin(), recommendations.end(), [](const CrossClientItemRecommendation& left, const CrossClientItemRecommendation& right) {
			if (left.visualSimilarity != right.visualSimilarity) {
				return left.visualSimilarity > right.visualSimilarity;
			}
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

	SpriteVisualFingerprint PreviewFingerprint(const std::vector<uint8_t>& pixels, int width, int height) {
		SpriteVisualFingerprint result { 14695981039346656037ull, 0x9E3779B97F4A7C15ull, pixels.size() };
		const auto add = [&](uint8_t byte) {
			result.primary = (result.primary ^ byte) * 1099511628211ull;
			result.secondary ^= uint64_t(byte) + 0x9E3779B97F4A7C15ull + (result.secondary << 6) + (result.secondary >> 2);
		};
		for (int shift = 0; shift < 32; shift += 8) {
			add(static_cast<uint8_t>(width >> shift));
			add(static_cast<uint8_t>(height >> shift));
		}
		for (uint8_t byte : pixels) {
			add(byte);
		}
		return result;
	}
}

CrossClientClipboard::CrossClientClipboard() = default;
CrossClientClipboard::~CrossClientClipboard() = default;

bool CrossClientClipboard::capture(CopyBuffer& source, const std::shared_ptr<EditorResourceSession>& session, wxString& error) {
	error.clear();
	clear();
	if (!session || session != GetActiveEditorResourceSession() || !source.canPaste()) {
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
	size_t retainedPreviewBytes = 0;
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
				snapshot.renderedVisualAvailable = sprite->getRenderedFingerprint(snapshot.renderedVisual, pending, false);
				pending = false;
				snapshot.previewAvailable = LoadStoredPreview(sprite, snapshot.previewRgba, snapshot.previewWidth, snapshot.previewHeight, pending);
				if (snapshot.previewAvailable && retainedPreviewBytes <= MaximumRetainedPreviewBytes
					&& snapshot.previewRgba.size() <= MaximumRetainedPreviewBytes - retainedPreviewBytes) {
					retainedPreviewBytes += snapshot.previewRgba.size();
					snapshot.previewVisual = PreviewFingerprint(snapshot.previewRgba, snapshot.previewWidth, snapshot.previewHeight);
				} else {
					snapshot.previewAvailable = false;
					snapshot.previewRgba.clear();
					snapshot.previewWidth = 0;
					snapshot.previewHeight = 0;
				}
			}
		}
		capturedItems.push_back(std::move(snapshot));
	}

	map = std::move(capturedMap);
	copyPosition = source.getPosition();
	spawnDependencies = source.getSpawnDependencies();
	houses = source.houses;
	copiedMapSession = source.sourceMapSessionId;
	sourceGraphicsIdentity = g_gui.gfx.getResourceIdentity();
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
	spawnDependencies.clear();
	houses.clear();
	copiedMapSession = InvalidSessionId;
	sourceGraphicsIdentity = InvalidSessionId;
	if (++generation == 0) {
		++generation;
	}
}

bool CrossClientClipboard::canPaste() const noexcept {
	return map && map->size() != 0;
}

bool CrossClientClipboard::isFromSession(const std::shared_ptr<EditorResourceSession>& session) const noexcept {
	return canPaste() && session && sourceSession.lock() == session && sourceGraphicsIdentity == g_gui.gfx.getResourceIdentity();
}

CrossClientPasteAnalysis CrossClientClipboard::analyze(
	const std::shared_ptr<EditorResourceSession>& destinationSession,
	const ProgressCallback& progress,
	wxString& error
) const {
	CrossClientPasteAnalysis analysis;
	error.clear();
	if (!canPaste() || !destinationSession || destinationSession != GetActiveEditorResourceSession()) {
		error = "The cross-client clipboard is empty or the destination tab is not active.";
		return analysis;
	}
	analysis.clipboardGeneration = generation;
	analysis.destinationGraphicsIdentity = g_gui.gfx.getResourceIdentity();
	analysis.destinationWorkspaceGeneration = g_workspace.getGeneration();
	analysis.sourceSession = sourceSession;
	analysis.destinationSession = destinationSession;
	analysis.sourceClient = sourceClient;
	analysis.sourceServer = sourceServer;
	const auto& targetClient = g_workspace.getClient();
	analysis.destinationClient = targetClient.rootPath.empty() ? wxString("Not configured") : targetClient.rootPath;
	analysis.destinationServer = DisplayPath(g_workspace.getServer().rootPath);
	const auto update = [&](size_t current, size_t total) {
		if (progress && !progress(current, total)) {
			error = "Cross-client comparison was cancelled.";
			return false;
		}
		// Progress callbacks pump events: never use cached resources after a tab/reload/copy.
		if (analysis.clipboardGeneration != generation || destinationSession != GetActiveEditorResourceSession()
			|| analysis.destinationGraphicsIdentity != g_gui.gfx.getResourceIdentity()
			|| analysis.destinationWorkspaceGeneration != g_workspace.getGeneration()) {
			error = "The clipboard or destination resources changed during comparison. Paste again.";
			return false;
		}
		return true;
	};
	const size_t maximumId = CrossClientMaximumItemId();
	const size_t total = maximumId * 2;
	if (!update(0, total)) {
		return {};
	}
	std::unordered_map<SpriteVisualFingerprint, std::vector<TargetCandidate>, FingerprintHash> candidates;
	std::unordered_map<uint16_t, TargetSpriteCache> spriteCache;
	const bool needsFingerprints = std::any_of(items.begin(), items.end(), [](const auto& source) { return source.renderedVisualAvailable; });
	const bool needsPreviews = std::any_of(items.begin(), items.end(), [](const auto& source) { return source.previewAvailable; });
	std::unordered_set<SpriteVisualFingerprint, FingerprintHash> sourcePreviews;
	bool needsUnfilteredFingerprints = false;
	for (const auto& source : items) {
		if (source.previewAvailable) {
			sourcePreviews.insert(source.previewVisual);
		} else if (source.renderedVisualAvailable) {
			needsUnfilteredFingerprints = true;
		}
	}
	for (size_t id = 1; id <= maximumId; ++id) {
		if (id % 32 == 0 && !update(id, total)) {
			return {};
		}
		if (!needsFingerprints || !g_items.typeExists(static_cast<int>(id))) {
			continue;
		}
		const ItemType& item = g_items[id];
		if (!item.clientID) {
			continue;
		}
		auto [cached, inserted] = spriteCache.try_emplace(item.clientID);
		if (inserted) {
			GameSprite* sprite = item.sprite ? item.sprite : dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			bool pending = false;
			std::vector<uint8_t> preview;
			int width = 0, height = 0;
			// This only rejects candidates. It can never accept a sampled preview as a match.
			const bool previewMatches = needsUnfilteredFingerprints
				|| (LoadStoredPreview(sprite, preview, width, height, pending)
					&& sourcePreviews.contains(PreviewFingerprint(preview, width, height)));
			cached->second.available = previewMatches && sprite && sprite->getRenderedFingerprint(cached->second.visual, pending, false);
		}
		if (cached->second.available) {
			candidates[cached->second.visual].push_back({ static_cast<uint16_t>(id), item.clientID,
														  static_cast<uint16_t>(item.group), static_cast<uint16_t>(item.type), SemanticFlags(item), item.name });
		}
	}
	analysis.rows.reserve(items.size());
	for (const auto& source : items) {
		CrossClientPasteRow row;
		row.source = source;
		analysis.totalOccurrences += source.occurrences;
		std::vector<const TargetCandidate*> compatible;
		if (source.renderedVisualAvailable) {
			const auto found = candidates.find(source.renderedVisual);
			if (found != candidates.end()) {
				for (const auto& candidate : found->second) {
					if (StructurallyCompatible(source, candidate)) {
						compatible.push_back(&candidate);
					}
				}
			}
		}
		if (compatible.size() == 1) {
			const auto& best = *compatible.front();
			row.destinationId = best.id;
			row.destinationClientId = best.clientId;
			row.destinationName = best.name;
			row.state = source.sourceId == best.id ? CrossClientMatchState::Matched : CrossClientMatchState::Remapped;
			row.detail = "All animation frames and patterns match a unique compatible destination item.";
		} else if (compatible.size() > 1) {
			row.detail = "Several destination IDs have identical graphics and compatible roles. Choose the intended ID.";
			for (const auto* candidate : compatible) {
				// Identical visuals are still ambiguous: names and numeric IDs cannot break the tie.
				row.recommendations.push_back({ candidate->id, candidate->clientId, candidate->name, 100, false, true, 1.0, true });
			}
		} else {
			row.detail = source.renderedVisualAvailable
				? "No compatible item matches every frame and pattern. Suggestions require review."
				: "The complete source graphics could not be verified. Choose a destination manually.";
		}
		analysis.rows.push_back(std::move(row));
	}
	const bool missing = std::any_of(analysis.rows.begin(), analysis.rows.end(), [](const auto& row) { return row.state == CrossClientMatchState::Missing && row.recommendations.empty(); });
	if (missing && needsPreviews) {
		// One destination preview at a time: memory is independent of the destination item count.
		for (size_t id = 1; id <= maximumId; ++id) {
			if (id % 32 == 0 && !update(maximumId + id, total)) {
				return {};
			}
			if (!g_items.typeExists(static_cast<int>(id))) {
				continue;
			}
			const ItemType& item = g_items[id];
			if (!item.clientID) {
				continue;
			}
			const TargetCandidate candidate { static_cast<uint16_t>(id), item.clientID,
											  static_cast<uint16_t>(item.group), static_cast<uint16_t>(item.type), SemanticFlags(item), item.name };
			GameSprite* sprite = item.sprite ? item.sprite : dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(item.clientID));
			bool pending = false;
			std::vector<uint8_t> pixels;
			int width = 0, height = 0;
			if (!LoadStoredPreview(sprite, pixels, width, height, pending)) {
				continue;
			}
			for (auto& row : analysis.rows) {
				// Don't mix approximate scores into an exact-but-ambiguous equivalence list.
				if (row.state != CrossClientMatchState::Missing || !row.source.previewAvailable
					|| (!row.recommendations.empty() && row.recommendations.front().allVariantsVerified)
					|| !StructurallyCompatible(row.source, candidate)) {
					continue;
				}
				const double similarity = PreviewSimilarity(row.source.previewRgba, row.source.previewWidth, row.source.previewHeight, pixels, width, height);
				AddRecommendation(row.recommendations, row.source, candidate, similarity, true);
			}
		}
	}
	if (!update(total, total)) {
		return {};
	}
	Recount(analysis);
	analysis.valid = true;
	return analysis;
}

bool CrossClientClipboard::hasSafeAutomaticMapping(const CrossClientPasteRow& row) {
	if (row.state != CrossClientMatchState::Missing || row.recommendations.empty()) {
		return false;
	}
	const auto& best = row.recommendations.front();
	int competingConfidence = -1;
	double competingVisual = -1.0;
	for (size_t i = 1; i < row.recommendations.size(); ++i) {
		competingConfidence = std::max(competingConfidence, int(row.recommendations[i].confidence));
		competingVisual = std::max(competingVisual, row.recommendations[i].visualSimilarity);
	}
	return CrossClientMatchPolicy::CanApplySuggestion(best.confidence, best.automatic, best.visualCompared, best.visualSimilarity, competingConfidence, competingVisual, best.allVariantsVerified);
}
bool CrossClientClipboard::isCompatibleDestination(const CrossClientItemSnapshot& source, uint16_t destinationId) {
	if (destinationId == 0 || !g_items.typeExists(destinationId)) {
		return false;
	}
	const ItemType& item = g_items[destinationId];
	return CrossClientMatchPolicy::StructurallyCompatible(
		source.group, source.type, source.semanticFlags,
		static_cast<uint16_t>(item.group), static_cast<uint16_t>(item.type), SemanticFlags(item)
	);
}

bool CrossClientClipboard::resolveMapping(CrossClientPasteAnalysis& analysis, size_t rowIndex, uint16_t destinationId, wxString& error) {
	error.clear();
	if (!analysis.valid || rowIndex >= analysis.rows.size() || analysis.destinationSession.lock() != GetActiveEditorResourceSession() || analysis.destinationGraphicsIdentity != g_gui.gfx.getResourceIdentity() || analysis.destinationWorkspaceGeneration != g_workspace.getGeneration()) {
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
	if (!canPaste() || analysis.clipboardGeneration != generation) {
		error = "The copied area changed while the cross-client verification window was open.";
		return false;
	}
	if (analysis.destinationSession.expired() || analysis.destinationSession.lock() != GetActiveEditorResourceSession() || analysis.destinationGraphicsIdentity != g_gui.gfx.getResourceIdentity() || analysis.destinationWorkspaceGeneration != g_workspace.getGeneration()) {
		error = "The destination resource session changed before the paste was applied.";
		return false;
	}
	if (!analysis.canApply()) {
		error = "The copied area contains missing resources. No map or client file was changed.";
		return false;
	}

	std::unordered_map<uint16_t, uint16_t> remap;
	if (analysis.rows.size() != items.size()) {
		error = "The mapping does not cover the complete copied area. Paste again.";
		return false;
	}
	for (const CrossClientPasteRow& row : analysis.rows) {
		if (!isCompatibleDestination(row.source, row.destinationId)) {
			error = wxString::Format("Destination item ID %u is missing or incompatible. Nothing was changed.", row.destinationId);
			return false;
		}
		remap[row.source.sourceId] = row.destinationId;
	}
	for (const auto& source : items) {
		if (!remap.contains(source.sourceId)) {
			error = "The mapping does not cover every copied item. Nothing was changed.";
			return false;
		}
	}
	std::vector<std::pair<Item*, uint16_t>> changedItems;
	struct RestoreItemIds {
		std::vector<std::pair<Item*, uint16_t>>& items;
		~RestoreItemIds() {
			for (auto iterator = items.rbegin(); iterator != items.rend(); ++iterator) {
				iterator->first->setID(iterator->second);
			}
		}
	} restore { changedItems };
	VisitMapItems(*map, [&](Item* item) {
		const auto mapping = remap.find(item->getID());
		if (mapping != remap.end()) {
			changedItems.emplace_back(item, item->getID());
			item->setID(mapping->second);
		}
	});

	std::unique_ptr<BaseMap> converted = CloneMap(*map);
	if (!converted || converted->size() == 0) {
		error = "The converted paste buffer is empty.";
		return false;
	}
	CopyBuffer prepared;
	prepared.replace(std::move(converted), copyPosition, spawnDependencies);
	prepared.houses = houses;
	prepared.sourceMapSessionId = copiedMapSession;
	destination.swap(prepared);
	return true;
}
