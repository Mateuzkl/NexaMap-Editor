#include "main.h"

#include "procedural_map_generator_window.h"

#include "action.h"
#include "brush.h"
#include "common.h"
#include "complexitem.h"
#include "doodad_brush.h"
#include "editor.h"
#include "graphics.h"
#include "ground_brush.h"
#include "gui.h"
#include "items.h"
#include "map.h"
#include "tile.h"
#include "wall_brush.h"

#include <array>
#include <chrono>
#include <fstream>
#include <random>
#include <set>
#include <unordered_set>

#include <wx/arrstr.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/timer.h>

namespace {

constexpr int kWorkerTimerIntervalMs = 75;

wxSpinCtrl* AddSpin(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, int value, int minimum, int maximum, const wxString& help = {}) {
	grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	auto* control = new wxSpinCtrl(parent, wxID_ANY);
	control->SetRange(minimum, maximum);
	control->SetValue(value);
	if (!help.empty()) control->SetToolTip(help);
	grid->Add(control, 1, wxEXPAND);
	return control;
}

wxSpinCtrlDouble* AddDoubleSpin(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, double value, double minimum, double maximum, double increment) {
	grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	auto* control = new wxSpinCtrlDouble(parent, wxID_ANY);
	control->SetRange(minimum, maximum);
	control->SetIncrement(increment);
	control->SetDigits(2);
	control->SetValue(value);
	grid->Add(control, 1, wxEXPAND);
	return control;
}

void SelectByKeywords(wxChoice* choice, const std::vector<wxString>& keywords) {
	if (!choice || choice->GetCount() <= 1) return;
	for (const auto& keyword : keywords) {
		const wxString wanted = keyword.Lower();
		for (unsigned int index = 1; index < choice->GetCount(); ++index) {
			if (choice->GetString(index).Lower() == wanted) {
				choice->SetSelection(static_cast<int>(index));
				return;
			}
		}
		for (unsigned int index = 1; index < choice->GetCount(); ++index) {
			const wxString name = choice->GetString(index).Lower();
			if (name.StartsWith(wanted + " ") || name.StartsWith(wanted + " (") || name.StartsWith(wanted + "-")) {
				choice->SetSelection(static_cast<int>(index));
				return;
			}
		}
		for (unsigned int index = 1; index < choice->GetCount(); ++index) {
			if (choice->GetString(index).Lower().Find(wanted) != wxNOT_FOUND) {
				choice->SetSelection(static_cast<int>(index));
				return;
			}
		}
	}
	// An arbitrary fallback could silently mix an unrelated material into the
	// profile. Keep it unresolved and require an explicit active-client choice.
	choice->SetSelection(0);
}

uint64_t MixApplyHash(uint64_t value) noexcept {
	value += 0x9E3779B97F4A7C15ULL;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
	return value ^ (value >> 31);
}

uint64_t PositionKey(int x, int y, int z) noexcept {
	return (static_cast<uint64_t>(static_cast<uint16_t>(x)) << 24) |
		(static_cast<uint64_t>(static_cast<uint16_t>(y)) << 8) |
		static_cast<uint8_t>(z);
}

bool RequestsEquivalent(const ProceduralMap::GenerationRequest& lhs, const ProceduralMap::GenerationRequest& rhs) {
	const auto& a = lhs.parameters;
	const auto& b = rhs.parameters;
	return lhs.minX == rhs.minX && lhs.minY == rhs.minY && lhs.minZ == rhs.minZ && lhs.maxX == rhs.maxX && lhs.maxY == rhs.maxY && lhs.maxZ == rhs.maxZ &&
		lhs.allowedCells == rhs.allowedCells && lhs.protectedCells == rhs.protectedCells && lhs.seed == rhs.seed && lhs.preset == rhs.preset && lhs.replacementMode == rhs.replacementMode && lhs.edgeBlending == rhs.edgeBlending &&
		a.noiseScale == b.noiseScale && a.octaves == b.octaves && a.persistence == b.persistence && a.lacunarity == b.lacunarity && a.waterLevel == b.waterLevel &&
		a.irregularity == b.irregularity && a.smoothingPasses == b.smoothingPasses && a.decorationDensity == b.decorationDensity && a.pathWidth == b.pathWidth &&
		a.roomCount == b.roomCount && a.roomMinSize == b.roomMinSize && a.roomMaxSize == b.roomMaxSize && a.loops == b.loops &&
		a.secondaryRoadSpacing == b.secondaryRoadSpacing && a.edgeMargin == b.edgeMargin && a.autoFixConnectivity == b.autoFixConnectivity;
}

struct ProtectionSummary {
	size_t tiles = 0;
	size_t houses = 0;
	size_t doors = 0;
	size_t teleports = 0;
	size_t depots = 0;
	size_t spawns = 0;
	size_t creatures = 0;
	size_t waypointsOrTowns = 0;
	size_t uniqueOrActionItems = 0;
	size_t customizedItems = 0;
	size_t containers = 0;
	size_t zonesOrFlags = 0;
};

void InspectProtectedItem(const Item* item, bool& protectedTile, ProtectionSummary* summary) {
	if (!item) return;
	if (dynamic_cast<const Teleport*>(item)) {
		protectedTile = true;
		if (summary) ++summary->teleports;
	}
	if (dynamic_cast<const Depot*>(item)) {
		protectedTile = true;
		if (summary) ++summary->depots;
	}
	if (item->isDoor()) {
		protectedTile = true;
		if (summary) ++summary->doors;
	}
	if (item->getUniqueID() != 0 || item->getActionID() != 0) {
		protectedTile = true;
		if (summary) ++summary->uniqueOrActionItems;
	}
	if (item->isComplex()) {
		protectedTile = true;
		if (summary) ++summary->customizedItems;
	}
	if (const auto* container = dynamic_cast<const Container*>(item); container && container->getItemCount() != 0) {
		protectedTile = true;
		if (summary) ++summary->containers;
	}
}

bool IsProtectedTile(const Tile* tile, ProtectionSummary* summary = nullptr) {
	if (!tile) return false;
	bool protectedTile = false;
	if (tile->isHouseTile() || tile->isHouseExit()) {
		protectedTile = true;
		if (summary) ++summary->houses;
	}
	if (tile->spawn || (tile->getLocation() && tile->getLocation()->getSpawnCount() != 0)) {
		protectedTile = true;
		if (summary) ++summary->spawns;
	}
	if (tile->creature) {
		protectedTile = true;
		if (summary) ++summary->creatures;
	}
	if (tile->getLocation() && (tile->getLocation()->getWaypointCount() != 0 || tile->getLocation()->getTownCount() != 0)) {
		protectedTile = true;
		if (summary) ++summary->waypointsOrTowns;
	}
	if (tile->getMapFlags() != 0 || tile->hasZone()) {
		protectedTile = true;
		if (summary) ++summary->zonesOrFlags;
	}
	InspectProtectedItem(tile->ground, protectedTile, summary);
	for (const auto* item : tile->items) InspectProtectedItem(item, protectedTile, summary);
	if (protectedTile && summary) ++summary->tiles;
	return protectedTile;
}

bool IsProtectedItem(const Item* item) {
	bool protectedItem = false;
	InspectProtectedItem(item, protectedItem, nullptr);
	return protectedItem;
}

bool UsesSmartLayerReplacement(ProceduralMap::ReplacementMode mode) noexcept {
	return mode == ProceduralMap::ReplacementMode::ReplaceTerrainAndBorders || mode == ProceduralMap::ReplacementMode::BlendWithExisting;
}

bool RequiresClearSurface(const ProceduralMap::GeneratedCell& cell) noexcept {
	constexpr uint16_t clearFeatures = ProceduralMap::FeatureWall | ProceduralMap::FeatureDecoration | ProceduralMap::FeatureTransition | ProceduralMap::FeaturePointOfInterest;
	return !cell.walkable || cell.material == ProceduralMap::MaterialRole::Path || cell.material == ProceduralMap::MaterialRole::Water ||
		cell.material == ProceduralMap::MaterialRole::Rock || (cell.features & clearFeatures) != 0;
}

bool ShouldRemoveExistingItem(const Item* item, const ProceduralMap::GeneratedCell& cell, ProceduralMap::ReplacementMode mode) {
	if (!item || IsProtectedItem(item)) return false;
	if (mode == ProceduralMap::ReplacementMode::ReplaceEverything) return true;
	if (!UsesSmartLayerReplacement(mode)) return false;
	// Borders and walls are generated layers and must never survive a new
	// terrain topology. Other unprotected objects are removed only where the
	// plan needs a clear path, liquid/rock, wall, transition, POI or doodad.
	return item->isBorder() || item->isWall() || item->isBlocking() || RequiresClearSurface(cell);
}

bool IsApplyEligible(const ProceduralMap::GenerationPlan& plan, const ProceduralMap::GeneratedCell& cell, const Tile* tile) {
	if (cell.material == ProceduralMap::MaterialRole::None || IsProtectedTile(tile)) return false;
	if (plan.request.replacementMode == ProceduralMap::ReplacementMode::EmptyTilesOnly && tile && !tile->empty()) return false;
	if (plan.request.replacementMode == ProceduralMap::ReplacementMode::BlendWithExisting) {
		const uint64_t roll = MixApplyHash(plan.request.seed ^ PositionKey(cell.x, cell.y, cell.z)) % 255U;
		if (roll >= cell.blendWeight) return false;
	}
	return true;
}

struct ApplyImpactSummary {
	size_t eligibleTiles = 0;
	size_t skippedProtectedTiles = 0;
	size_t skippedByPolicyTiles = 0;
	size_t groundsReplaced = 0;
	size_t bordersRemoved = 0;
	size_t wallsRemoved = 0;
	size_t objectsRemoved = 0;
	size_t blockingObjectsRemoved = 0;
	size_t objectsPreserved = 0;
};

ApplyImpactSummary ComputeApplyImpact(Editor& editor, const ProceduralMap::GenerationPlan& plan) {
	ApplyImpactSummary impact;
	impact.skippedProtectedTiles = plan.statistics.protectedTiles;
	for (const auto& cell : plan.cells) {
		if (cell.material == ProceduralMap::MaterialRole::None) continue;
		const Tile* tile = editor.map.getTile(cell.x, cell.y, cell.z);
		if (!IsApplyEligible(plan, cell, tile)) {
			if (IsProtectedTile(tile)) ++impact.skippedProtectedTiles;
			else ++impact.skippedByPolicyTiles;
			continue;
		}
		++impact.eligibleTiles;
		if (!tile) continue;
		if (tile->ground) ++impact.groundsReplaced;
		for (const Item* item : tile->items) {
			if (!ShouldRemoveExistingItem(item, cell, plan.request.replacementMode)) {
				++impact.objectsPreserved;
				continue;
			}
			if (item->isBorder()) ++impact.bordersRemoved;
			else if (item->isWall()) ++impact.wallsRemoved;
			else ++impact.objectsRemoved;
			if (item->isBlocking()) ++impact.blockingObjectsRemoved;
		}
	}
	return impact;
}

void RemoveConflictingItems(Tile& tile, const ProceduralMap::GeneratedCell& cell, ProceduralMap::ReplacementMode mode) {
	for (auto iterator = tile.items.begin(); iterator != tile.items.end();) {
		Item* item = *iterator;
		if (ShouldRemoveExistingItem(item, cell, mode)) {
			delete item;
			iterator = tile.items.erase(iterator);
		} else {
			++iterator;
		}
	}
}

wxString JoinWords(const std::vector<std::string>& words) {
	wxString result;
	for (size_t index = 0; index < words.size(); ++index) {
		if (index != 0) result += ", ";
		result += wxstr(words[index]);
	}
	return result;
}

} // namespace

class ProceduralPreviewPanel final : public wxScrolledWindow {
public:
	explicit ProceduralPreviewPanel(wxWindow* parent) : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(640, 420), wxBORDER_SIMPLE | wxHSCROLL | wxVSCROLL) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetScrollRate(16, 16);
		Bind(wxEVT_PAINT, &ProceduralPreviewPanel::OnPaint, this);
		Bind(wxEVT_MOUSEWHEEL, &ProceduralPreviewPanel::OnWheel, this);
	}

	void SetPlan(const ProceduralMap::GenerationPlan* newPlan, const std::array<int, static_cast<size_t>(ProceduralMap::MaterialRole::Count)>& newLookIds, int newFloor) {
		plan = newPlan;
		lookIds = newLookIds;
		floor = newFloor;
		UpdateVirtualSize();
		Refresh();
	}

	void SetFloor(int newFloor) {
		floor = newFloor;
		Refresh();
	}

	void ZoomBy(int delta) {
		tileSize = std::clamp(tileSize + delta, 2, 32);
		UpdateVirtualSize();
		Refresh();
	}

private:
	wxColour RoleColour(ProceduralMap::MaterialRole role) const {
		switch (role) {
			case ProceduralMap::MaterialRole::Base: return wxColour(67, 124, 63);
			case ProceduralMap::MaterialRole::Secondary: return wxColour(176, 146, 88);
			case ProceduralMap::MaterialRole::Water: return wxColour(49, 102, 170);
			case ProceduralMap::MaterialRole::Path: return wxColour(133, 116, 91);
			case ProceduralMap::MaterialRole::Rock: return wxColour(84, 86, 91);
			case ProceduralMap::MaterialRole::Accent: return wxColour(205, 210, 212);
			default: return wxColour(35, 38, 43);
		}
	}

	void UpdateVirtualSize() {
		if (!plan) {
			SetVirtualSize(GetClientSize());
			return;
		}
		SetVirtualSize(std::max(1, plan->request.width() * tileSize), std::max(1, plan->request.height() * tileSize));
	}

	void OnWheel(wxMouseEvent& event) {
		ZoomBy(event.GetWheelRotation() > 0 ? 2 : -2);
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		PrepareDC(dc);
		dc.SetBackground(wxBrush(wxColour(27, 30, 34)));
		dc.Clear();
		if (!plan || plan->request.width() <= 0 || plan->request.height() <= 0 || plan->cells.size() != plan->request.cellCount()) {
			dc.SetTextForeground(wxColour(190, 194, 201));
			dc.DrawText(plan ? "The current settings did not produce a drawable plan. Review Validation." : "Generate a preview to inspect the plan. The map is not modified.", 18, 18);
			return;
		}
		if (floor < plan->request.minZ || floor > plan->request.maxZ) return;
		int viewX = 0;
		int viewY = 0;
		int pixelsPerUnitX = 0;
		int pixelsPerUnitY = 0;
		GetViewStart(&viewX, &viewY);
		GetScrollPixelsPerUnit(&pixelsPerUnitX, &pixelsPerUnitY);
		const wxSize client = GetClientSize();
		const int firstLocalX = std::clamp((viewX * pixelsPerUnitX) / tileSize, 0, plan->request.width() - 1);
		const int firstLocalY = std::clamp((viewY * pixelsPerUnitY) / tileSize, 0, plan->request.height() - 1);
		const int lastLocalX = std::clamp((viewX * pixelsPerUnitX + client.GetWidth()) / tileSize + 1, 0, plan->request.width() - 1);
		const int lastLocalY = std::clamp((viewY * pixelsPerUnitY + client.GetHeight()) / tileSize + 1, 0, plan->request.height() - 1);
		const size_t floorOffset = static_cast<size_t>(floor - plan->request.minZ) * static_cast<size_t>(plan->request.width()) * static_cast<size_t>(plan->request.height());
		for (int localY = firstLocalY; localY <= lastLocalY; ++localY) {
			for (int localX = firstLocalX; localX <= lastLocalX; ++localX) {
				const auto& cell = plan->cells[floorOffset + static_cast<size_t>(localY) * static_cast<size_t>(plan->request.width()) + static_cast<size_t>(localX)];
				const int px = (cell.x - plan->request.minX) * tileSize;
				const int py = (cell.y - plan->request.minY) * tileSize;
				if (plan->request.isProtected(cell.x, cell.y, cell.z)) {
					dc.SetPen(wxPen(wxColour(211, 132, 255), std::max(1, tileSize / 6)));
					dc.SetBrush(wxBrush(wxColour(55, 43, 63)));
					dc.DrawRectangle(px, py, tileSize, tileSize);
					dc.DrawLine(px + 1, py + 1, px + tileSize - 1, py + tileSize - 1);
					dc.DrawLine(px + tileSize - 1, py + 1, px + 1, py + tileSize - 1);
					continue;
				}
				if (cell.material == ProceduralMap::MaterialRole::None) continue;
				bool spriteDrawn = false;
				const int lookId = lookIds[static_cast<size_t>(cell.material)];
				if (tileSize >= 8 && lookId != 0 && !g_gui.gfx.isUnloaded()) {
					if (auto* sprite = g_gui.gfx.getSprite(lookId)) {
						sprite->DrawTo(&dc, tileSize <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, px, py, tileSize, tileSize);
						spriteDrawn = true;
					}
				}
				if (!spriteDrawn) {
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.SetBrush(wxBrush(RoleColour(cell.material)));
					dc.DrawRectangle(px, py, tileSize, tileSize);
				}
				if ((cell.features & ProceduralMap::FeatureWall) != 0) {
					dc.SetPen(wxPen(wxColour(225, 118, 74), std::max(1, tileSize / 6)));
					dc.DrawLine(px, py, px + tileSize, py + tileSize);
				}
				if ((cell.features & ProceduralMap::FeatureEntrance) != 0 || (cell.features & ProceduralMap::FeatureExit) != 0) {
					dc.SetPen(wxPen((cell.features & ProceduralMap::FeatureEntrance) != 0 ? wxColour(84, 220, 124) : wxColour(230, 84, 94), 2));
					dc.SetBrush(*wxTRANSPARENT_BRUSH);
					dc.DrawRectangle(px + 1, py + 1, std::max(1, tileSize - 2), std::max(1, tileSize - 2));
				}
				if ((cell.features & ProceduralMap::FeatureTransition) != 0) {
					dc.SetBrush(wxBrush(wxColour(231, 196, 76)));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawCircle(px + tileSize / 2, py + tileSize / 2, std::max(2, tileSize / 4));
				}
			}
		}
	}

	const ProceduralMap::GenerationPlan* plan = nullptr;
	std::array<int, static_cast<size_t>(ProceduralMap::MaterialRole::Count)> lookIds {};
	int floor = 7;
	int tileSize = 12;
};

ProceduralGeneratorDialog::ProceduralGeneratorDialog(wxWindow* parent, Editor& editor, int currentFloor) :
	wxDialog(parent, wxID_ANY, "Procedural Map Generator", wxDefaultPosition, wxSize(1120, 780), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor), launchFloor(std::clamp(currentFloor, 0, 15)), workerTimer(std::make_unique<wxTimer>(this)), brushTimer(std::make_unique<wxTimer>(this)) {
	if (editor.selection.size() > ProceduralMap::MaximumPlanCells) {
		throw std::length_error("The selected area exceeds the procedural generator memory safety limit.");
	}
	selectedPositions.reserve(editor.selection.size());
	for (Tile* tile : editor.selection.getTiles()) {
		if (!tile) continue;
		const Position position = tile->getPosition();
		selectedPositions.emplace_back(position.x, position.y, position.z);
	}
	capturedSelection = !selectedPositions.empty();
	if (capturedSelection) {
		selectionMinX = selectionMinY = 65535;
		selectionMinZ = 15;
		selectionMaxX = selectionMaxY = selectionMaxZ = 0;
		for (const auto& [x, y, z] : selectedPositions) {
			selectionMinX = std::min(selectionMinX, x);
			selectionMinY = std::min(selectionMinY, y);
			selectionMinZ = std::min(selectionMinZ, z);
			selectionMaxX = std::max(selectionMaxX, x);
			selectionMaxY = std::max(selectionMaxY, y);
			selectionMaxZ = std::max(selectionMaxZ, z);
		}
	}
	BuildLayout();
	UpdateAreaSummary();
	Bind(wxEVT_TIMER, &ProceduralGeneratorDialog::OnWorkerTimer, this, workerTimer->GetId());
	Bind(wxEVT_TIMER, &ProceduralGeneratorDialog::OnBrushTimer, this, brushTimer->GetId());
	Bind(wxEVT_CLOSE_WINDOW, &ProceduralGeneratorDialog::OnClose, this);
	CentreOnParent();
	// Defer the active-client brush catalog until ShowModal has entered its event
	// loop. Loading one choice per event keeps the dialog visible and responsive.
	brushTimer->StartOnce(1);
}

ProceduralGeneratorDialog::~ProceduralGeneratorDialog() {
	closing = true;
	if (brushTimer) brushTimer->Stop();
	StopWorker();
}

void ProceduralGeneratorDialog::BuildLayout() {
	auto* root = new wxBoxSizer(wxVERTICAL);
	notebook = new wxNotebook(this, wxID_ANY);
	BuildPresetPage(notebook);
	BuildAreaPage(notebook);
	BuildTerrainPage(notebook);
	BuildStructuresPage(notebook);
	BuildMaterialsPage(notebook);
	BuildPreviewPage(notebook);
	BuildValidationPage(notebook);
	root->Add(notebook, 1, wxEXPAND | wxALL, 8);

	auto* progressRow = new wxBoxSizer(wxHORIZONTAL);
	statusText = new wxStaticText(this, wxID_ANY, "Opening... loading active-client brushes without blocking the dialog.");
	progressGauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(220, -1));
	progressRow->Add(statusText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
	progressRow->Add(progressGauge, 0, wxALIGN_CENTER_VERTICAL);
	root->Add(progressRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	loadPresetButton = new wxButton(this, wxID_ANY, "Load Preset...");
	savePresetButton = new wxButton(this, wxID_ANY, "Save Preset...");
	generateButton = new wxButton(this, wxID_ANY, "Generate Preview");
	regenerateButton = new wxButton(this, wxID_ANY, "Regenerate");
	applyButton = new wxButton(this, wxID_ANY, "Apply");
	auto* cancelButton = new wxButton(this, wxID_CANCEL, "Cancel");
	applyButton->Enable(false);
	regenerateButton->Enable(false);
	generateButton->Enable(false);
	loadPresetButton->Enable(false);
	savePresetButton->Enable(false);
	buttons->Add(loadPresetButton, 0, wxRIGHT, 6);
	buttons->Add(savePresetButton, 0, wxRIGHT, 12);
	buttons->AddStretchSpacer();
	buttons->Add(generateButton, 0, wxRIGHT, 6);
	buttons->Add(regenerateButton, 0, wxRIGHT, 12);
	buttons->Add(applyButton, 0, wxRIGHT, 6);
	buttons->Add(cancelButton);
	root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	SetSizer(root);
	SetMinSize(wxSize(880, 650));

	generateButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnGenerate, this);
	regenerateButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnRegenerate, this);
	applyButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnApply, this);
	cancelButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnCancel, this);
	loadPresetButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnLoadPreset, this);
	savePresetButton->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnSavePreset, this);
}

void ProceduralGeneratorDialog::BuildPresetPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* root = new wxBoxSizer(wxVERTICAL);
	auto* grid = new wxFlexGridSizer(2, 8, 10);
	grid->AddGrowableCol(1, 1);
	grid->Add(new wxStaticText(page, wxID_ANY, "Preset:"), 0, wxALIGN_CENTER_VERTICAL);
	presetChoice = new wxChoice(page, wxID_ANY);
	for (int preset = 0; preset < static_cast<int>(ProceduralMap::Preset::Count); ++preset) presetChoice->Append(ProceduralMap::PresetName(static_cast<ProceduralMap::Preset>(preset)));
	presetChoice->SetSelection(0);
	grid->Add(presetChoice, 1, wxEXPAND);
	grid->Add(new wxStaticText(page, wxID_ANY, "Seed:"), 0, wxALIGN_CENTER_VERTICAL);
	auto* seedRow = new wxBoxSizer(wxHORIZONTAL);
	seedText = new wxTextCtrl(page, wxID_ANY, "1");
	auto* randomize = new wxButton(page, wxID_ANY, "Randomize");
	seedRow->Add(seedText, 1, wxRIGHT, 6);
	seedRow->Add(randomize);
	grid->Add(seedRow, 1, wxEXPAND);
	root->Add(grid, 0, wxEXPAND | wxALL, 12);
	root->Add(new wxStaticText(page, wxID_ANY, "Generation Brief (supported terms are interpreted locally and deterministically):"), 0, wxLEFT | wxRIGHT | wxTOP, 12);
	briefText = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 90), wxTE_MULTILINE);
	root->Add(briefText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
	auto* interpret = new wxButton(page, wxID_ANY, "Interpret Brief");
	root->Add(interpret, 0, wxALIGN_LEFT | wxALL, 12);
	interpretedText = new wxStaticText(page, wxID_ANY, "Interpreted configuration: no brief interpreted yet.");
	interpretedText->Wrap(760);
	root->Add(interpretedText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	auto* locksBox = new wxStaticBoxSizer(wxHORIZONTAL, page, "Regeneration locks");
	lockTerrain = new wxCheckBox(page, wxID_ANY, "Lock Terrain");
	lockStructures = new wxCheckBox(page, wxID_ANY, "Lock Roads / Structures");
	lockDecorations = new wxCheckBox(page, wxID_ANY, "Lock Decorations");
	locksBox->Add(lockTerrain, 0, wxALL, 6);
	locksBox->Add(lockStructures, 0, wxALL, 6);
	locksBox->Add(lockDecorations, 0, wxALL, 6);
	root->Add(locksBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Preset", true);
	presetChoice->Bind(wxEVT_CHOICE, &ProceduralGeneratorDialog::OnPresetChanged, this);
	randomize->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnRandomize, this);
	interpret->Bind(wxEVT_BUTTON, &ProceduralGeneratorDialog::OnInterpretBrief, this);
}

void ProceduralGeneratorDialog::BuildAreaPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* root = new wxBoxSizer(wxVERTICAL);
	auto* info = new wxStaticText(page, wxID_ANY, capturedSelection ? "The exact selected tile mask is used. Irregular selections are preserved." : "No selection was captured. Define an explicit rectangle; the whole map is never selected silently.");
	info->Wrap(800);
	root->Add(info, 0, wxEXPAND | wxALL, 12);
	auto* grid = new wxFlexGridSizer(4, 8, 10);
	grid->AddGrowableCol(1, 1);
	grid->AddGrowableCol(3, 1);
	const int defaultStartX = capturedSelection ? selectionMinX : 1;
	const int defaultStartY = capturedSelection ? selectionMinY : 1;
	const int defaultEndX = capturedSelection ? selectionMaxX : std::max(1, std::min<int>(editor.getMapWidth(), 64));
	const int defaultEndY = capturedSelection ? selectionMaxY : std::max(1, std::min<int>(editor.getMapHeight(), 64));
	startX = AddSpin(page, grid, "Start X:", defaultStartX, 0, 65535);
	endX = AddSpin(page, grid, "End X:", defaultEndX, 0, 65535);
	startY = AddSpin(page, grid, "Start Y:", defaultStartY, 0, 65535);
	endY = AddSpin(page, grid, "End Y:", defaultEndY, 0, 65535);
	startZ = AddSpin(page, grid, "Start Z:", capturedSelection ? selectionMinZ : launchFloor, 0, 15);
	endZ = AddSpin(page, grid, "End Z:", capturedSelection ? selectionMaxZ : launchFloor, 0, 15);
	startX->Enable(!capturedSelection);
	startY->Enable(!capturedSelection);
	endX->Enable(!capturedSelection);
	endY->Enable(!capturedSelection);
	root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	areaSummary = new wxStaticText(page, wxID_ANY, wxEmptyString);
	root->Add(areaSummary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	auto* policy = new wxFlexGridSizer(2, 8, 10);
	policy->AddGrowableCol(1, 1);
	policy->Add(new wxStaticText(page, wxID_ANY, "Replacement mode:"), 0, wxALIGN_CENTER_VERTICAL);
	replacementChoice = new wxChoice(page, wxID_ANY);
	replacementChoice->Append("Empty tiles only");
	replacementChoice->Append("Replace ground only");
	replacementChoice->Append("Smart replace terrain, borders, walls and conflicts (recommended)");
	replacementChoice->Append("Replace everything inside selection");
	replacementChoice->Append("Blend with existing map");
	replacementChoice->SetSelection(2);
	policy->Add(replacementChoice, 1, wxEXPAND);
	policy->Add(new wxStaticText(page, wxID_ANY, "Edge blending:"), 0, wxALIGN_CENTER_VERTICAL);
	edgeChoice = new wxChoice(page, wxID_ANY);
	edgeChoice->Append("None");
	edgeChoice->Append("Soft");
	edgeChoice->Append("Strong");
	edgeChoice->SetSelection(1);
	policy->Add(edgeChoice, 1, wxEXPAND);
	edgeMargin = AddSpin(page, policy, "Blend margin:", 2, 1, 12, "The margin is clipped to the selected mask; tiles outside it are never changed.");
	root->Add(policy, 0, wxEXPAND | wxALL, 12);
	auto* protectionHelp = new wxStaticText(page, wxID_ANY,
		"Smart replacement removes stale generated layers, blocking objects from planned walkable ground, and unprotected objects that conflict with paths, water, rock, walls, transitions, POIs or new decorations. Gameplay-critical content is always excluded and generation routes around it.");
	protectionHelp->Wrap(800);
	root->Add(protectionHelp, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Area");
	for (auto* control : { startX, startY, startZ, endX, endY, endZ }) control->Bind(wxEVT_SPINCTRL, &ProceduralGeneratorDialog::OnAreaChanged, this);
}

void ProceduralGeneratorDialog::BuildTerrainPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* grid = new wxFlexGridSizer(2, 10, 12);
	grid->AddGrowableCol(1, 1);
	noiseScale = AddDoubleSpin(page, grid, "Noise scale:", 48.0, 1.0, 512.0, 1.0);
	octaves = AddSpin(page, grid, "Octaves:", 4, 1, 10);
	persistence = AddDoubleSpin(page, grid, "Persistence:", 0.52, 0.05, 0.95, 0.01);
	lacunarity = AddDoubleSpin(page, grid, "Lacunarity:", 2.0, 1.1, 4.0, 0.05);
	waterLevel = AddDoubleSpin(page, grid, "Water level:", -0.18, -1.0, 1.0, 0.02);
	irregularity = AddDoubleSpin(page, grid, "Domain irregularity:", 0.55, 0.0, 2.0, 0.05);
	smoothing = AddSpin(page, grid, "Smoothing passes:", 3, 0, 8);
	auto* root = new wxBoxSizer(wxVERTICAL);
	root->Add(grid, 0, wxEXPAND | wxALL, 14);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Terrain");
}

void ProceduralGeneratorDialog::BuildStructuresPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* grid = new wxFlexGridSizer(2, 10, 12);
	grid->AddGrowableCol(1, 1);
	pathWidth = AddSpin(page, grid, "Path / road width:", 2, 1, 12);
	roomCount = AddSpin(page, grid, "Room / area count:", 12, 2, 128);
	roomMinSize = AddSpin(page, grid, "Minimum room size:", 5, 3, 64);
	roomMaxSize = AddSpin(page, grid, "Maximum room size:", 13, 3, 128);
	loops = AddSpin(page, grid, "Alternative loops:", 2, 0, 32);
	roadSpacing = AddSpin(page, grid, "Secondary road spacing:", 10, 6, 40);
	decorationDensity = AddSpin(page, grid, "Decoration density (%):", 18, 0, 100);
	autoFixConnectivity = new wxCheckBox(page, wxID_ANY, "Auto Fix Connectivity");
	autoFixConnectivity->SetValue(true);
	grid->Add(new wxStaticText(page, wxID_ANY, "Connectivity:"), 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(autoFixConnectivity, 0, wxALIGN_CENTER_VERTICAL);
	auto* root = new wxBoxSizer(wxVERTICAL);
	root->Add(grid, 0, wxEXPAND | wxALL, 14);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Structures / Decorations");
}

void ProceduralGeneratorDialog::BuildMaterialsPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* root = new wxBoxSizer(wxVERTICAL);
	const wxString activeClient = wxString::Format("%s (ID %d)", wxstr(g_gui.GetCurrentVersion().getName()), g_gui.GetCurrentVersionID());
	auto* explanation = new wxStaticText(page, wxID_ANY, "Active client: " + activeClient + "\nAll entries below are brushes loaded by this client version. No item ID or material from the reference ZIP is used.");
	explanation->Wrap(800);
	root->Add(explanation, 0, wxEXPAND | wxALL, 12);
	auto* grid = new wxFlexGridSizer(2, 8, 10);
	grid->AddGrowableCol(1, 1);
	for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < static_cast<size_t>(ProceduralMap::MaterialRole::Count); ++role) {
		grid->Add(new wxStaticText(page, wxID_ANY, wxString::Format("%s ground brush:", wxString::FromUTF8(ProceduralMap::MaterialRoleName(static_cast<ProceduralMap::MaterialRole>(role))))), 0, wxALIGN_CENTER_VERTICAL);
		groundChoices[role] = new wxChoice(page, wxID_ANY);
		grid->Add(groundChoices[role], 1, wxEXPAND);
	}
	grid->Add(new wxStaticText(page, wxID_ANY, "Wall brush:"), 0, wxALIGN_CENTER_VERTICAL);
	wallChoice = new wxChoice(page, wxID_ANY);
	grid->Add(wallChoice, 1, wxEXPAND);
	grid->Add(new wxStaticText(page, wxID_ANY, "Decoration doodad brush:"), 0, wxALIGN_CENTER_VERTICAL);
	doodadChoice = new wxChoice(page, wxID_ANY);
	grid->Add(doodadChoice, 1, wxEXPAND);
	grid->Add(new wxStaticText(page, wxID_ANY, "Aligned floor transition doodad brush:"), 0, wxALIGN_CENTER_VERTICAL);
	transitionChoice = new wxChoice(page, wxID_ANY);
	grid->Add(transitionChoice, 1, wxEXPAND);
	root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Materials");
}

void ProceduralGeneratorDialog::BuildPreviewPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* root = new wxBoxSizer(wxVERTICAL);
	auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(new wxStaticText(page, wxID_ANY, "Floor:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	previewFloorChoice = new wxChoice(page, wxID_ANY);
	toolbar->Add(previewFloorChoice, 0, wxRIGHT, 12);
	auto* zoomOut = new wxButton(page, wxID_ANY, "-");
	auto* zoomIn = new wxButton(page, wxID_ANY, "+");
	toolbar->Add(new wxStaticText(page, wxID_ANY, "Zoom:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	toolbar->Add(zoomOut, 0, wxRIGHT, 4);
	toolbar->Add(zoomIn);
	toolbar->AddStretchSpacer();
	toolbar->Add(new wxStaticText(page, wxID_ANY, "Purple X = protected existing content"), 0, wxALIGN_CENTER_VERTICAL);
	root->Add(toolbar, 0, wxEXPAND | wxALL, 8);
	previewPanel = new ProceduralPreviewPanel(page);
	root->Add(previewPanel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Preview");
	previewFloorChoice->Bind(wxEVT_CHOICE, &ProceduralGeneratorDialog::OnPreviewFloorChanged, this);
	zoomOut->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { previewPanel->ZoomBy(-2); });
	zoomIn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { previewPanel->ZoomBy(2); });
}

void ProceduralGeneratorDialog::BuildValidationPage(wxNotebook* parentNotebook) {
	auto* page = new wxPanel(parentNotebook);
	auto* root = new wxBoxSizer(wxVERTICAL);
	validationText = new wxTextCtrl(page, wxID_ANY, "Generate a preview to validate the plan and selected active-client brushes.", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
	root->Add(validationText, 1, wxEXPAND | wxALL, 8);
	page->SetSizer(root);
	parentNotebook->AddPage(page, "Validation");
}

void ProceduralGeneratorDialog::PrepareBrushCatalog() {
	const size_t brushCount = g_brushes.getMap().size();
	std::unordered_set<Brush*> seen;
	seen.reserve(brushCount);
	groundBrushes.reserve(brushCount);
	wallBrushes.reserve(brushCount);
	doodadBrushes.reserve(brushCount);
	groundBrushNames.reserve(brushCount);
	wallBrushNames.reserve(brushCount);
	doodadBrushNames.reserve(brushCount);
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (!brush || name.empty() || !seen.insert(brush).second) continue;
		if (brush->isGround()) {
			groundBrushes.push_back(brush);
			groundBrushNames.push_back(wxstr(brush->getName()));
		}
		if (brush->isWall() && !brush->isWallDecoration()) {
			wallBrushes.push_back(brush);
			wallBrushNames.push_back(wxstr(brush->getName()));
		}
		if (brush->isDoodad()) {
			auto* doodad = brush->asDoodad();
			if (doodad && doodad->hasSingleObjects(0)) {
				doodadBrushes.push_back(brush);
				doodadBrushNames.push_back(wxstr(brush->getName()));
			}
		}
	}
	// Brushes::getMap() is a name-ordered multimap, so the three filtered lists
	// are already deterministic and alphabetic. Sorting them again caused many
	// virtual string allocations on every dialog opening.
}

void ProceduralGeneratorDialog::PopulateBrushChoice(wxChoice* choice, const std::vector<Brush*>& brushes, const std::vector<wxString>& names) {
	if (brushes.size() != names.size()) throw std::logic_error("The brush catalog names and pointers are inconsistent.");
	wxArrayString labels;
	labels.Alloc(names.size() + 1);
	std::vector<void*> clientData;
	clientData.reserve(brushes.size() + 1);
	labels.Add("<not selected>");
	clientData.push_back(nullptr);
	for (size_t index = 0; index < brushes.size(); ++index) {
		labels.Add(names[index]);
		clientData.push_back(brushes[index]);
	}

	choice->Freeze();
	try {
		choice->Append(labels, clientData.data());
		choice->SetSelection(0);
	} catch (...) {
		choice->Thaw();
		throw;
	}
	choice->Thaw();
}

void ProceduralGeneratorDialog::FinishBrushInitialization() {
	auto invalidateMaterialPreview = [this](wxCommandEvent&) {
		if (!plan) return;
		plan.reset();
		applyButton->Enable(false);
		regenerateButton->Enable(false);
		previewFloorChoice->Clear();
		previewPanel->SetPlan(nullptr, {}, launchFloor);
		validationText->SetValue("Material profile changed. Generate a new preview so sprites and validation match the selected active-client brushes.");
		statusText->SetLabel("Material profile changed; preview invalidated.");
	};
	for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < static_cast<size_t>(ProceduralMap::MaterialRole::Count); ++role) groundChoices[role]->Bind(wxEVT_CHOICE, invalidateMaterialPreview);
	wallChoice->Bind(wxEVT_CHOICE, invalidateMaterialPreview);
	doodadChoice->Bind(wxEVT_CHOICE, invalidateMaterialPreview);
	transitionChoice->Bind(wxEVT_CHOICE, invalidateMaterialPreview);
	UpdatePresetDefaults(true);
	brushesReady = true;
	SetGeneratingState(false);
	progressGauge->SetValue(0);
	statusText->SetLabel("Ready. Preview generation does not modify the map.");
}

void ProceduralGeneratorDialog::SelectRecommendedBrushes() {
	const auto preset = static_cast<ProceduralMap::Preset>(std::max(0, presetChoice->GetSelection()));
	std::vector<wxString> base { "grass", "earth", "dirt" };
	std::vector<wxString> secondary { "dirt", "sand", "earth" };
	std::vector<wxString> accent { "moss", "snow", "sand", "grass" };
	std::vector<wxString> path { "dirt", "earth", "cobblestone", "stone floor", "cave" };
	std::vector<wxString> rock { "mountain", "rock", "cave" };
	std::vector<wxString> wall { "stone", "cave", "brick", "wood" };
	std::vector<wxString> doodad { "tree", "plant", "rock", "bush" };
	if (preset == ProceduralMap::Preset::Cave || preset == ProceduralMap::Preset::Dungeon || preset == ProceduralMap::Preset::HuntingArea || preset == ProceduralMap::Preset::Ruins) {
		base = { "cave", "earth", "dirt" };
		secondary = { "cave", "stone", "earth" };
		path = { "cave", "earth", "stone floor" };
		wall = { "cave", "stone" };
		doodad = { "cave", "rock", "bone" };
	} else if (preset == ProceduralMap::Preset::Desert) {
		base = secondary = { "sand", "desert" };
		accent = { "desert", "sand", "dirt" };
		doodad = { "desert", "cactus", "rock" };
	} else if (preset == ProceduralMap::Preset::SnowArea) {
		base = accent = { "snow", "ice" };
		secondary = { "ice", "snow", "stone" };
		doodad = { "snow", "ice", "pine" };
	} else if (preset == ProceduralMap::Preset::Swamp) {
		base = accent = { "swamp", "moss", "mud" };
		secondary = { "mud", "earth", "swamp" };
		doodad = { "swamp", "mushroom", "plant" };
	} else if (preset == ProceduralMap::Preset::City || preset == ProceduralMap::Preset::Village) {
		base = { "grass", "earth" };
		secondary = { "stone", "wood", "floor" };
		accent = { "marble", "stone", "grass" };
		path = { "cobblestone", "pavement", "stone floor", "dirt" };
		wall = { "brick", "stone", "wood" };
		doodad = { "street", "city", "plant", "furniture" };
	}
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Base)], base);
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Secondary)], secondary);
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Water)], { "clear water", "water", "sea", "ocean" });
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Path)], path);
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Rock)], rock);
	SelectByKeywords(groundChoices[static_cast<size_t>(ProceduralMap::MaterialRole::Accent)], accent);
	SelectByKeywords(wallChoice, wall);
	SelectByKeywords(doodadChoice, doodad);
	SelectByKeywords(transitionChoice, { "stair", "ramp", "ladder", "hole" });
}

void ProceduralGeneratorDialog::UpdateAreaSummary() {
	const int width = endX->GetValue() - startX->GetValue() + 1;
	const int height = endY->GetValue() - startY->GetValue() + 1;
	const int floors = endZ->GetValue() - startZ->GetValue() + 1;
	const wxString mask = capturedSelection ? wxString::Format("exact mouse selection (%zu captured tiles)", selectedPositions.size()) : wxString("explicit rectangle");
	areaSummary->SetLabel(wxString::Format("Area: %d x %d | Floors: %d (%d..%d) | Mask: %s", width, height, floors, startZ->GetValue(), endZ->GetValue(), mask));
}

void ProceduralGeneratorDialog::UpdatePresetDefaults(bool force) {
	if (!force) {
		SelectRecommendedBrushes();
		return;
	}
	const auto preset = static_cast<ProceduralMap::Preset>(std::max(0, presetChoice->GetSelection()));
	int density = 18;
	int width = 2;
	int rooms = 12;
	int passes = 3;
	double scale = 48.0;
	switch (preset) {
		case ProceduralMap::Preset::Forest: density = 42; scale = 34.0; break;
		case ProceduralMap::Preset::Mountain: density = 12; scale = 58.0; width = 2; break;
		case ProceduralMap::Preset::Cave: density = 13; passes = 4; width = 2; break;
		case ProceduralMap::Preset::Dungeon: density = 8; rooms = 16; width = 2; break;
		case ProceduralMap::Preset::HuntingArea: density = 16; rooms = 10; width = 3; break;
		case ProceduralMap::Preset::City: density = 8; width = 3; break;
		case ProceduralMap::Preset::Village: density = 16; width = 2; break;
		case ProceduralMap::Preset::Desert: density = 7; scale = 54.0; break;
		case ProceduralMap::Preset::SnowArea: density = 12; scale = 52.0; break;
		case ProceduralMap::Preset::Swamp: density = 28; scale = 30.0; break;
		case ProceduralMap::Preset::Island: density = 24; scale = 44.0; break;
		case ProceduralMap::Preset::River: density = 14; width = 4; break;
		case ProceduralMap::Preset::Ruins: density = 18; rooms = 10; width = 2; break;
		default: break;
	}
	decorationDensity->SetValue(density);
	pathWidth->SetValue(width);
	roomCount->SetValue(rooms);
	smoothing->SetValue(passes);
	noiseScale->SetValue(scale);
	SelectRecommendedBrushes();
}

void ProceduralGeneratorDialog::BuildAllowedMask(ProceduralMap::GenerationRequest& request) const {
	request.allowedCells.assign(request.cellCount(), uint8_t { 0 });
	const auto index = [&](int x, int y, int z) {
		return (static_cast<size_t>(z - request.minZ) * request.height() + static_cast<size_t>(y - request.minY)) * request.width() + static_cast<size_t>(x - request.minX);
	};
	if (!capturedSelection) {
		std::fill(request.allowedCells.begin(), request.allowedCells.end(), uint8_t { 1 });
		return;
	}
	const int capturedFloor = std::get<2>(selectedPositions.front());
	const bool singleFloorSelection = std::all_of(selectedPositions.begin(), selectedPositions.end(), [capturedFloor](const auto& position) { return std::get<2>(position) == capturedFloor; });
	if (singleFloorSelection) {
		for (const auto& position : selectedPositions) {
			const int x = std::get<0>(position);
			const int y = std::get<1>(position);
			if (x < request.minX || x > request.maxX || y < request.minY || y > request.maxY) continue;
			for (int z = request.minZ; z <= request.maxZ; ++z) request.allowedCells[index(x, y, z)] = 1;
		}
	} else {
		for (const auto& [x, y, z] : selectedPositions) {
			if (x >= request.minX && x <= request.maxX && y >= request.minY && y <= request.maxY && z >= request.minZ && z <= request.maxZ) request.allowedCells[index(x, y, z)] = 1;
		}
	}
}

void ProceduralGeneratorDialog::BuildProtectedMask(ProceduralMap::GenerationRequest& request) const {
	request.protectedCells.assign(request.cellCount(), uint8_t { 0 });
	const size_t width = static_cast<size_t>(request.width());
	const size_t height = static_cast<size_t>(request.height());
	for (int z = request.minZ; z <= request.maxZ; ++z) {
		for (int y = request.minY; y <= request.maxY; ++y) {
			for (int x = request.minX; x <= request.maxX; ++x) {
				const size_t index = (static_cast<size_t>(z - request.minZ) * height + static_cast<size_t>(y - request.minY)) * width + static_cast<size_t>(x - request.minX);
				if (request.allowedCells[index] != 0 && IsProtectedTile(editor.map.getTile(x, y, z))) request.protectedCells[index] = 1;
			}
		}
	}
}

ProceduralMap::GenerationRequest ProceduralGeneratorDialog::ReadRequest(wxString& error) const {
	ProceduralMap::GenerationRequest request;
	request.minX = startX->GetValue();
	request.minY = startY->GetValue();
	request.minZ = startZ->GetValue();
	request.maxX = endX->GetValue();
	request.maxY = endY->GetValue();
	request.maxZ = endZ->GetValue();
	if (request.minX > request.maxX || request.minY > request.maxY || request.minZ > request.maxZ) {
		error = "Every start coordinate must be less than or equal to its end coordinate.";
		return request;
	}
	if (request.maxX > editor.getMapWidth() || request.maxY > editor.getMapHeight()) {
		error = wxString::Format("The rectangle exceeds the open map bounds (%u x %u).", static_cast<unsigned int>(editor.getMapWidth()), static_cast<unsigned int>(editor.getMapHeight()));
		return request;
	}
	const uint64_t cellCount = static_cast<uint64_t>(request.maxX - request.minX + 1) * static_cast<uint64_t>(request.maxY - request.minY + 1) * static_cast<uint64_t>(request.maxZ - request.minZ + 1);
	if (cellCount > ProceduralMap::MaximumPlanCells) {
		error = wxString::Format("The requested area exceeds the configured memory safety limit of %zu cells.", ProceduralMap::MaximumPlanCells);
		return request;
	}
	try {
		size_t parsed = 0;
		const std::string seedValue = seedText->GetValue().ToStdString();
		request.seed = std::stoull(seedValue, &parsed, 10);
		if (parsed != seedValue.size()) throw std::invalid_argument("seed");
	} catch (...) {
		error = "Seed must be an unsigned integer.";
		return request;
	}
	request.preset = static_cast<ProceduralMap::Preset>(std::max(0, presetChoice->GetSelection()));
	request.replacementMode = static_cast<ProceduralMap::ReplacementMode>(std::max(0, replacementChoice->GetSelection()));
	request.edgeBlending = static_cast<ProceduralMap::EdgeBlending>(std::max(0, edgeChoice->GetSelection()));
	request.parameters.noiseScale = noiseScale->GetValue();
	request.parameters.octaves = octaves->GetValue();
	request.parameters.persistence = persistence->GetValue();
	request.parameters.lacunarity = lacunarity->GetValue();
	request.parameters.waterLevel = waterLevel->GetValue();
	request.parameters.irregularity = irregularity->GetValue();
	request.parameters.smoothingPasses = smoothing->GetValue();
	request.parameters.decorationDensity = decorationDensity->GetValue();
	request.parameters.pathWidth = pathWidth->GetValue();
	request.parameters.roomCount = roomCount->GetValue();
	request.parameters.roomMinSize = roomMinSize->GetValue();
	request.parameters.roomMaxSize = roomMaxSize->GetValue();
	request.parameters.loops = loops->GetValue();
	request.parameters.secondaryRoadSpacing = roadSpacing->GetValue();
	request.parameters.edgeMargin = edgeMargin->GetValue();
	request.parameters.autoFixConnectivity = autoFixConnectivity->GetValue();
	try {
		BuildAllowedMask(request);
		BuildProtectedMask(request);
	} catch (const std::bad_alloc&) {
		error = "Not enough memory to build the exact selection and protected-content masks.";
	}
	return request;
}

void ProceduralGeneratorDialog::SetGeneratingState(bool generating) {
	generateButton->Enable(brushesReady && !generating);
	regenerateButton->Enable(brushesReady && !generating && plan.has_value());
	applyButton->Enable(brushesReady && !generating && plan.has_value() && !plan->hasErrors());
	loadPresetButton->Enable(brushesReady && !generating);
	savePresetButton->Enable(brushesReady && !generating);
}

void ProceduralGeneratorDialog::StartGeneration(bool randomizeSeed) {
	if (!brushesReady) {
		statusText->SetLabel("Active-client brushes are still loading...");
		return;
	}
	if (randomizeSeed) {
		std::random_device device;
		const uint64_t value = (static_cast<uint64_t>(device()) << 32) ^ device() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
		seedText->SetValue(wxString::Format("%llu", static_cast<unsigned long long>(value)));
	}
	wxString error;
	auto request = ReadRequest(error);
	if (!error.empty()) {
		wxMessageBox(error, "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	}
	StopWorker();
	{
		std::scoped_lock lock(workerMutex);
		pendingPlan.reset();
		workerError.clear();
	}
	workerDone = false;
	workerProgress = 0;
	workerStage = ProceduralMap::GenerationStage::ReadingSelection;
	SetGeneratingState(true);
	progressGauge->SetValue(0);
	statusText->SetLabel("Generating worker-safe plan...");
	try {
		worker = std::jthread([this, request = std::move(request)](std::stop_token stopToken) {
			try {
				auto generated = ProceduralMap::Generate(request, stopToken, [this](int progress, ProceduralMap::GenerationStage stage) {
					workerProgress.store(progress, std::memory_order_relaxed);
					workerStage.store(stage, std::memory_order_relaxed);
				});
				std::scoped_lock lock(workerMutex);
				pendingPlan = std::move(generated);
			} catch (const std::bad_alloc&) {
				std::scoped_lock lock(workerMutex);
				workerError = "Not enough memory to build the generation plan.";
			} catch (const std::exception& exception) {
				std::scoped_lock lock(workerMutex);
				workerError = exception.what();
			} catch (...) {
				std::scoped_lock lock(workerMutex);
				workerError = "Unexpected failure while building the generation plan.";
			}
			workerDone.store(true, std::memory_order_release);
		});
	} catch (const std::exception& exception) {
		SetGeneratingState(false);
		statusText->SetLabel("Could not start the generation worker.");
		wxMessageBox(wxString("Could not start the generation worker: ") + wxString::FromUTF8(exception.what()), "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	} catch (...) {
		SetGeneratingState(false);
		statusText->SetLabel("Could not start the generation worker.");
		wxMessageBox("Could not start the generation worker.", "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	}
	workerTimer->Start(kWorkerTimerIntervalMs);
}

void ProceduralGeneratorDialog::OnBrushTimer(wxTimerEvent&) {
	if (closing || brushesReady) return;
	try {
		if (!brushCatalogPrepared) {
			PrepareBrushCatalog();
			brushCatalogPrepared = true;
		}

		constexpr size_t firstGround = static_cast<size_t>(ProceduralMap::MaterialRole::Base);
		constexpr size_t groundChoiceCount = static_cast<size_t>(ProceduralMap::MaterialRole::Count) - firstGround;
		constexpr size_t totalChoiceCount = groundChoiceCount + 3;
		if (brushChoiceLoadIndex < groundChoiceCount) {
			PopulateBrushChoice(groundChoices[firstGround + brushChoiceLoadIndex], groundBrushes, groundBrushNames);
		} else if (brushChoiceLoadIndex == groundChoiceCount) {
			PopulateBrushChoice(wallChoice, wallBrushes, wallBrushNames);
		} else if (brushChoiceLoadIndex == groundChoiceCount + 1) {
			PopulateBrushChoice(doodadChoice, doodadBrushes, doodadBrushNames);
		} else {
			PopulateBrushChoice(transitionChoice, doodadBrushes, doodadBrushNames);
		}
		++brushChoiceLoadIndex;
		progressGauge->SetValue(static_cast<int>(brushChoiceLoadIndex * 100 / totalChoiceCount));
		statusText->SetLabel(wxString::Format("Loading active-client brushes... %zu/%zu", brushChoiceLoadIndex, totalChoiceCount));
		if (brushChoiceLoadIndex < totalChoiceCount) {
			brushTimer->StartOnce(1);
		} else {
			FinishBrushInitialization();
		}
	} catch (const std::bad_alloc&) {
		statusText->SetLabel("Not enough memory to load the active-client brush catalog.");
		wxMessageBox("Not enough memory to load the active-client brush catalog.", "Procedural Map Generator", wxOK | wxICON_ERROR, this);
	} catch (const std::exception& exception) {
		statusText->SetLabel("Could not load the active-client brush catalog.");
		wxMessageBox(wxString("Could not load active-client brushes: ") + wxString::FromUTF8(exception.what()), "Procedural Map Generator", wxOK | wxICON_ERROR, this);
	} catch (...) {
		statusText->SetLabel("Could not load the active-client brush catalog.");
		wxMessageBox("An unexpected failure occurred while loading active-client brushes.", "Procedural Map Generator", wxOK | wxICON_ERROR, this);
	}
}

void ProceduralGeneratorDialog::StopWorker() {
	if (workerTimer) workerTimer->Stop();
	if (worker.joinable()) {
		worker.request_stop();
		worker.join();
	}
}

void ProceduralGeneratorDialog::OnWorkerTimer(wxTimerEvent&) {
	progressGauge->SetValue(workerProgress.load(std::memory_order_relaxed));
	statusText->SetLabel(wxString::Format("%d%% - %s", workerProgress.load(std::memory_order_relaxed), wxString::FromUTF8(ProceduralMap::GenerationStageName(workerStage.load(std::memory_order_relaxed)))));
	if (!workerDone.load(std::memory_order_acquire)) return;
	if (workerTimer) workerTimer->Stop();
	if (worker.joinable()) worker.join();
	std::optional<ProceduralMap::GenerationPlan> generated;
	std::string error;
	{
		std::scoped_lock lock(workerMutex);
		generated = std::move(pendingPlan);
		error = std::move(workerError);
		pendingPlan.reset();
		workerError.clear();
	}
	if (!error.empty()) {
		statusText->SetLabel(wxstr(error));
		SetGeneratingState(false);
		if (error != "Generation cancelled." && !closing) wxMessageBox(wxstr(error), "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	}
	if (!generated) {
		statusText->SetLabel("Generation stopped.");
		SetGeneratingState(false);
		return;
	}
	try {
		if (plan) ProceduralMap::MergeLockedParts(*generated, *plan, lockTerrain->GetValue(), lockStructures->GetValue(), lockDecorations->GetValue());
		plan = std::move(generated);
		std::vector<ProceduralMap::ValidationIssue> materialIssues;
		ValidateMaterials(materialIssues);
		plan->issues.insert(plan->issues.end(), materialIssues.begin(), materialIssues.end());
		UpdatePreviewFloorChoice();
		std::array<int, static_cast<size_t>(ProceduralMap::MaterialRole::Count)> lookIds {};
		for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < static_cast<size_t>(ProceduralMap::MaterialRole::Count); ++role) {
			if (auto* brush = SelectedGround(static_cast<ProceduralMap::MaterialRole>(role))) lookIds[role] = brush->getLookID();
		}
		const int previewFloor = previewFloorChoice->GetSelection() == wxNOT_FOUND ? plan->request.minZ : plan->request.minZ + previewFloorChoice->GetSelection();
		previewPanel->SetPlan(&*plan, lookIds, previewFloor);
		UpdateValidationReport();
	} catch (const std::bad_alloc&) {
		plan.reset();
		previewPanel->SetPlan(nullptr, {}, launchFloor);
		statusText->SetLabel("Not enough memory to finish the preview.");
		SetGeneratingState(false);
		wxMessageBox("Not enough memory to finish the preview.", "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	} catch (const std::exception& exception) {
		plan.reset();
		previewPanel->SetPlan(nullptr, {}, launchFloor);
		statusText->SetLabel("Could not finish the preview.");
		SetGeneratingState(false);
		wxMessageBox(wxString("Could not finish the preview: ") + wxString::FromUTF8(exception.what()), "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		return;
	}
	statusText->SetLabel(plan->hasErrors() ? "Preview generated with validation errors. Review the Validation tab." : "Preview ready. The open map is still unchanged.");
	regenerateButton->Enable(true);
	applyButton->Enable(!plan->hasErrors());
	SetGeneratingState(false);
}

void ProceduralGeneratorDialog::UpdatePreviewFloorChoice() {
	previewFloorChoice->Clear();
	if (!plan) return;
	for (int z = plan->request.minZ; z <= plan->request.maxZ; ++z) previewFloorChoice->Append(wxString::Format("Z %d", z));
	previewFloorChoice->SetSelection(0);
}

void ProceduralGeneratorDialog::UpdateValidationReport() {
	if (!plan) return;
	wxString report;
	report << wxString::Format("Preset: %s\nSeed: %llu\nPlan hash: %016llX\n\n", wxString::FromUTF8(ProceduralMap::PresetName(plan->request.preset)), static_cast<unsigned long long>(plan->request.seed), static_cast<unsigned long long>(plan->stableHash));
	report << wxString::Format("Selected cells: %zu\nProtected cells excluded from planning: %zu\nPlanned tiles: %zu\nWalkable tiles: %zu\nWall positions: %zu\nDecoration positions: %zu\nPoints of interest: %zu\nRooms / chambers placed: %zu\nDetected walkable regions: %zu\nConnectivity repairs: %zu\nApproximate plan memory: %.2f MiB\n\n",
		plan->statistics.allowedTiles, plan->statistics.protectedTiles, plan->statistics.plannedTiles, plan->statistics.walkableTiles, plan->statistics.wallTiles, plan->statistics.decorationTiles,
		plan->statistics.pointOfInterestTiles, plan->statistics.roomsPlaced, plan->statistics.connectedRegions, plan->statistics.repairedConnections, plan->statistics.approximateBytes / (1024.0 * 1024.0));
	ProtectionSummary protection;
	for (int z = plan->request.minZ; z <= plan->request.maxZ; ++z) {
		for (int y = plan->request.minY; y <= plan->request.maxY; ++y) {
			for (int x = plan->request.minX; x <= plan->request.maxX; ++x) {
				if (plan->request.isProtected(x, y, z)) IsProtectedTile(editor.map.getTile(x, y, z), &protection);
			}
		}
	}
	report << wxString::Format("Protected tiles (always excluded): %zu\n  Houses/exits: %zu\n  Doors: %zu\n  Teleports: %zu\n  Depots: %zu\n  Spawns: %zu\n  Creatures/NPCs: %zu\n  Waypoints/towns: %zu\n  Unique/Action items: %zu\n  Customized item attributes: %zu\n  Non-empty containers: %zu\n  Zones/flags: %zu\n\n",
		protection.tiles, protection.houses, protection.doors, protection.teleports, protection.depots, protection.spawns, protection.creatures, protection.waypointsOrTowns, protection.uniqueOrActionItems, protection.customizedItems, protection.containers, protection.zonesOrFlags);
	const ApplyImpactSummary impact = ComputeApplyImpact(editor, *plan);
	report << wxString::Format("Apply impact for the selected replacement mode:\n  Eligible tiles: %zu\n  Skipped by policy/blending: %zu\n  Existing grounds replaced: %zu\n  Stale borders removed: %zu\n  Existing walls replaced/removed: %zu\n  Conflicting unprotected objects removed: %zu (%zu blocking)\n  Non-conflicting objects preserved: %zu\n\n",
		impact.eligibleTiles, impact.skippedByPolicyTiles, impact.groundsReplaced, impact.bordersRemoved, impact.wallsRemoved, impact.objectsRemoved, impact.blockingObjectsRemoved, impact.objectsPreserved);
	if (plan->issues.empty()) {
		report << "Validation: no problems found.\n";
	} else {
		report << "Validation findings:\n";
		for (const auto& issue : plan->issues) {
			const char* severity = issue.severity == ProceduralMap::IssueSeverity::Error ? "ERROR" : issue.severity == ProceduralMap::IssueSeverity::Warning ? "WARNING" : "INFO";
			report << wxString::Format("- [%s] %s", wxString::FromUTF8(severity), wxstr(issue.message));
			if (issue.x >= 0) report << wxString::Format(" (%d,%d,%d)", issue.x, issue.y, issue.z);
			report << "\n";
		}
	}
	validationText->SetValue(report);
}

void ProceduralGeneratorDialog::OnGenerate(wxCommandEvent&) { StartGeneration(false); }
void ProceduralGeneratorDialog::OnRegenerate(wxCommandEvent&) { StartGeneration(true); }

void ProceduralGeneratorDialog::OnRandomize(wxCommandEvent&) {
	std::random_device device;
	const uint64_t value = (static_cast<uint64_t>(device()) << 32) ^ device() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	seedText->SetValue(wxString::Format("%llu", static_cast<unsigned long long>(value)));
}

void ProceduralGeneratorDialog::OnPresetChanged(wxCommandEvent&) { UpdatePresetDefaults(true); }
void ProceduralGeneratorDialog::OnAreaChanged(wxCommandEvent&) { UpdateAreaSummary(); }

void ProceduralGeneratorDialog::OnPreviewFloorChanged(wxCommandEvent&) {
	if (plan && previewFloorChoice->GetSelection() != wxNOT_FOUND) previewPanel->SetFloor(plan->request.minZ + previewFloorChoice->GetSelection());
}

void ProceduralGeneratorDialog::OnInterpretBrief(wxCommandEvent&) {
	const auto interpreted = ProceduralMap::InterpretBrief(briefText->GetValue().ToStdString());
	if (!interpreted.hasSupportedTerms) {
		interpretedText->SetLabel("No supported terms were found. Structured settings were not changed.");
		return;
	}
	wxString summary = wxString::Format("Preset: %s\nRecognized: %s", wxString::FromUTF8(ProceduralMap::PresetName(interpreted.preset)), JoinWords(interpreted.recognized));
	if (!interpreted.unknown.empty()) summary += "\nUnknown (ignored): " + JoinWords(interpreted.unknown);
	const int answer = wxMessageBox("Interpreted configuration:\n\n" + summary + "\n\nApply this interpretation to the structured controls?", "Confirm Generation Brief", wxYES_NO | wxICON_QUESTION, this);
	interpretedText->SetLabel("Interpreted configuration:\n" + summary);
	interpretedText->Wrap(760);
	if (answer != wxYES) return;
	presetChoice->SetSelection(static_cast<int>(interpreted.preset));
	UpdatePresetDefaults(true);
	pathWidth->SetValue(interpreted.parameters.pathWidth);
	decorationDensity->SetValue(interpreted.parameters.decorationDensity);
}

Brush* ProceduralGeneratorDialog::SelectedBrush(const wxChoice* choice) const {
	if (!choice || choice->GetSelection() == wxNOT_FOUND) return nullptr;
	return static_cast<Brush*>(choice->GetClientData(static_cast<unsigned int>(choice->GetSelection())));
}

GroundBrush* ProceduralGeneratorDialog::SelectedGround(ProceduralMap::MaterialRole role) const {
	const size_t index = static_cast<size_t>(role);
	if (index >= groundChoices.size()) return nullptr;
	auto* brush = SelectedBrush(groundChoices[index]);
	return brush && brush->isGround() ? brush->asGround() : nullptr;
}

WallBrush* ProceduralGeneratorDialog::SelectedWall() const {
	auto* brush = SelectedBrush(wallChoice);
	return brush && brush->isWall() ? brush->asWall() : nullptr;
}

DoodadBrush* ProceduralGeneratorDialog::SelectedDoodad(const wxChoice* choice) const {
	auto* brush = SelectedBrush(choice);
	return brush && brush->isDoodad() ? brush->asDoodad() : nullptr;
}

bool ProceduralGeneratorDialog::ValidateMaterials(std::vector<ProceduralMap::ValidationIssue>& issues) const {
	if (!plan) return false;
	std::array<bool, static_cast<size_t>(ProceduralMap::MaterialRole::Count)> usedRoles {};
	const bool appliesObjects = plan->request.replacementMode != ProceduralMap::ReplacementMode::ReplaceGroundOnly;
	bool needsWall = false;
	bool needsDecoration = false;
	bool needsTransition = false;
	for (const auto& cell : plan->cells) {
		if (cell.material != ProceduralMap::MaterialRole::None) usedRoles[static_cast<size_t>(cell.material)] = true;
		needsWall = needsWall || (appliesObjects && (cell.features & ProceduralMap::FeatureWall) != 0);
		needsDecoration = needsDecoration || (appliesObjects && (cell.features & ProceduralMap::FeatureDecoration) != 0);
		needsTransition = needsTransition || (appliesObjects && (cell.features & ProceduralMap::FeatureTransition) != 0);
	}
	for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < usedRoles.size(); ++role) {
		if (usedRoles[role] && !SelectedGround(static_cast<ProceduralMap::MaterialRole>(role))) {
			issues.push_back({ ProceduralMap::IssueSeverity::Error, std::string("Select an active-client ground brush for the ") + ProceduralMap::MaterialRoleName(static_cast<ProceduralMap::MaterialRole>(role)) + " material role." });
		}
	}
	if (needsWall && !SelectedWall()) issues.push_back({ ProceduralMap::IssueSeverity::Error, "The plan contains walls, but no active-client WallBrush is selected." });
	if (needsDecoration && !SelectedDoodad(doodadChoice)) issues.push_back({ ProceduralMap::IssueSeverity::Error, "The plan contains decorations, but no compatible active-client DoodadBrush is selected." });
	if (needsTransition && !SelectedDoodad(transitionChoice)) issues.push_back({ ProceduralMap::IssueSeverity::Error, "The multi-floor plan requires an explicitly selected stair/ramp/transition DoodadBrush." });
	return std::none_of(issues.begin(), issues.end(), [](const auto& issue) { return issue.severity == ProceduralMap::IssueSeverity::Error; });
}

bool ProceduralGeneratorDialog::ConfirmDestructiveApply() const {
	if (!plan) return false;
	const ApplyImpactSummary impact = ComputeApplyImpact(editor, *plan);
	const bool replaceEverything = plan->request.replacementMode == ProceduralMap::ReplacementMode::ReplaceEverything;
	if (!replaceEverything && impact.objectsRemoved == 0) return true;
	const wxString modeText = replaceEverything ? "Replace everything" : "Smart replacement";
	const wxString message = wxString::Format(
		"%s will modify %zu eligible tiles.\n\nExisting layers to remove:\n- %zu grounds\n- %zu borders\n- %zu walls\n- %zu conflicting unprotected objects (%zu blocking)\n\n%zu protected tiles and %zu non-conflicting objects remain untouched.\n\nContinue?",
		modeText, impact.eligibleTiles, impact.groundsReplaced, impact.bordersRemoved, impact.wallsRemoved, impact.objectsRemoved,
		impact.blockingObjectsRemoved, impact.skippedProtectedTiles, impact.objectsPreserved
	);
	return wxMessageBox(message, "Confirm generated map changes", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, const_cast<ProceduralGeneratorDialog*>(this)) == wxYES;
}

bool ProceduralGeneratorDialog::ApplyPlan(wxString& error) {
	if (!plan) {
		error = "Generate and validate a preview before applying.";
		return false;
	}
	std::unique_ptr<BatchAction> batch;
	try {
		std::vector<ProceduralMap::ValidationIssue> materialIssues;
		if (!ValidateMaterials(materialIssues)) {
			error = wxstr(materialIssues.front().message);
			return false;
		}

		batch.reset(editor.actionQueue->createBatch(ACTION_GENERATE_AREA));
		std::vector<const ProceduralMap::GeneratedCell*> affected;
		affected.reserve(plan->cells.size());
		for (const auto& cell : plan->cells) {
			Tile* oldTile = editor.map.getTile(cell.x, cell.y, cell.z);
			if (IsApplyEligible(*plan, cell, oldTile)) affected.push_back(&cell);
		}
		if (affected.empty()) {
			error = "No eligible tile remains after replacement policy and protected-content checks.";
			return false;
		}

		ScopedRandomSeed deterministicBrushRandom(plan->request.seed);
		auto groundAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
		for (const auto* cell : affected) {
			const Position position(cell->x, cell->y, cell->z);
			TileLocation* location = editor.map.createTileL(position);
			Tile* oldTile = location->get();
			const bool wasSelected = oldTile && oldTile->isSelected();
			std::unique_ptr<Tile> newTile;
			if (oldTile && plan->request.replacementMode != ProceduralMap::ReplacementMode::ReplaceEverything) {
				newTile.reset(oldTile->deepCopy(editor.map));
			} else {
				newTile.reset(editor.map.allocator(location));
			}
			delete newTile->ground;
			newTile->ground = nullptr;
			if (plan->request.replacementMode != ProceduralMap::ReplacementMode::ReplaceEverything) RemoveConflictingItems(*newTile, *cell, plan->request.replacementMode);
			GroundBrush* ground = SelectedGround(cell->material);
			if (!ground) throw std::runtime_error(std::string("Missing ground brush for ") + ProceduralMap::MaterialRoleName(cell->material) + ".");
			ground->draw(&editor.map, newTile.get(), nullptr);
			if (!newTile->ground) throw std::runtime_error(std::string("The selected ground brush '") + ground->getName() + "' did not create a valid ground item.");
			if (wasSelected) newTile->select();
			newTile->update();
			groundAction->addChange(newd Change(newTile.release()));
		}
		batch->addAndCommitAction(groundAction.release());

		const bool updateBorders = plan->request.replacementMode != ProceduralMap::ReplacementMode::ReplaceGroundOnly;
		if (updateBorders) {
			auto borderAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
			for (const auto* cell : affected) {
				Tile* current = editor.map.getTile(cell->x, cell->y, cell->z);
				if (!current) continue;
				auto newTile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
				newTile->cleanBorders();
				newTile->borderize(&editor.map);
				newTile->update();
				borderAction->addChange(newd Change(newTile.release()));
			}
			batch->addAndCommitAction(borderAction.release());
		}

		const bool addObjects = plan->request.replacementMode != ProceduralMap::ReplacementMode::ReplaceGroundOnly;
		if (addObjects) {
			if (WallBrush* wall = SelectedWall()) {
				auto wallAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
				for (const auto* cell : affected) {
					if ((cell->features & ProceduralMap::FeatureWall) == 0) continue;
					Tile* current = editor.map.getTile(cell->x, cell->y, cell->z);
					if (!current) continue;
					auto newTile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
					newTile->cleanWalls();
					wall->draw(&editor.map, newTile.get(), nullptr);
					if (!newTile->hasWall()) throw std::runtime_error(std::string("The selected wall brush '") + wall->getName() + "' did not create a valid wall item.");
					newTile->update();
					wallAction->addChange(newd Change(newTile.release()));
				}
				batch->addAndCommitAction(wallAction.release());
				auto wallizeAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
				for (const auto* cell : affected) {
					if ((cell->features & ProceduralMap::FeatureWall) == 0) continue;
					Tile* current = editor.map.getTile(cell->x, cell->y, cell->z);
					if (!current || !current->hasWall()) continue;
					auto newTile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
					newTile->wallize(&editor.map);
					newTile->update();
					wallizeAction->addChange(newd Change(newTile.release()));
				}
				batch->addAndCommitAction(wallizeAction.release());
			}

			auto objectAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
			DoodadBrush* decoration = SelectedDoodad(doodadChoice);
			DoodadBrush* transition = SelectedDoodad(transitionChoice);
			std::set<std::tuple<int, int, int>> objectBorderPositions;
			std::unordered_set<uint64_t> affectedPositions;
			affectedPositions.reserve(affected.size());
			for (const auto* cell : affected) affectedPositions.insert(PositionKey(cell->x, cell->y, cell->z));
			for (const auto* cell : affected) {
				DoodadBrush* brush = (cell->features & ProceduralMap::FeatureTransition) != 0 ? transition : ((cell->features & ProceduralMap::FeatureDecoration) != 0 ? decoration : nullptr);
				if (!brush) continue;
				Tile* current = editor.map.getTile(cell->x, cell->y, cell->z);
				if (!current || (current->isBlocking() && !brush->placeOnBlocking())) continue;
				auto newTile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
				const int before = newTile->size();
				const uint16_t beforeGround = newTile->ground ? newTile->ground->getID() : 0;
				int variation = static_cast<int>(MixApplyHash(plan->request.seed ^ PositionKey(cell->x, cell->y, cell->z) ^ 0xD00DULL) % static_cast<uint64_t>(std::max(1, brush->getMaxVariation())));
				brush->draw(&editor.map, newTile.get(), &variation);
				const uint16_t afterGround = newTile->ground ? newTile->ground->getID() : 0;
				if (newTile->size() == before && beforeGround == afterGround) throw std::runtime_error(std::string("The selected doodad brush '") + brush->getName() + "' did not create an item.");
				newTile->update();
				objectAction->addChange(newd Change(newTile.release()));
				if (updateBorders && brush->doNewBorders()) {
					for (int offsetY = -1; offsetY <= 1; ++offsetY) {
						for (int offsetX = -1; offsetX <= 1; ++offsetX) {
							const int x = cell->x + offsetX;
							const int y = cell->y + offsetY;
							if (plan->request.isAllowed(x, y, cell->z) && affectedPositions.contains(PositionKey(x, y, cell->z))) objectBorderPositions.emplace(x, y, cell->z);
						}
					}
				}
			}
			batch->addAndCommitAction(objectAction.release());
			if (!objectBorderPositions.empty()) {
				auto objectBorderAction = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
				for (const auto& [x, y, z] : objectBorderPositions) {
					Tile* current = editor.map.getTile(x, y, z);
					if (!current) continue;
					auto newTile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
					newTile->borderize(&editor.map);
					newTile->wallize(&editor.map);
					newTile->update();
					objectBorderAction->addChange(newd Change(newTile.release()));
				}
				batch->addAndCommitAction(objectBorderAction.release());
			}
		}

		if (batch->size() == 0) {
			error = "The plan did not produce any map change.";
			return false;
		}
		editor.actionQueue->addBatch(batch.release());
		return true;
	} catch (const std::bad_alloc&) {
		if (batch) batch->rollback();
		error = "Not enough memory to apply the generated area. All committed phases were rolled back.";
	} catch (const std::exception& exception) {
		if (batch) batch->rollback();
		error = wxString::FromUTF8(exception.what()) + " All committed phases were rolled back.";
	} catch (...) {
		if (batch) batch->rollback();
		error = "Unexpected apply failure. All committed phases were rolled back.";
	}
	return false;
}

void ProceduralGeneratorDialog::OnApply(wxCommandEvent&) {
	wxString requestError;
	const auto currentRequest = ReadRequest(requestError);
	if (!requestError.empty() || !plan || !RequestsEquivalent(currentRequest, plan->request)) {
		const wxString message = requestError.empty() ? wxString("Settings changed after the preview was generated. Generate a new preview before applying.") : requestError;
		wxMessageBox(message, "Procedural Map Generator", wxOK | wxICON_INFORMATION, this);
		return;
	}
	if (!ConfirmDestructiveApply()) return;
	statusText->SetLabel("Applying with active-client brushes and one Undo transaction...");
	wxString error;
	if (!ApplyPlan(error)) {
		wxMessageBox(error, "Procedural Map Generator", wxOK | wxICON_ERROR, this);
		statusText->SetLabel("Apply failed; the original map was preserved.");
		return;
	}
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
	g_gui.SetStatusText("Procedural area generated. Ctrl+Z restores the previous area.");
	EndModal(wxID_OK);
}

void ProceduralGeneratorDialog::OnSavePreset(wxCommandEvent&) {
	wxString error;
	const auto request = ReadRequest(error);
	if (!error.empty()) {
		wxMessageBox(error, "Save Preset", wxOK | wxICON_ERROR, this);
		return;
	}
	wxFileDialog dialog(this, "Save procedural preset", wxEmptyString, "procedural-preset.json", "JSON preset (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() != wxID_OK) return;
	try {
		nlohmann::json json;
		json["format"] = "rme-procedural-preset";
		json["version"] = 1;
		json["clientVersion"] = g_gui.GetCurrentVersionID();
		json["preset"] = static_cast<int>(request.preset);
		json["seed"] = request.seed;
		json["replacementMode"] = static_cast<int>(request.replacementMode);
		json["edgeBlending"] = static_cast<int>(request.edgeBlending);
		json["parameters"] = {
			{ "noiseScale", request.parameters.noiseScale }, { "octaves", request.parameters.octaves }, { "persistence", request.parameters.persistence },
			{ "lacunarity", request.parameters.lacunarity }, { "waterLevel", request.parameters.waterLevel }, { "irregularity", request.parameters.irregularity },
			{ "smoothingPasses", request.parameters.smoothingPasses }, { "decorationDensity", request.parameters.decorationDensity }, { "pathWidth", request.parameters.pathWidth },
			{ "roomCount", request.parameters.roomCount }, { "roomMinSize", request.parameters.roomMinSize }, { "roomMaxSize", request.parameters.roomMaxSize },
			{ "loops", request.parameters.loops }, { "secondaryRoadSpacing", request.parameters.secondaryRoadSpacing }, { "edgeMargin", request.parameters.edgeMargin },
			{ "autoFixConnectivity", request.parameters.autoFixConnectivity }
		};
		nlohmann::json brushes;
		for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < static_cast<size_t>(ProceduralMap::MaterialRole::Count); ++role) {
			if (auto* brush = SelectedGround(static_cast<ProceduralMap::MaterialRole>(role))) brushes[ProceduralMap::MaterialRoleName(static_cast<ProceduralMap::MaterialRole>(role))] = brush->getName();
		}
		if (auto* brush = SelectedWall()) brushes["Wall"] = brush->getName();
		if (auto* brush = SelectedDoodad(doodadChoice)) brushes["Decoration"] = brush->getName();
		if (auto* brush = SelectedDoodad(transitionChoice)) brushes["Transition"] = brush->getName();
		json["brushes"] = std::move(brushes);
		std::ofstream output(dialog.GetPath().ToStdString(), std::ios::binary | std::ios::trunc);
		if (!output) throw std::runtime_error("Could not create the preset file.");
		output << json.dump(2);
		if (!output) throw std::runtime_error("Could not finish writing the preset file.");
	} catch (const std::exception& exception) {
		wxMessageBox(wxString::FromUTF8(exception.what()), "Save Preset", wxOK | wxICON_ERROR, this);
	}
}

void ProceduralGeneratorDialog::OnLoadPreset(wxCommandEvent&) {
	wxFileDialog dialog(this, "Load procedural preset", wxEmptyString, wxEmptyString, "JSON preset (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) return;
	try {
		std::ifstream input(dialog.GetPath().ToStdString(), std::ios::binary);
		if (!input) throw std::runtime_error("Could not open the preset file.");
		nlohmann::json json;
		input >> json;
		if (json.value("format", std::string {}) != "rme-procedural-preset" || json.value("version", 0) != 1) throw std::runtime_error("Unsupported procedural preset format.");
		const int savedClient = json.value("clientVersion", -1);
		if (savedClient != g_gui.GetCurrentVersionID()) {
			wxMessageBox("This preset was saved for a different client version. Brush names will be validated against the currently active client; missing brushes will remain unselected.", "Client version changed", wxOK | wxICON_WARNING, this);
		}
		presetChoice->SetSelection(std::clamp(json.value("preset", 0), 0, static_cast<int>(ProceduralMap::Preset::Count) - 1));
		seedText->SetValue(wxString::Format("%llu", static_cast<unsigned long long>(json.value("seed", uint64_t { 1 }))));
		replacementChoice->SetSelection(std::clamp(json.value("replacementMode", 2), 0, 4));
		edgeChoice->SetSelection(std::clamp(json.value("edgeBlending", 1), 0, 2));
		const auto& parameters = json.at("parameters");
		noiseScale->SetValue(parameters.value("noiseScale", 48.0));
		octaves->SetValue(parameters.value("octaves", 4));
		persistence->SetValue(parameters.value("persistence", 0.52));
		lacunarity->SetValue(parameters.value("lacunarity", 2.0));
		waterLevel->SetValue(parameters.value("waterLevel", -0.18));
		irregularity->SetValue(parameters.value("irregularity", 0.55));
		smoothing->SetValue(parameters.value("smoothingPasses", 3));
		decorationDensity->SetValue(parameters.value("decorationDensity", 18));
		pathWidth->SetValue(parameters.value("pathWidth", 2));
		roomCount->SetValue(parameters.value("roomCount", 12));
		roomMinSize->SetValue(parameters.value("roomMinSize", 5));
		roomMaxSize->SetValue(parameters.value("roomMaxSize", 13));
		loops->SetValue(parameters.value("loops", 2));
		roadSpacing->SetValue(parameters.value("secondaryRoadSpacing", 10));
		edgeMargin->SetValue(parameters.value("edgeMargin", 2));
		autoFixConnectivity->SetValue(parameters.value("autoFixConnectivity", true));
		std::vector<std::string> missingBrushes;
		const auto selectExact = [&missingBrushes](wxChoice* choice, const std::string& name) {
			const int index = choice->FindString(wxstr(name), true);
			choice->SetSelection(index == wxNOT_FOUND ? 0 : index);
			if (index == wxNOT_FOUND) missingBrushes.push_back(name);
		};
		if (json.contains("brushes")) {
			const auto& brushes = json["brushes"];
			for (size_t role = static_cast<size_t>(ProceduralMap::MaterialRole::Base); role < static_cast<size_t>(ProceduralMap::MaterialRole::Count); ++role) {
				const char* key = ProceduralMap::MaterialRoleName(static_cast<ProceduralMap::MaterialRole>(role));
				if (brushes.contains(key)) selectExact(groundChoices[role], brushes[key].get<std::string>());
			}
			if (brushes.contains("Wall")) selectExact(wallChoice, brushes["Wall"].get<std::string>());
			if (brushes.contains("Decoration")) selectExact(doodadChoice, brushes["Decoration"].get<std::string>());
			if (brushes.contains("Transition")) selectExact(transitionChoice, brushes["Transition"].get<std::string>());
		}
		if (!missingBrushes.empty()) {
			std::sort(missingBrushes.begin(), missingBrushes.end());
			missingBrushes.erase(std::unique(missingBrushes.begin(), missingBrushes.end()), missingBrushes.end());
			wxString message = "The active client does not provide these saved brushes:\n";
			for (const auto& name : missingBrushes) message += "\n- " + wxstr(name);
			message += "\n\nThey were left unselected. Choose compatible brushes from the active client before generating.";
			wxMessageBox(message, "Missing active-client brushes", wxOK | wxICON_WARNING, this);
		}
		// Invalidate preview before clearing plan
		if (previewPanel) {
			previewPanel->SetPlan(nullptr, {}, launchFloor);
		}
		plan.reset();
		applyButton->Enable(false);
		regenerateButton->Enable(false);
		statusText->SetLabel("Preset loaded and validated by brush name. Generate a new preview.");
	} catch (const std::exception& exception) {
		wxMessageBox(wxString::FromUTF8(exception.what()), "Load Preset", wxOK | wxICON_ERROR, this);
	}
}

void ProceduralGeneratorDialog::OnCancel(wxCommandEvent&) {
	closing = true;
	StopWorker();
	EndModal(wxID_CANCEL);
}

void ProceduralGeneratorDialog::OnClose(wxCloseEvent&) {
	closing = true;
	StopWorker();
	EndModal(wxID_CANCEL);
}

bool RunProceduralMapGenerator(wxWindow* parent, Editor& editor, int currentFloor) {
	try {
		ProceduralGeneratorDialog dialog(parent, editor, currentFloor);
		return dialog.ShowModal() == wxID_OK;
	} catch (const std::bad_alloc&) {
		wxMessageBox("Not enough memory to open the Procedural Map Generator for this selection.", "Procedural Map Generator", wxOK | wxICON_ERROR, parent);
	} catch (const std::exception& exception) {
		wxMessageBox(wxString("Could not open the Procedural Map Generator: ") + wxString::FromUTF8(exception.what()), "Procedural Map Generator", wxOK | wxICON_ERROR, parent);
	} catch (...) {
		wxMessageBox("Could not open the Procedural Map Generator because of an unexpected failure.", "Procedural Map Generator", wxOK | wxICON_ERROR, parent);
	}
	return false;
}
