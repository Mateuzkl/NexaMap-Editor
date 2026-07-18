#ifndef RME_PROCEDURAL_MAP_GENERATOR_WINDOW_H_
#define RME_PROCEDURAL_MAP_GENERATOR_WINDOW_H_

#include "procedural_map_generator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include <wx/dialog.h>

class Brush;
class Editor;
class GroundBrush;
class DoodadBrush;
class WallBrush;
class wxButton;
class wxCheckBox;
class wxCloseEvent;
class wxChoice;
class wxGauge;
class wxNotebook;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTextCtrl;
class wxTimer;
class wxTimerEvent;

class ProceduralPreviewPanel;

class ProceduralGeneratorDialog final : public wxDialog {
public:
	ProceduralGeneratorDialog(wxWindow* parent, Editor& editor, int currentFloor);
	~ProceduralGeneratorDialog() override;

private:
	void BuildLayout();
	void BuildPresetPage(wxNotebook* notebook);
	void BuildAreaPage(wxNotebook* notebook);
	void BuildTerrainPage(wxNotebook* notebook);
	void BuildStructuresPage(wxNotebook* notebook);
	void BuildMaterialsPage(wxNotebook* notebook);
	void BuildPreviewPage(wxNotebook* notebook);
	void BuildValidationPage(wxNotebook* notebook);
	void PrepareBrushCatalog();
	void PopulateBrushChoice(wxChoice* choice, const std::vector<Brush*>& brushes, const std::vector<wxString>& names);
	void FinishBrushInitialization();
	void SelectRecommendedBrushes();
	void UpdateAreaSummary();
	void UpdatePresetDefaults(bool force);
	void UpdatePreviewFloorChoice();
	void UpdateValidationReport();
	void SetGeneratingState(bool generating);

	ProceduralMap::GenerationRequest ReadRequest(wxString& error) const;
	void BuildAllowedMask(ProceduralMap::GenerationRequest& request) const;
	void BuildProtectedMask(ProceduralMap::GenerationRequest& request) const;
	void StartGeneration(bool randomizeSeed);
	void StopWorker();
	void OnBrushTimer(wxTimerEvent& event);
	void OnWorkerTimer(wxTimerEvent& event);
	void OnGenerate(wxCommandEvent& event);
	void OnRegenerate(wxCommandEvent& event);
	void OnRandomize(wxCommandEvent& event);
	void OnInterpretBrief(wxCommandEvent& event);
	void OnPresetChanged(wxCommandEvent& event);
	void OnAreaChanged(wxCommandEvent& event);
	void OnPreviewFloorChanged(wxCommandEvent& event);
	void OnSavePreset(wxCommandEvent& event);
	void OnLoadPreset(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	bool ValidateMaterials(std::vector<ProceduralMap::ValidationIssue>& issues) const;
	bool ConfirmDestructiveApply() const;
	bool ApplyPlan(wxString& error);
	Brush* SelectedBrush(const wxChoice* choice) const;
	GroundBrush* SelectedGround(ProceduralMap::MaterialRole role) const;
	WallBrush* SelectedWall() const;
	DoodadBrush* SelectedDoodad(const wxChoice* choice) const;

	Editor& editor;
	int launchFloor = 7;
	bool capturedSelection = false;
	int selectionMinX = 0;
	int selectionMinY = 0;
	int selectionMinZ = 7;
	int selectionMaxX = 0;
	int selectionMaxY = 0;
	int selectionMaxZ = 7;
	std::vector<std::tuple<int, int, int>> selectedPositions;

	wxNotebook* notebook = nullptr;
	wxChoice* presetChoice = nullptr;
	wxTextCtrl* seedText = nullptr;
	wxTextCtrl* briefText = nullptr;
	wxStaticText* interpretedText = nullptr;
	wxSpinCtrl* startX = nullptr;
	wxSpinCtrl* startY = nullptr;
	wxSpinCtrl* startZ = nullptr;
	wxSpinCtrl* endX = nullptr;
	wxSpinCtrl* endY = nullptr;
	wxSpinCtrl* endZ = nullptr;
	wxStaticText* areaSummary = nullptr;
	wxChoice* replacementChoice = nullptr;
	wxChoice* edgeChoice = nullptr;
	wxSpinCtrl* edgeMargin = nullptr;
	wxSpinCtrlDouble* noiseScale = nullptr;
	wxSpinCtrl* octaves = nullptr;
	wxSpinCtrlDouble* persistence = nullptr;
	wxSpinCtrlDouble* lacunarity = nullptr;
	wxSpinCtrlDouble* waterLevel = nullptr;
	wxSpinCtrlDouble* irregularity = nullptr;
	wxSpinCtrl* smoothing = nullptr;
	wxSpinCtrl* decorationDensity = nullptr;
	wxSpinCtrl* pathWidth = nullptr;
	wxSpinCtrl* roomCount = nullptr;
	wxSpinCtrl* roomMinSize = nullptr;
	wxSpinCtrl* roomMaxSize = nullptr;
	wxSpinCtrl* loops = nullptr;
	wxSpinCtrl* roadSpacing = nullptr;
	wxCheckBox* autoFixConnectivity = nullptr;
	wxCheckBox* lockTerrain = nullptr;
	wxCheckBox* lockStructures = nullptr;
	wxCheckBox* lockDecorations = nullptr;
	std::array<wxChoice*, static_cast<size_t>(ProceduralMap::MaterialRole::Count)> groundChoices {};
	wxChoice* wallChoice = nullptr;
	wxChoice* doodadChoice = nullptr;
	wxChoice* transitionChoice = nullptr;
	ProceduralPreviewPanel* previewPanel = nullptr;
	wxChoice* previewFloorChoice = nullptr;
	wxTextCtrl* validationText = nullptr;
	wxGauge* progressGauge = nullptr;
	wxStaticText* statusText = nullptr;
	wxButton* generateButton = nullptr;
	wxButton* regenerateButton = nullptr;
	wxButton* applyButton = nullptr;
	wxButton* savePresetButton = nullptr;
	wxButton* loadPresetButton = nullptr;

	std::vector<Brush*> groundBrushes;
	std::vector<Brush*> wallBrushes;
	std::vector<Brush*> doodadBrushes;
	std::vector<wxString> groundBrushNames;
	std::vector<wxString> wallBrushNames;
	std::vector<wxString> doodadBrushNames;
	std::optional<ProceduralMap::GenerationPlan> plan;
	std::optional<ProceduralMap::GenerationPlan> pendingPlan;
	std::string workerError;
	std::mutex workerMutex;
	std::jthread worker;
	std::unique_ptr<wxTimer> workerTimer;
	std::unique_ptr<wxTimer> brushTimer;
	std::atomic<bool> workerDone { false };
	std::atomic<int> workerProgress { 0 };
	std::atomic<ProceduralMap::GenerationStage> workerStage { ProceduralMap::GenerationStage::ReadingSelection };
	size_t brushChoiceLoadIndex = 0;
	bool brushCatalogPrepared = false;
	bool brushesReady = false;
	bool closing = false;
};

bool RunProceduralMapGenerator(wxWindow* parent, Editor& editor, int currentFloor);

#endif
