#ifndef NEXAMAP_EDITOR_RESOURCE_SESSION_H_
#define NEXAMAP_EDITOR_RESOURCE_SESSION_H_

#include "client_assets.h"
#include "client_version.h"

#include <cstdint>
#include <memory>
#include <string>

class Brush;
class BaseMap;
class DoorBrush;
class EraserBrush;
class FlagBrush;
class HouseBrush;
class HouseExitBrush;
class OptionalBorderBrush;
class SpawnBrush;
class WaypointBrush;
class ZoneBrush;

class EditorResourceSession final {
public:
	EditorResourceSession();
	~EditorResourceSession();

	EditorResourceSession(const EditorResourceSession&) = delete;
	EditorResourceSession& operator=(const EditorResourceSession&) = delete;

	void swapWithGlobals();
	const std::string& getFavoritesContext() const {
		return favoritesContext;
	}

private:
	struct Storage;
	std::unique_ptr<Storage> storage;
	std::string favoritesContext;

	ClientVersionID loadedVersion = CLIENT_VERSION_NONE;
	bool canaryCrystalAssetsLoaded = false;
	uint64_t loadedWorkspaceGeneration = 0;
	ClientAssets::State clientAssetsState;

	Brush* currentBrush = nullptr;
	Brush* previousBrush = nullptr;
	HouseBrush* houseBrush = nullptr;
	HouseExitBrush* houseExitBrush = nullptr;
	WaypointBrush* waypointBrush = nullptr;
	OptionalBorderBrush* optionalBrush = nullptr;
	EraserBrush* eraser = nullptr;
	SpawnBrush* spawnBrush = nullptr;
	DoorBrush* normalDoorBrush = nullptr;
	DoorBrush* lockedDoorBrush = nullptr;
	DoorBrush* magicDoorBrush = nullptr;
	DoorBrush* questDoorBrush = nullptr;
	DoorBrush* hatchDoorBrush = nullptr;
	DoorBrush* normalDoorAltBrush = nullptr;
	DoorBrush* archwayDoorBrush = nullptr;
	DoorBrush* windowDoorBrush = nullptr;
	FlagBrush* pzBrush = nullptr;
	FlagBrush* rookBrush = nullptr;
	FlagBrush* nologBrush = nullptr;
	FlagBrush* pvpBrush = nullptr;
	ZoneBrush* zoneBrush = nullptr;
	std::unique_ptr<BaseMap> doodadBufferMap;

	friend class GUI;
};

using EditorResourceSessionPtr = std::shared_ptr<EditorResourceSession>;

EditorResourceSessionPtr GetActiveEditorResourceSession();
EditorResourceSessionPtr CreateEditorResourceSession();
void SetActiveEditorResourceSession(const EditorResourceSessionPtr& session);

#endif // NEXAMAP_EDITOR_RESOURCE_SESSION_H_
