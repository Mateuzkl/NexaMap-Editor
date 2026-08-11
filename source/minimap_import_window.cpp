#include "main.h"

#include "minimap_import_window.h"

#include "action.h"
#include "dcbutton.h"
#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "item.h"
#include "items.h"
#include "map_display.h"
#include "map_tab.h"
#include "minimap_import.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/progdlg.h>
#include <wx/radiobox.h>
#include <wx/spinctrl.h>

namespace {

	constexpr int overlayMode = 0;
	constexpr int newMapMode = 1;
	constexpr int mergeEmptyMode = 2;
	constexpr int replaceGroundMode = 3;

	wxColour MinimapColor(uint8_t color, uint8_t alpha = 255) {
		return wxColour(
			static_cast<uint8_t>((color / 36) % 6 * 51),
			static_cast<uint8_t>((color / 6) % 6 * 51),
			static_cast<uint8_t>(color % 6 * 51),
			alpha
		);
	}

	int ItemSpriteId(uint16_t itemId) {
		return itemId != 0 && g_items.typeExists(itemId) ? g_items[itemId].clientID : 0;
	}

	class MinimapPreviewPanel final : public wxPanel {
	public:
		MinimapPreviewPanel(wxWindow* parent, std::shared_ptr<const MinimapImportDocument> document) :
			wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(620, 360), wxBORDER_SIMPLE),
			document_(std::move(document)) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			Bind(wxEVT_PAINT, &MinimapPreviewPanel::OnPaint, this);
			Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
				if (!fitted_) {
					FitFloor();
				}
				event.Skip();
			});
			Bind(wxEVT_LEFT_DOWN, &MinimapPreviewPanel::OnLeftDown, this);
			Bind(wxEVT_LEFT_UP, &MinimapPreviewPanel::OnLeftUp, this);
			Bind(wxEVT_MOUSE_CAPTURE_LOST, &MinimapPreviewPanel::OnMouseCaptureLost, this);
			Bind(wxEVT_MOTION, &MinimapPreviewPanel::OnMotion, this);
			Bind(wxEVT_MOUSEWHEEL, &MinimapPreviewPanel::OnWheel, this);
		}

		void SetDocument(std::shared_ptr<const MinimapImportDocument> document) {
			document_ = std::move(document);
			fitted_ = false;
			FitFloor();
		}

		void SetFloor(int floor) {
			floor_ = floor;
			fitted_ = false;
			FitFloor();
		}

		void SetGrid(bool show) {
			showGrid_ = show;
			Refresh();
		}

	private:
		void FitFloor() {
			if (!document_) {
				return;
			}
			const auto& bounds = document_->getFloorInfo(floor_).bounds;
			const wxSize size = GetClientSize();
			if (!bounds.valid() || size.x <= 20 || size.y <= 20) {
				return;
			}
			centerX_ = (bounds.minX + bounds.maxX + 1) / 2.0;
			centerY_ = (bounds.minY + bounds.maxY + 1) / 2.0;
			const int mapWidth = std::max(1, bounds.width());
			const int mapHeight = std::max(1, bounds.height());
			zoom_ = std::clamp(std::min((size.x - 20.0) / mapWidth, (size.y - 20.0) / mapHeight), 0.02, 32.0);
			fitted_ = true;
			Refresh();
		}

		void OnPaint(wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			const wxSize size = GetClientSize();
			dc.SetBackground(wxBrush(wxColour(18, 20, 23)));
			dc.Clear();
			if (!document_ || !fitted_) {
				return;
			}
			const auto& bounds = document_->getFloorInfo(floor_).bounds;
			if (!bounds.valid()) {
				return;
			}

			if (zoom_ < 1.0) {
				wxImage image(std::max(1, size.x), std::max(1, size.y), false);
				image.SetRGB(wxRect(0, 0, image.GetWidth(), image.GetHeight()), 18, 20, 23);
				unsigned char* pixels = image.GetData();
				for (int py = 0; py < size.y; ++py) {
					const int mapY = static_cast<int>(std::floor(centerY_ + (py - size.y / 2.0) / zoom_));
					for (int px = 0; px < size.x; ++px) {
						const int mapX = static_cast<int>(std::floor(centerX_ + (px - size.x / 2.0) / zoom_));
						const MinimapTile* tile = document_->getTile(mapX, mapY, floor_);
						if (!tile) {
							continue;
						}
						const wxColour color = MinimapColor(tile->color);
						const size_t index = (static_cast<size_t>(py) * size.x + px) * 3;
						pixels[index] = color.Red();
						pixels[index + 1] = color.Green();
						pixels[index + 2] = color.Blue();
					}
				}
				dc.DrawBitmap(wxBitmap(image), 0, 0, false);
				return;
			}

			const int firstX = std::max(bounds.minX, static_cast<int>(std::floor(centerX_ - size.x / (2.0 * zoom_))) - 1);
			const int lastX = std::min(bounds.maxX, static_cast<int>(std::ceil(centerX_ + size.x / (2.0 * zoom_))) + 1);
			const int firstY = std::max(bounds.minY, static_cast<int>(std::floor(centerY_ - size.y / (2.0 * zoom_))) - 1);
			const int lastY = std::min(bounds.maxY, static_cast<int>(std::ceil(centerY_ + size.y / (2.0 * zoom_))) + 1);
			dc.SetPen(*wxTRANSPARENT_PEN);
			for (int y = firstY; y <= lastY; ++y) {
				for (int x = firstX; x <= lastX;) {
					const MinimapTile* tile = document_->getTile(x, y, floor_);
					if (!tile) {
						++x;
						continue;
					}
					int runEnd = x + 1;
					while (runEnd <= lastX) {
						const MinimapTile* next = document_->getTile(runEnd, y, floor_);
						if (!next || next->color != tile->color) {
							break;
						}
						++runEnd;
					}
					const int sx = static_cast<int>(std::floor((x - centerX_) * zoom_ + size.x / 2.0));
					const int sy = static_cast<int>(std::floor((y - centerY_) * zoom_ + size.y / 2.0));
					const int ex = static_cast<int>(std::ceil((runEnd - centerX_) * zoom_ + size.x / 2.0));
					const int ey = static_cast<int>(std::ceil((y + 1 - centerY_) * zoom_ + size.y / 2.0));
					dc.SetBrush(wxBrush(MinimapColor(tile->color)));
					dc.DrawRectangle(sx, sy, std::max(1, ex - sx), std::max(1, ey - sy));
					x = runEnd;
				}
			}
			if (showGrid_ && zoom_ >= 8.0) {
				dc.SetPen(wxPen(wxColour(255, 255, 255, 45)));
				for (int x = firstX; x <= lastX + 1; ++x) {
					const int sx = static_cast<int>(std::round((x - centerX_) * zoom_ + size.x / 2.0));
					dc.DrawLine(sx, 0, sx, size.y);
				}
				for (int y = firstY; y <= lastY + 1; ++y) {
					const int sy = static_cast<int>(std::round((y - centerY_) * zoom_ + size.y / 2.0));
					dc.DrawLine(0, sy, size.x, sy);
				}
			}
		}

		void OnLeftDown(wxMouseEvent& event) {
			dragging_ = true;
			lastMouse_ = event.GetPosition();
			CaptureMouse();
		}

		void OnLeftUp(wxMouseEvent&) {
			dragging_ = false;
			if (HasCapture()) {
				ReleaseMouse();
			}
		}

		void OnMouseCaptureLost(wxMouseCaptureLostEvent&) {
			dragging_ = false;
		}

		void OnMotion(wxMouseEvent& event) {
			if (!dragging_ || !event.Dragging()) {
				return;
			}
			const wxPoint current = event.GetPosition();
			centerX_ -= (current.x - lastMouse_.x) / zoom_;
			centerY_ -= (current.y - lastMouse_.y) / zoom_;
			lastMouse_ = current;
			Refresh();
		}

		void OnWheel(wxMouseEvent& event) {
			if (event.GetWheelRotation() == 0) {
				return;
			}
			const wxSize size = GetClientSize();
			const wxPoint mouse = event.GetPosition();
			const double beforeX = centerX_ + (mouse.x - size.x / 2.0) / zoom_;
			const double beforeY = centerY_ + (mouse.y - size.y / 2.0) / zoom_;
			const double factor = event.GetWheelRotation() > 0 ? 1.25 : 0.8;
			zoom_ = std::clamp(zoom_ * factor, 0.02, 64.0);
			centerX_ = beforeX - (mouse.x - size.x / 2.0) / zoom_;
			centerY_ = beforeY - (mouse.y - size.y / 2.0) / zoom_;
			Refresh();
		}

		std::shared_ptr<const MinimapImportDocument> document_;
		int floor_ = GROUND_LAYER;
		double centerX_ = 0.0;
		double centerY_ = 0.0;
		double zoom_ = 1.0;
		bool showGrid_ = true;
		bool fitted_ = false;
		bool dragging_ = false;
		wxPoint lastMouse_;
	};

	struct GroundCandidate {
		uint16_t serverId = 0;
		uint16_t clientId = 0;
		uint8_t minimapSpeed = 0;
		std::string name;
	};

	class MinimapImportDialog final : public wxDialog {
	public:
		MinimapImportDialog(wxWindow* parent, std::shared_ptr<MinimapImportDocument> document) :
			wxDialog(parent, wxID_ANY, "Import Minimap / OTMM", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			document_(std::move(document)) {
			BuildInterface();
			RefreshDocumentControls();
			AnalyzeColors();
			SetMinSize(FromDIP(wxSize(1050, 720)));
			SetSize(FromDIP(wxSize(1180, 800)));
			CentreOnParent();
		}

	private:
		void BuildInterface() {
			auto* root = newd wxBoxSizer(wxVERTICAL);
			auto* fileBox = newd wxStaticBoxSizer(wxVERTICAL, this, "OTMM source");
			auto* fileRow = newd wxBoxSizer(wxHORIZONTAL);
			fileLabel_ = newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, wxEmptyString);
			fileRow->Add(fileLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
			auto* browse = newd wxButton(fileBox->GetStaticBox(), wxID_OPEN, "Browse...");
			fileRow->Add(browse, 0);
			fileBox->Add(fileRow, 0, wxEXPAND | wxALL, FromDIP(6));
			summaryLabel_ = newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, wxEmptyString);
			fileBox->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
			root->Add(fileBox, 0, wxEXPAND | wxALL, FromDIP(8));

			auto* upper = newd wxBoxSizer(wxHORIZONTAL);
			preview_ = newd MinimapPreviewPanel(this, document_);
			upper->Add(preview_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
			auto* options = newd wxStaticBoxSizer(wxVERTICAL, this, "Import options");
			auto* floorRow = newd wxBoxSizer(wxHORIZONTAL);
			floorRow->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Floor:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			floorChoice_ = newd wxChoice(options->GetStaticBox(), wxID_ANY);
			floorRow->Add(floorChoice_, 1);
			options->Add(floorRow, 0, wxEXPAND | wxALL, FromDIP(7));
			gridCheck_ = newd wxCheckBox(options->GetStaticBox(), wxID_ANY, "Show preview grid");
			gridCheck_->SetValue(true);
			options->Add(gridCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			options->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Overlay opacity:"), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(7));
			opacitySlider_ = newd wxSlider(options->GetStaticBox(), wxID_ANY, 50, 10, 100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL | wxSL_LABELS);
			options->Add(opacitySlider_, 0, wxEXPAND | wxALL, FromDIP(7));
			const wxString modes[] = { "Reference Overlay", "Generate Editable Skeleton Map", "Merge Empty Tiles", "Replace Ground" };
			modeChoice_ = newd wxRadioBox(options->GetStaticBox(), wxID_ANY, "Apply mode", wxDefaultPosition, wxDefaultSize, WXSIZEOF(modes), modes, 1, wxRA_SPECIFY_COLS);
			options->Add(modeChoice_, 0, wxEXPAND | wxALL, FromDIP(7));
			if (!g_gui.GetCurrentEditor()) {
				modeChoice_->Enable(overlayMode, false);
				modeChoice_->Enable(mergeEmptyMode, false);
				modeChoice_->Enable(replaceGroundMode, false);
				modeChoice_->SetSelection(newMapMode);
			}
			analyzeButton_ = newd wxButton(options->GetStaticBox(), wxID_REFRESH, "Analyze Colors");
			options->Add(analyzeButton_, 0, wxEXPAND | wxALL, FromDIP(7));
			options->AddStretchSpacer();
			options->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Preview: mouse wheel zooms; drag to pan."), 0, wxALL, FromDIP(7));
			upper->Add(options, 0, wxEXPAND);
			root->Add(upper, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

			auto* lower = newd wxBoxSizer(wxHORIZONTAL);
			colorList_ = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(650, 235)), wxLC_REPORT | wxLC_SINGLE_SEL);
			colorList_->AppendColumn("Color", wxLIST_FORMAT_LEFT, FromDIP(70));
			colorList_->AppendColumn("Tiles", wxLIST_FORMAT_RIGHT, FromDIP(105));
			colorList_->AppendColumn("Speed", wxLIST_FORMAT_RIGHT, FromDIP(75));
			colorList_->AppendColumn("Suggested ground", wxLIST_FORMAT_LEFT, FromDIP(360));
			lower->Add(colorList_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
			auto* inspector = newd wxStaticBoxSizer(wxVERTICAL, this, "Selected color mapping");
			auto* previewRow = newd wxBoxSizer(wxHORIZONTAL);
			groundPreview_ = newd DCButton(inspector->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
			mappingLabel_ = newd wxStaticText(inspector->GetStaticBox(), wxID_ANY, "Select a minimap color.");
			previewRow->Add(groundPreview_, 0, wxRIGHT, FromDIP(8));
			previewRow->Add(mappingLabel_, 1, wxALIGN_CENTER_VERTICAL);
			inspector->Add(previewRow, 0, wxEXPAND | wxALL, FromDIP(7));
			candidateChoice_ = newd wxChoice(inspector->GetStaticBox(), wxID_ANY);
			inspector->Add(candidateChoice_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			useCandidateButton_ = newd wxButton(inspector->GetStaticBox(), wxID_APPLY, "Use selected candidate");
			inspector->Add(useCandidateButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			auto* manualRow = newd wxBoxSizer(wxHORIZONTAL);
			manualRow->Add(newd wxStaticText(inspector->GetStaticBox(), wxID_ANY, "Server ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			manualId_ = newd wxSpinCtrl(inspector->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::max<int>(0, g_items.getMaxID()), 0);
			manualRow->Add(manualId_, 1, wxRIGHT, FromDIP(6));
			manualButton_ = newd wxButton(inspector->GetStaticBox(), wxID_ANY, "Use ID");
			manualRow->Add(manualButton_, 0);
			inspector->Add(manualRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			lower->Add(inspector, 0, wxEXPAND);
			root->Add(lower, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

			auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
			analysisLabel_ = newd wxStaticText(this, wxID_ANY, wxEmptyString);
			buttons->Add(analysisLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
			applyButton_ = newd wxButton(this, wxID_OK, "Apply Import");
			buttons->Add(applyButton_, 0, wxRIGHT, FromDIP(6));
			buttons->Add(newd wxButton(this, wxID_CANCEL), 0);
			root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
			SetSizer(root);

			browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFile(); });
			floorChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnFloorChanged(); });
			gridCheck_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { preview_->SetGrid(gridCheck_->GetValue()); });
			analyzeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AnalyzeColors(); });
			colorList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
				selectedColor_ = static_cast<int>(colorList_->GetItemData(event.GetIndex()));
				RefreshInspector();
			});
			candidateChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshCandidatePreview(); });
			useCandidateButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseCandidate(); });
			manualButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseManualId(); });
			applyButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Apply(); });
		}

		void RefreshDocumentControls() {
			fileLabel_->SetLabel(wxstr(document_->getSourcePath()));
			const auto floors = document_->getAvailableFloors();
			wxString floorText;
			for (size_t index = 0; index < floors.size(); ++index) {
				if (index != 0) {
					floorText += ", ";
				}
				floorText += wxString::Format("%d", floors[index]);
			}
			const auto& bounds = document_->getBounds();
			summaryLabel_->SetLabel(wxString::Format(
				"OTMM v%u  |  Floors: %s  |  Bounds: X %d-%d, Y %d-%d  |  Seen tiles: %llu  |  Blocks: %zu",
				document_->getVersion(), floorText, bounds.minX, bounds.maxX, bounds.minY, bounds.maxY,
				static_cast<unsigned long long>(document_->getTileCount()), document_->getBlockCount()
			));
			floorChoice_->Clear();
			availableFloors_ = floors;
			for (const int floor : floors) {
				floorChoice_->Append(wxString::Format("%d", floor));
			}
			const int currentFloor = g_gui.GetCurrentEditor() ? g_gui.GetCurrentFloor() : GROUND_LAYER;
			auto found = std::find(floors.begin(), floors.end(), currentFloor);
			floorChoice_->SetSelection(found == floors.end() ? 0 : static_cast<int>(std::distance(floors.begin(), found)));
			OnFloorChanged();
		}

		void BrowseFile() {
			wxFileDialog dialog(this, "Import minimap", wxEmptyString, wxEmptyString, "OTMM minimap (*.otmm;*.otbmm)|*.otmm;*.otbmm|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			auto replacement = std::make_shared<MinimapImportDocument>();
			std::string error;
			if (!replacement->loadFromFile(nstr(dialog.GetPath()), error)) {
				wxMessageBox(wxstr(error), "Import Minimap", wxOK | wxICON_ERROR, this);
				return;
			}
			document_ = std::move(replacement);
			preview_->SetDocument(document_);
			RefreshDocumentControls();
			AnalyzeColors();
		}

		void OnFloorChanged() {
			const int selection = floorChoice_->GetSelection();
			if (selection >= 0 && selection < static_cast<int>(availableFloors_.size())) {
				preview_->SetFloor(availableFloors_[selection]);
			}
		}

		void AnalyzeColors() {
			for (auto& list : candidates_) {
				list.clear();
			}
			mappings_.fill(0);
			for (int id = 0; id <= g_items.getMaxID(); ++id) {
				if (!g_items.typeExists(id)) {
					continue;
				}
				ItemType& type = g_items[id];
				if (!type.isGroundTile() || !type.sprite || type.sprite->minimap_color > 255) {
					continue;
				}
				GroundCandidate candidate;
				candidate.serverId = static_cast<uint16_t>(id);
				candidate.clientId = type.clientID;
				candidate.minimapSpeed = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(std::ceil(type.sprite->ground_speed / 10.0))));
				candidate.name = type.name;
				candidates_[static_cast<uint8_t>(type.sprite->minimap_color)].push_back(std::move(candidate));
			}
			const auto& stats = document_->getColorStats();
			for (size_t color = 0; color < candidates_.size(); ++color) {
				const uint8_t speed = stats[color].dominantSpeed();
				auto& list = candidates_[color];
				std::sort(list.begin(), list.end(), [speed](const GroundCandidate& lhs, const GroundCandidate& rhs) {
					const int lhsDifference = std::abs(static_cast<int>(lhs.minimapSpeed) - speed);
					const int rhsDifference = std::abs(static_cast<int>(rhs.minimapSpeed) - speed);
					return lhsDifference != rhsDifference ? lhsDifference < rhsDifference : lhs.serverId < rhs.serverId;
				});
				if (stats[color].count != 0 && !list.empty()) {
					mappings_[color] = list.front().serverId;
				}
			}
			RefreshColorList();
		}

		void RefreshColorList() {
			const int colorToSelect = selectedColor_;
			colorList_->Freeze();
			colorList_->DeleteAllItems();
			uint64_t mappedTiles = 0;
			int unmatchedColors = 0;
			for (const uint8_t color : document_->getUsedColors()) {
				const auto& stats = document_->getColorStats()[color];
				const long row = colorList_->InsertItem(colorList_->GetItemCount(), wxString::Format("%u", color));
				colorList_->SetItemData(row, color);
				colorList_->SetItem(row, 1, wxString::Format("%llu", static_cast<unsigned long long>(stats.count)));
				colorList_->SetItem(row, 2, wxString::Format("%u", stats.dominantSpeed()));
				colorList_->SetItem(row, 3, MappingDescription(color));
				colorList_->SetItemBackgroundColour(row, MinimapColor(color));
				if (mappings_[color] != 0) {
					mappedTiles += stats.count;
				} else {
					++unmatchedColors;
				}
			}
			colorList_->Thaw();
			analysisLabel_->SetLabel(wxString::Format("Mapped tiles: %llu / %llu; unmatched colors: %d", static_cast<unsigned long long>(mappedTiles), static_cast<unsigned long long>(document_->getTileCount()), unmatchedColors));
			if (colorList_->GetItemCount() != 0) {
				long selectedRow = 0;
				for (long row = 0; row < colorList_->GetItemCount(); ++row) {
					if (colorList_->GetItemData(row) == colorToSelect) {
						selectedRow = row;
						break;
					}
				}
				colorList_->SetItemState(selectedRow, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				selectedColor_ = static_cast<int>(colorList_->GetItemData(selectedRow));
				RefreshInspector();
			}
		}

		wxString MappingDescription(uint8_t color) const {
			const uint16_t id = mappings_[color];
			if (id == 0 || !g_items.typeExists(id)) {
				return "Unmapped (will be skipped)";
			}
			const ItemType& type = g_items[id];
			return wxString::Format("%u - %s (Client %u)", id, wxstr(type.name.empty() ? std::string("Unnamed ground") : type.name), type.clientID);
		}

		void RefreshInspector() {
			candidateChoice_->Clear();
			currentCandidateIds_.clear();
			if (selectedColor_ < 0 || selectedColor_ > 255) {
				return;
			}
			const uint8_t color = static_cast<uint8_t>(selectedColor_);
			for (const auto& candidate : candidates_[color]) {
				candidateChoice_->Append(wxString::Format("%u - %s (speed %u)", candidate.serverId, wxstr(candidate.name.empty() ? std::string("Unnamed ground") : candidate.name), candidate.minimapSpeed));
				currentCandidateIds_.push_back(candidate.serverId);
			}
			const uint16_t mapped = mappings_[color];
			auto selected = std::find(currentCandidateIds_.begin(), currentCandidateIds_.end(), mapped);
			if (selected != currentCandidateIds_.end()) {
				candidateChoice_->SetSelection(static_cast<int>(std::distance(currentCandidateIds_.begin(), selected)));
			} else if (!currentCandidateIds_.empty()) {
				candidateChoice_->SetSelection(0);
			}
			manualId_->SetValue(mapped);
			mappingLabel_->SetLabel(wxString::Format("Minimap color %u\n%llu tiles, speed %u", color, static_cast<unsigned long long>(document_->getColorStats()[color].count), document_->getColorStats()[color].dominantSpeed()));
			RefreshCandidatePreview();
		}

		void RefreshCandidatePreview() {
			uint16_t id = 0;
			const int selection = candidateChoice_->GetSelection();
			if (selection >= 0 && selection < static_cast<int>(currentCandidateIds_.size())) {
				id = currentCandidateIds_[selection];
			} else if (selectedColor_ >= 0) {
				id = mappings_[selectedColor_];
			}
			groundPreview_->SetSprite(ItemSpriteId(id));
			useCandidateButton_->Enable(id != 0);
		}

		void UseCandidate() {
			const int selection = candidateChoice_->GetSelection();
			if (selectedColor_ < 0 || selection < 0 || selection >= static_cast<int>(currentCandidateIds_.size())) {
				return;
			}
			mappings_[selectedColor_] = currentCandidateIds_[selection];
			manualId_->SetValue(mappings_[selectedColor_]);
			RefreshColorList();
		}

		void UseManualId() {
			if (selectedColor_ < 0) {
				return;
			}
			const int id = manualId_->GetValue();
			if (id == 0) {
				mappings_[selectedColor_] = 0;
				RefreshColorList();
				return;
			}
			if (!g_items.typeExists(id) || !g_items[id].isGroundTile()) {
				wxMessageBox("The selected Server ID is not a valid ground item.", "Import Minimap", wxOK | wxICON_WARNING, this);
				return;
			}
			mappings_[selectedColor_] = static_cast<uint16_t>(id);
			RefreshColorList();
		}

		uint64_t CountMappedTiles() const {
			uint64_t count = 0;
			for (size_t color = 0; color < mappings_.size(); ++color) {
				if (mappings_[color] != 0) {
					count += document_->getColorStats()[color].count;
				}
			}
			return count;
		}

		void Apply() {
			const int mode = modeChoice_->GetSelection();
			wxLogMessage(wxString::Format("Minimap import: applying mode %d with %llu seen tiles.", mode, static_cast<unsigned long long>(document_->getTileCount())));
			if (mode == overlayMode) {
				ApplyOverlay();
				return;
			}
			const uint64_t mapped = CountMappedTiles();
			if (mapped == 0) {
				wxMessageBox("No minimap color is mapped to a ground item.", "Import Minimap", wxOK | wxICON_WARNING, this);
				return;
			}
			if (mode != newMapMode && !g_gui.GetCurrentEditor()) {
				wxMessageBox("Open a map before using this import mode.", "Import Minimap", wxOK | wxICON_WARNING, this);
				return;
			}
			const wxString operation = mode == newMapMode ? "create a new editable skeleton map" : (mode == mergeEmptyMode ? "add grounds only where the current map has no ground" : "replace grounds while preserving other items");
			if (wxMessageBox(wxString::Format("This will %s using up to %llu mapped minimap tiles.\n\nContinue?", operation, static_cast<unsigned long long>(mapped)), "Confirm Minimap Import", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
				return;
			}
			if (mode == newMapMode) {
				ApplyNewMap();
			} else {
				ApplyToCurrentMap(mode);
			}
		}

		void ApplyOverlay() {
			MapTab* tab = g_gui.GetCurrentMapTab();
			if (!tab || !tab->GetCanvas()) {
				wxMessageBox("Open a map before attaching a reference overlay.", "Import Minimap", wxOK | wxICON_WARNING, this);
				return;
			}
			tab->GetCanvas()->SetMinimapImportOverlay(document_, static_cast<uint8_t>(std::round(opacitySlider_->GetValue() * 255.0 / 100.0)));
			FocusImportedArea(tab->GetCanvas());
			g_gui.UpdateMenus();
			EndModal(wxID_OK);
		}

		void ApplyNewMap() {
			if (!g_gui.NewMap()) {
				return;
			}
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor) {
				return;
			}
			wxProgressDialog progress("Import Minimap", "Generating editable skeleton map...", 100, this, wxPD_APP_MODAL | wxPD_AUTO_HIDE);
			uint64_t processed = 0;
			uint64_t changed = 0;
			try {
				document_->forEachSeenTile([&](int x, int y, int z, const MinimapTile& tile) {
					++processed;
					if ((processed & 4095) == 0) {
						progress.Update(static_cast<int>(processed * 100 / document_->getTileCount()));
					}
					const uint16_t groundId = mappings_[tile.color];
					if (groundId != 0) {
						Tile* mapTile = editor->map.getOrCreateTile(Position(x, y, z));
						mapTile->addItem(Item::Create(groundId));
						mapTile->update();
						++changed;
					}
				});
			} catch (const std::exception& exception) {
				editor->map.clear(true);
				editor->map.clearChanges();
				wxMessageBox(wxString::Format("Import failed: %s\nThe incomplete skeleton map was cleared.", wxString::FromUTF8(exception.what())), "Import Minimap", wxOK | wxICON_ERROR, this);
				return;
			} catch (...) {
				editor->map.clear(true);
				editor->map.clearChanges();
				wxMessageBox("Import failed unexpectedly. The incomplete skeleton map was cleared.", "Import Minimap", wxOK | wxICON_ERROR, this);
				return;
			}
			progress.Update(100);
			const auto& bounds = document_->getBounds();
			editor->map.setWidth(std::min(MAP_MAX_WIDTH, bounds.maxX + 1));
			editor->map.setHeight(std::min(MAP_MAX_HEIGHT, bounds.maxY + 1));
			editor->map.setName("Imported Minimap Skeleton");
			editor->map.setMapDescription("Editable skeleton generated from " + document_->getSourcePath());
			editor->map.doChange();
			FinishApplyView();
			wxMessageBox(wxString::Format("Created %llu editable ground tiles.\nUse File > Save As to save the map as .otbm.", static_cast<unsigned long long>(changed)), "Minimap Import Complete", wxOK | wxICON_INFORMATION, this);
			EndModal(wxID_OK);
		}

		void ApplyToCurrentMap(int mode) {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor) {
				return;
			}
			std::unique_ptr<BatchAction> batch(editor->actionQueue->createBatch(ACTION_IMPORT_MINIMAP));
			std::unique_ptr<Action> action(editor->actionQueue->createAction(batch.get()));
			wxProgressDialog progress("Import Minimap", "Applying minimap grounds...", 100, this, wxPD_APP_MODAL | wxPD_AUTO_HIDE);
			uint64_t processed = 0;
			uint64_t changed = 0;
			try {
				document_->forEachSeenTile([&](int x, int y, int z, const MinimapTile& tile) {
					++processed;
					if ((processed & 4095) == 0) {
						progress.Update(static_cast<int>(processed * 100 / document_->getTileCount()));
					}
					const uint16_t groundId = mappings_[tile.color];
					if (groundId == 0) {
						return;
					}
					const Position position(x, y, z);
					TileLocation* location = editor->map.createTileL(position);
					Tile* oldTile = location->get();
					if (mode == mergeEmptyMode && oldTile && oldTile->ground) {
						return;
					}
					std::unique_ptr<Tile> newTile(oldTile ? oldTile->deepCopy(editor->map) : editor->map.allocator(location));
					newTile->addItem(Item::Create(groundId));
					newTile->update();
					action->addChange(newd Change(newTile.release()));
					++changed;
					if (action->size() >= 4096) {
						batch->addAndCommitAction(action.release());
						action.reset(editor->actionQueue->createAction(batch.get()));
					}
				});
				batch->addAndCommitAction(action.release());
				if (batch->size() == 0) {
					wxMessageBox("No eligible current-map tile changed.", "Import Minimap", wxOK | wxICON_INFORMATION, this);
					return;
				}
				editor->actionQueue->addBatch(batch.release());
			} catch (const std::exception& exception) {
				if (batch) {
					batch->rollback();
				}
				wxMessageBox(wxString::Format("Import failed: %s\nAll committed chunks were rolled back.", wxString::FromUTF8(exception.what())), "Import Minimap", wxOK | wxICON_ERROR, this);
				return;
			}
			progress.Update(100);
			const auto& bounds = document_->getBounds();
			editor->map.setWidth(std::min(MAP_MAX_WIDTH, std::max(editor->map.getWidth(), bounds.maxX + 1)));
			editor->map.setHeight(std::min(MAP_MAX_HEIGHT, std::max(editor->map.getHeight(), bounds.maxY + 1)));
			editor->map.doChange();
			FinishApplyView();
			wxMessageBox(wxString::Format("Applied %llu ground tiles. The operation can be undone.", static_cast<unsigned long long>(changed)), "Minimap Import Complete", wxOK | wxICON_INFORMATION, this);
			EndModal(wxID_OK);
		}

		void FocusImportedArea(MapCanvas* canvas) {
			const int selection = floorChoice_->GetSelection();
			const int targetFloor = selection >= 0 && selection < static_cast<int>(availableFloors_.size()) ? availableFloors_[selection] : availableFloors_.front();
			const auto& bounds = document_->getFloorInfo(targetFloor).bounds;
			if (!bounds.valid()) {
				return;
			}
			g_gui.SetScreenCenterPosition(Position((bounds.minX + bounds.maxX) / 2, (bounds.minY + bounds.maxY) / 2, targetFloor), false);
			int width = 1;
			int height = 1;
			canvas->GetClientSize(&width, &height);
			const double fitZoom = std::max(bounds.width() * static_cast<double>(TileSize) / std::max(1, width), bounds.height() * static_cast<double>(TileSize) / std::max(1, height));
			canvas->SetZoom(std::clamp(fitZoom, 0.125, 25.0));
			canvas->RefreshViewport();
		}

		void FinishApplyView() {
			MapTab* tab = g_gui.GetCurrentMapTab();
			if (tab && tab->GetCanvas()) {
				FocusImportedArea(tab->GetCanvas());
			}
			g_gui.RefreshView();
			g_gui.UpdateMinimap(true);
			g_gui.UpdateTitle();
			g_gui.UpdateMenus();
		}

		std::shared_ptr<MinimapImportDocument> document_;
		std::array<std::vector<GroundCandidate>, 256> candidates_;
		std::array<uint16_t, 256> mappings_ {};
		std::vector<uint16_t> currentCandidateIds_;
		std::vector<int> availableFloors_;
		int selectedColor_ = -1;
		wxStaticText* fileLabel_ = nullptr;
		wxStaticText* summaryLabel_ = nullptr;
		MinimapPreviewPanel* preview_ = nullptr;
		wxChoice* floorChoice_ = nullptr;
		wxCheckBox* gridCheck_ = nullptr;
		wxSlider* opacitySlider_ = nullptr;
		wxRadioBox* modeChoice_ = nullptr;
		wxButton* analyzeButton_ = nullptr;
		wxListCtrl* colorList_ = nullptr;
		DCButton* groundPreview_ = nullptr;
		wxStaticText* mappingLabel_ = nullptr;
		wxChoice* candidateChoice_ = nullptr;
		wxButton* useCandidateButton_ = nullptr;
		wxSpinCtrl* manualId_ = nullptr;
		wxButton* manualButton_ = nullptr;
		wxStaticText* analysisLabel_ = nullptr;
		wxButton* applyButton_ = nullptr;
	};

} // namespace

bool RunMinimapImport(wxWindow* parent) {
	wxFileDialog fileDialog(parent, "Import minimap", wxEmptyString, wxEmptyString, "OTMM minimap (*.otmm;*.otbmm)|*.otmm;*.otbmm|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (fileDialog.ShowModal() != wxID_OK) {
		return false;
	}
	wxLogMessage("Minimap import: loading " + fileDialog.GetPath());
	auto document = std::make_shared<MinimapImportDocument>();
	std::string error;
	if (!document->loadFromFile(nstr(fileDialog.GetPath()), error)) {
		wxLogWarning("Minimap import failed: " + wxstr(error));
		wxMessageBox(wxstr(error), "Import Minimap", wxOK | wxICON_ERROR, parent);
		return false;
	}
	wxLogMessage(wxString::Format("Minimap import: parsed %llu seen tiles in %zu blocks; opening preview.", static_cast<unsigned long long>(document->getTileCount()), document->getBlockCount()));
	MinimapImportDialog dialog(parent, std::move(document));
	const bool applied = dialog.ShowModal() == wxID_OK;
	wxLogMessage(applied ? "Minimap import: preview accepted." : "Minimap import: preview cancelled.");
	return applied;
}
