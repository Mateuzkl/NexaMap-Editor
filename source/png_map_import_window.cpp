#include "main.h"

#include "png_map_import_window.h"

#include "action.h"
#include "dcbutton.h"
#include "editor.h"
#include "gui.h"
#include "item.h"
#include "items.h"
#include "map_display.h"
#include "map_tab.h"
#include "png_map_import.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <numeric>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/progdlg.h>
#include <wx/radiobox.h>
#include <wx/spinctrl.h>

namespace {

	constexpr int newMapMode = 0;
	constexpr int mergeEmptyMode = 1;
	constexpr int replaceGroundMode = 2;
	constexpr size_t maximumExactColors = 256;

	struct GroundCandidate {
		uint16_t serverId = 0;
		uint16_t clientId = 0;
		uint32_t rgb = 0;
		std::string name;
	};

	wxColour RgbColour(uint32_t rgb) {
		return wxColour(static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF), static_cast<uint8_t>(rgb & 0xFF));
	}

	uint32_t MinimapRgb(uint8_t color) {
		const uint32_t red = (color / 36) % 6 * 51;
		const uint32_t green = (color / 6) % 6 * 51;
		const uint32_t blue = color % 6 * 51;
		return (red << 16) | (green << 8) | blue;
	}

	int ColorDistance(uint32_t left, uint32_t right) {
		const int red = static_cast<int>((left >> 16) & 0xFF) - static_cast<int>((right >> 16) & 0xFF);
		const int green = static_cast<int>((left >> 8) & 0xFF) - static_cast<int>((right >> 8) & 0xFF);
		const int blue = static_cast<int>(left & 0xFF) - static_cast<int>(right & 0xFF);
		return red * red + green * green + blue * blue;
	}

	int ItemSpriteId(uint16_t itemId) {
		return itemId != 0 && g_items.typeExists(itemId) ? g_items[itemId].clientID : 0;
	}

	bool LoadPng(const wxString& path, PngMapImportDocument& document, std::string& error) {
		wxImage image;
		if (!image.LoadFile(path, wxBITMAP_TYPE_PNG) || !image.IsOk()) {
			error = "Could not decode the selected PNG file.";
			return false;
		}
		const int width = image.GetWidth();
		const int height = image.GetHeight();
		if (width <= 0 || height <= 0) {
			error = "PNG has invalid dimensions.";
			return false;
		}
		const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
		if (pixelCount > 25'000'000) {
			error = "PNG is too large. The native importer accepts at most 25,000,000 pixels.";
			return false;
		}
		const unsigned char* rgb = image.GetData();
		const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
		const bool hasMask = image.HasMask();
		const unsigned char maskRed = hasMask ? image.GetMaskRed() : 0;
		const unsigned char maskGreen = hasMask ? image.GetMaskGreen() : 0;
		const unsigned char maskBlue = hasMask ? image.GetMaskBlue() : 0;
		std::vector<PngImportPixel> pixels(static_cast<size_t>(pixelCount));
		for (size_t index = 0; index < pixels.size(); ++index) {
			PngImportPixel& pixel = pixels[index];
			pixel.red = rgb[index * 3];
			pixel.green = rgb[index * 3 + 1];
			pixel.blue = rgb[index * 3 + 2];
			pixel.alpha = alpha ? alpha[index] : 255;
			if (!alpha && hasMask && pixel.red == maskRed && pixel.green == maskGreen && pixel.blue == maskBlue) {
				pixel.alpha = 0;
			}
		}
		return document.setPixels(width, height, std::move(pixels), error);
	}

	bool LimitColorCount(wxWindow* parent, PngMapImportDocument& document) {
		if (document.getColors().size() <= maximumExactColors) {
			return true;
		}
		const int result = wxMessageBox(
			wxString::Format(
				"This PNG contains %llu opaque colors, but exact mode supports at most %llu.\n\nSimplify the colors now?",
				static_cast<unsigned long long>(document.getColors().size()),
				static_cast<unsigned long long>(maximumExactColors)
			),
			"Simplify PNG Colors", wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION, parent
		);
		if (result != wxYES) {
			return false;
		}
		std::string error;
		if (!document.quantizeColors(6, error)) {
			wxMessageBox(wxstr(error), "Import PNG", wxOK | wxICON_ERROR, parent);
			return false;
		}
		return true;
	}

	wxImage DocumentImage(const PngMapImportDocument& document) {
		wxImage image(document.getWidth(), document.getHeight(), false);
		image.InitAlpha();
		unsigned char* rgb = image.GetData();
		unsigned char* alpha = image.GetAlpha();
		const auto& pixels = document.getPixels();
		for (size_t index = 0; index < pixels.size(); ++index) {
			rgb[index * 3] = pixels[index].red;
			rgb[index * 3 + 1] = pixels[index].green;
			rgb[index * 3 + 2] = pixels[index].blue;
			alpha[index] = pixels[index].alpha;
		}
		return image;
	}

	class PngPreviewPanel final : public wxPanel {
	public:
		PngPreviewPanel(wxWindow* parent, const PngMapImportDocument& document) :
			wxPanel(parent, wxID_ANY, wxDefaultPosition, parent ? parent->FromDIP(wxSize(620, 360)) : wxSize(620, 360), wxBORDER_SIMPLE),
			image_(DocumentImage(document)) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			Bind(wxEVT_PAINT, &PngPreviewPanel::OnPaint, this);
		}

		void SetDocument(const PngMapImportDocument& document) {
			image_ = DocumentImage(document);
			Refresh();
		}

	private:
		void OnPaint(wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			const wxSize size = GetClientSize();
			dc.SetBackground(wxBrush(wxColour(24, 26, 30)));
			dc.Clear();
			if (!image_.IsOk() || size.x <= 4 || size.y <= 4) {
				return;
			}
			const double scale = std::min((size.x - 12.0) / image_.GetWidth(), (size.y - 12.0) / image_.GetHeight());
			const int width = std::max(1, static_cast<int>(std::round(image_.GetWidth() * scale)));
			const int height = std::max(1, static_cast<int>(std::round(image_.GetHeight() * scale)));
			const wxImage scaled = image_.Scale(width, height, wxIMAGE_QUALITY_NEAREST);
			const int x = (size.x - width) / 2;
			const int y = (size.y - height) / 2;
			dc.DrawBitmap(wxBitmap(scaled), x, y, true);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(95, 100, 108)));
			dc.DrawRectangle(x - 1, y - 1, width + 2, height + 2);
		}

		wxImage image_;
	};

	class PngMapImportDialog final : public wxDialog {
	public:
		PngMapImportDialog(wxWindow* parent, wxString sourcePath, std::shared_ptr<PngMapImportDocument> document) :
			wxDialog(parent, wxID_ANY, "Import PNG as OTBM Map", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			sourcePath_(std::move(sourcePath)),
			document_(std::move(document)) {
			BuildGroundCatalog();
			BuildInterface();
			RefreshDocument();
			SetMinSize(FromDIP(wxSize(1050, 700)));
			SetSize(FromDIP(wxSize(1180, 790)));
			CentreOnParent();
		}

		bool ShouldSaveAfterApply() const {
			return saveAfterApply_;
		}

	private:
		void BuildGroundCatalog() {
			for (int id = 1; id <= g_items.getMaxID(); ++id) {
				if (!g_items.typeExists(id)) {
					continue;
				}
				const ItemType& type = g_items[id];
				if (!type.isGroundTile() || !type.sprite || type.sprite->minimap_color > 255) {
					continue;
				}
				grounds_.push_back({ static_cast<uint16_t>(id), type.clientID, MinimapRgb(static_cast<uint8_t>(type.sprite->minimap_color)), type.name });
			}
		}

		void BuildInterface() {
			auto* root = newd wxBoxSizer(wxVERTICAL);
			auto* sourceBox = newd wxStaticBoxSizer(wxVERTICAL, this, "PNG source");
			auto* sourceRow = newd wxBoxSizer(wxHORIZONTAL);
			fileLabel_ = newd wxStaticText(sourceBox->GetStaticBox(), wxID_ANY, sourcePath_);
			sourceRow->Add(fileLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
			auto* browseButton = newd wxButton(sourceBox->GetStaticBox(), wxID_OPEN, "Browse...");
			sourceRow->Add(browseButton, 0);
			sourceBox->Add(sourceRow, 0, wxEXPAND | wxALL, FromDIP(6));
			summaryLabel_ = newd wxStaticText(sourceBox->GetStaticBox(), wxID_ANY, wxEmptyString);
			sourceBox->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
			root->Add(sourceBox, 0, wxEXPAND | wxALL, FromDIP(8));

			auto* upper = newd wxBoxSizer(wxHORIZONTAL);
			preview_ = newd PngPreviewPanel(this, *document_);
			upper->Add(preview_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
			auto* options = newd wxStaticBoxSizer(wxVERTICAL, this, "Map options");
			const wxString modes[] = { "New Editable Map", "Merge Empty Tiles", "Replace Ground" };
			modeChoice_ = newd wxRadioBox(options->GetStaticBox(), wxID_ANY, "Apply mode", wxDefaultPosition, wxDefaultSize, WXSIZEOF(modes), modes, 1, wxRA_SPECIFY_COLS);
			if (!g_gui.GetCurrentEditor()) {
				modeChoice_->Enable(mergeEmptyMode, false);
				modeChoice_->Enable(replaceGroundMode, false);
			}
			options->Add(modeChoice_, 0, wxEXPAND | wxALL, FromDIP(6));

			auto* grid = newd wxFlexGridSizer(2, FromDIP(5), FromDIP(7));
			grid->AddGrowableCol(1);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Floor Z:"), 0, wxALIGN_CENTER_VERTICAL);
			floor_ = newd wxSpinCtrl(options->GetStaticBox(), wxID_ANY, "7", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, GROUND_LAYER);
			grid->Add(floor_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Offset X:"), 0, wxALIGN_CENTER_VERTICAL);
			offsetX_ = newd wxSpinCtrl(options->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAP_MAX_WIDTH - 1, 0);
			grid->Add(offsetX_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Offset Y:"), 0, wxALIGN_CENTER_VERTICAL);
			offsetY_ = newd wxSpinCtrl(options->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAP_MAX_HEIGHT - 1, 0);
			grid->Add(offsetY_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Scale:"), 0, wxALIGN_CENTER_VERTICAL);
			scale_ = newd wxSpinCtrl(options->GetStaticBox(), wxID_ANY, "100", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 100, 100);
			grid->Add(scale_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Rotation:"), 0, wxALIGN_CENTER_VERTICAL);
			rotation_ = newd wxChoice(options->GetStaticBox(), wxID_ANY);
			rotation_->Append("0 degrees");
			rotation_->Append("90 degrees");
			rotation_->Append("180 degrees");
			rotation_->Append("270 degrees");
			rotation_->SetSelection(0);
			grid->Add(rotation_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Transparent ground:"), 0, wxALIGN_CENTER_VERTICAL);
			transparentId_ = newd wxSpinCtrl(options->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::max<int>(0, g_items.getMaxID()), 0);
			transparentId_->SetToolTip("Server ID 0 skips transparent pixels");
			grid->Add(transparentId_, 1, wxEXPAND);
			grid->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "Color palette:"), 0, wxALIGN_CENTER_VERTICAL);
			palette_ = newd wxChoice(options->GetStaticBox(), wxID_ANY);
			palette_->Append("Exact colors");
			palette_->Append("Up to 216 colors");
			palette_->Append("Up to 125 colors");
			palette_->Append("Up to 64 colors");
			palette_->Append("Up to 27 colors");
			palette_->Append("Up to 8 colors");
			palette_->SetSelection(document_->hasSimplifiedColors() ? 1 : 0);
			grid->Add(palette_, 1, wxEXPAND);
			options->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));

			flipHorizontal_ = newd wxCheckBox(options->GetStaticBox(), wxID_ANY, "Flip horizontal");
			flipVertical_ = newd wxCheckBox(options->GetStaticBox(), wxID_ANY, "Flip vertical");
			saveAfter_ = newd wxCheckBox(options->GetStaticBox(), wxID_ANY, "Save new map as .otbm after import");
			saveAfter_->SetValue(true);
			options->Add(flipHorizontal_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			options->Add(flipVertical_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			options->Add(saveAfter_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			options->AddStretchSpacer();
			options->Add(newd wxStaticText(options->GetStaticBox(), wxID_ANY, "One PNG pixel becomes one real ground tile at 100% scale."), 0, wxALL, FromDIP(7));
			upper->Add(options, 0, wxEXPAND);
			root->Add(upper, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

			auto* mappingHeader = newd wxBoxSizer(wxHORIZONTAL);
			mappingHeader->Add(newd wxStaticText(this, wxID_ANY, "Color to real ground mapping"), 1, wxALIGN_CENTER_VERTICAL);
			autoMapButton_ = newd wxButton(this, wxID_REFRESH, "Auto-map All");
			mappingHeader->Add(autoMapButton_, 0);
			root->Add(mappingHeader, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

			auto* lower = newd wxBoxSizer(wxHORIZONTAL);
			colorList_ = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(650, 220)), wxLC_REPORT | wxLC_SINGLE_SEL);
			colorList_->AppendColumn("Color", wxLIST_FORMAT_LEFT, FromDIP(100));
			colorList_->AppendColumn("Pixels", wxLIST_FORMAT_RIGHT, FromDIP(105));
			colorList_->AppendColumn("Server ID", wxLIST_FORMAT_RIGHT, FromDIP(90));
			colorList_->AppendColumn("Ground", wxLIST_FORMAT_LEFT, FromDIP(330));
			lower->Add(colorList_, 1, wxEXPAND | wxRIGHT, FromDIP(8));

			auto* inspector = newd wxStaticBoxSizer(wxVERTICAL, this, "Selected color");
			auto* previewRow = newd wxBoxSizer(wxHORIZONTAL);
			groundPreview_ = newd DCButton(inspector->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
			mappingLabel_ = newd wxStaticText(inspector->GetStaticBox(), wxID_ANY, "Select a PNG color.");
			previewRow->Add(groundPreview_, 0, wxRIGHT, FromDIP(8));
			previewRow->Add(mappingLabel_, 1, wxALIGN_CENTER_VERTICAL);
			inspector->Add(previewRow, 0, wxEXPAND | wxALL, FromDIP(7));
			candidateChoice_ = newd wxChoice(inspector->GetStaticBox(), wxID_ANY);
			candidateChoice_->SetToolTip("Closest grounds by loaded client minimap color");
			inspector->Add(candidateChoice_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			useCandidateButton_ = newd wxButton(inspector->GetStaticBox(), wxID_APPLY, "Use Suggested Ground");
			inspector->Add(useCandidateButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			auto* manualRow = newd wxBoxSizer(wxHORIZONTAL);
			manualRow->Add(newd wxStaticText(inspector->GetStaticBox(), wxID_ANY, "Server ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			manualId_ = newd wxSpinCtrl(inspector->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::max<int>(0, g_items.getMaxID()), 0);
			manualRow->Add(manualId_, 1, wxRIGHT, FromDIP(6));
			manualButton_ = newd wxButton(inspector->GetStaticBox(), wxID_ANY, "Use ID");
			manualRow->Add(manualButton_, 0);
			inspector->Add(manualRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			skipButton_ = newd wxButton(inspector->GetStaticBox(), wxID_ANY, "Skip This Color");
			inspector->Add(skipButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
			lower->Add(inspector, 0, wxEXPAND);
			root->Add(lower, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

			auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
			mappingSummary_ = newd wxStaticText(this, wxID_ANY, wxEmptyString);
			buttons->Add(mappingSummary_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
			applyButton_ = newd wxButton(this, wxID_OK, "Generate Map");
			buttons->Add(applyButton_, 0, wxRIGHT, FromDIP(6));
			buttons->Add(newd wxButton(this, wxID_CANCEL), 0);
			root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
			SetSizer(root);

			browseButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Browse(); });
			palette_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ChangePalette(); });
			modeChoice_->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) { saveAfter_->Enable(modeChoice_->GetSelection() == newMapMode); });
			autoMapButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AutoMapAll(); });
			colorList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
				selectedColor_ = static_cast<uint32_t>(colorList_->GetItemData(event.GetIndex()));
				RefreshInspector();
			});
			candidateChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshCandidatePreview(); });
			useCandidateButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseCandidate(); });
			manualButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseManualId(); });
			skipButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SkipColor(); });
			applyButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Apply(); });
		}

		void Browse() {
			wxFileDialog dialog(this, "Import PNG", wxEmptyString, wxEmptyString, "PNG images (*.png)|*.png", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			auto replacement = std::make_shared<PngMapImportDocument>();
			std::string error;
			if (!LoadPng(dialog.GetPath(), *replacement, error) || !LimitColorCount(this, *replacement)) {
				if (!error.empty()) {
					wxMessageBox(wxstr(error), "Import PNG", wxOK | wxICON_ERROR, this);
				}
				return;
			}
			sourcePath_ = dialog.GetPath();
			document_ = std::move(replacement);
			palette_->SetSelection(document_->hasSimplifiedColors() ? 1 : 0);
			RefreshDocument();
		}

		void ChangePalette() {
			const int selection = palette_->GetSelection();
			if (selection == 0) {
				document_->restoreOriginalColors();
				if (document_->getColors().size() > maximumExactColors) {
					wxMessageBox(wxString::Format("Exact mode supports at most %llu colors. The palette was simplified so every color remains editable.", static_cast<unsigned long long>(maximumExactColors)), "PNG Color Palette", wxOK | wxICON_INFORMATION, this);
					std::string error;
					document_->quantizeColors(6, error);
					palette_->SetSelection(1);
				}
			} else {
				static constexpr uint8_t levels[] = { 6, 5, 4, 3, 2 };
				std::string error;
				if (!document_->quantizeColors(levels[selection - 1], error)) {
					wxMessageBox(wxstr(error), "PNG Color Palette", wxOK | wxICON_ERROR, this);
					return;
				}
			}
			RefreshDocument();
		}

		void RefreshDocument() {
			fileLabel_->SetLabel(sourcePath_);
			summaryLabel_->SetLabel(wxString::Format(
				"%d x %d pixels  |  %llu opaque colors  |  %llu transparent pixels",
				document_->getWidth(), document_->getHeight(), static_cast<unsigned long long>(document_->getColors().size()),
				static_cast<unsigned long long>(document_->getTransparentPixelCount())
			));
			preview_->SetDocument(*document_);
			AutoMapAll();
		}

		void AutoMapAll() {
			mappings_.clear();
			if (!grounds_.empty()) {
				for (const PngImportColor& color : document_->getColors()) {
					const auto closest = std::min_element(grounds_.begin(), grounds_.end(), [&color](const GroundCandidate& left, const GroundCandidate& right) {
						const int leftDistance = ColorDistance(color.rgb, left.rgb);
						const int rightDistance = ColorDistance(color.rgb, right.rgb);
						return leftDistance != rightDistance ? leftDistance < rightDistance : left.serverId < right.serverId;
					});
					mappings_[color.rgb] = closest->serverId;
				}
			}
			RefreshColorList();
		}

		wxString GroundDescription(uint16_t id) const {
			if (id == 0 || !g_items.typeExists(id)) {
				return "Skipped";
			}
			const ItemType& type = g_items[id];
			return wxString::Format("%s (Client %u)", wxstr(type.name.empty() ? std::string("Unnamed ground") : type.name), type.clientID);
		}

		void RefreshColorList() {
			colorList_->Freeze();
			colorList_->DeleteAllItems();
			uint64_t mappedPixels = 0;
			int mappedColors = 0;
			long selectionRow = wxNOT_FOUND;
			for (const PngImportColor& color : document_->getColors()) {
				const uint16_t id = mappings_.contains(color.rgb) ? mappings_[color.rgb] : 0;
				const long row = colorList_->InsertItem(colorList_->GetItemCount(), wxString::Format("#%06X", color.rgb));
				colorList_->SetItemData(row, static_cast<long>(color.rgb));
				colorList_->SetItem(row, 1, wxString::Format("%llu", static_cast<unsigned long long>(color.count)));
				colorList_->SetItem(row, 2, id == 0 ? wxString("-") : wxString::Format("%u", id));
				colorList_->SetItem(row, 3, GroundDescription(id));
				const wxColour background = RgbColour(color.rgb);
				colorList_->SetItemBackgroundColour(row, background);
				const int luminance = (background.Red() * 299 + background.Green() * 587 + background.Blue() * 114) / 1000;
				colorList_->SetItemTextColour(row, luminance >= 145 ? *wxBLACK : *wxWHITE);
				if (id != 0) {
					mappedPixels += color.count;
					++mappedColors;
				}
				if (selectedColor_ == color.rgb) {
					selectionRow = row;
				}
			}
			colorList_->Thaw();
			mappingSummary_->SetLabel(wxString::Format(
				"Mapped: %d/%llu colors, %llu pixels. Alpha pixels: %llu.", mappedColors,
				static_cast<unsigned long long>(document_->getColors().size()), static_cast<unsigned long long>(mappedPixels),
				static_cast<unsigned long long>(document_->getTransparentPixelCount())
			));
			if (colorList_->GetItemCount() > 0) {
				if (selectionRow == wxNOT_FOUND) {
					selectionRow = 0;
					selectedColor_ = static_cast<uint32_t>(colorList_->GetItemData(0));
				}
				colorList_->SetItemState(selectionRow, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				RefreshInspector();
			}
		}

		void RefreshInspector() {
			candidateChoice_->Clear();
			candidateIds_.clear();
			std::vector<size_t> order(grounds_.size());
			std::iota(order.begin(), order.end(), 0);
			const size_t candidateCount = std::min<size_t>(64, order.size());
			std::partial_sort(order.begin(), order.begin() + candidateCount, order.end(), [this](size_t left, size_t right) {
				const int leftDistance = ColorDistance(selectedColor_, grounds_[left].rgb);
				const int rightDistance = ColorDistance(selectedColor_, grounds_[right].rgb);
				return leftDistance != rightDistance ? leftDistance < rightDistance : grounds_[left].serverId < grounds_[right].serverId;
			});
			for (size_t index = 0; index < candidateCount; ++index) {
				const GroundCandidate& ground = grounds_[order[index]];
				candidateIds_.push_back(ground.serverId);
				candidateChoice_->Append(wxString::Format("%u - %s (Client %u)", ground.serverId, wxstr(ground.name.empty() ? std::string("Unnamed ground") : ground.name), ground.clientId));
			}
			const uint16_t mapped = mappings_.contains(selectedColor_) ? mappings_[selectedColor_] : 0;
			const auto found = std::find(candidateIds_.begin(), candidateIds_.end(), mapped);
			candidateChoice_->SetSelection(found == candidateIds_.end() ? (candidateIds_.empty() ? wxNOT_FOUND : 0) : static_cast<int>(std::distance(candidateIds_.begin(), found)));
			manualId_->SetValue(mapped);
			uint64_t count = 0;
			for (const PngImportColor& color : document_->getColors()) {
				if (color.rgb == selectedColor_) {
					count = color.count;
					break;
				}
			}
			mappingLabel_->SetLabel(wxString::Format("#%06X\n%llu pixels", selectedColor_, static_cast<unsigned long long>(count)));
			RefreshCandidatePreview();
		}

		void RefreshCandidatePreview() {
			uint16_t id = mappings_.contains(selectedColor_) ? mappings_[selectedColor_] : 0;
			const int selection = candidateChoice_->GetSelection();
			if (selection >= 0 && selection < static_cast<int>(candidateIds_.size())) {
				id = candidateIds_[selection];
			}
			groundPreview_->SetSprite(ItemSpriteId(id));
			useCandidateButton_->Enable(id != 0);
		}

		void UseCandidate() {
			const int selection = candidateChoice_->GetSelection();
			if (selection < 0 || selection >= static_cast<int>(candidateIds_.size())) {
				return;
			}
			mappings_[selectedColor_] = candidateIds_[selection];
			RefreshColorList();
		}

		void UseManualId() {
			const int id = manualId_->GetValue();
			if (id == 0) {
				SkipColor();
				return;
			}
			if (!g_items.typeExists(id) || !g_items[id].isGroundTile()) {
				wxMessageBox("The selected Server ID is not a valid ground item for the loaded client.", "Import PNG", wxOK | wxICON_WARNING, this);
				return;
			}
			mappings_[selectedColor_] = static_cast<uint16_t>(id);
			RefreshColorList();
		}

		void SkipColor() {
			mappings_[selectedColor_] = 0;
			RefreshColorList();
		}

		PngImportOptions GetOptions() const {
			PngImportOptions options;
			options.offsetX = offsetX_->GetValue();
			options.offsetY = offsetY_->GetValue();
			options.floor = floor_->GetValue();
			options.scalePercent = scale_->GetValue();
			options.rotation = rotation_->GetSelection() * 90;
			options.flipHorizontal = flipHorizontal_->GetValue();
			options.flipVertical = flipVertical_->GetValue();
			options.transparentGroundId = static_cast<uint16_t>(transparentId_->GetValue());
			return options;
		}

		bool ValidateGround(uint16_t id, const wxString& label) {
			if (id == 0) {
				return true;
			}
			if (!g_items.typeExists(id) || !g_items[id].isGroundTile()) {
				wxMessageBox(label + " must be 0 (skip) or a valid ground Server ID.", "Import PNG", wxOK | wxICON_WARNING, this);
				return false;
			}
			return true;
		}

		void Apply() {
			const PngImportOptions options = GetOptions();
			if (!ValidateGround(options.transparentGroundId, "Transparent ground")) {
				return;
			}
			for (const auto& [rgb, id] : mappings_) {
				if (!ValidateGround(id, wxString::Format("Mapping #%06X", rgb))) {
					return;
				}
			}
			const int mode = modeChoice_->GetSelection();
			if (mode != newMapMode && !g_gui.GetCurrentEditor()) {
				wxMessageBox("Open a map before using this import mode.", "Import PNG", wxOK | wxICON_WARNING, this);
				return;
			}
			std::string error;
			uint64_t mappedTiles = 0;
			if (!document_->countMappedTiles(mappings_, options, mappedTiles, error)) {
				wxMessageBox(wxstr(error), "Import PNG", wxOK | wxICON_ERROR, this);
				return;
			}
			if (mappedTiles == 0) {
				wxMessageBox("No PNG color is mapped to a ground item.", "Import PNG", wxOK | wxICON_WARNING, this);
				return;
			}
			const wxString operation = mode == newMapMode ? "create a new editable map" : (mode == mergeEmptyMode ? "add grounds to empty tiles" : "replace existing grounds");
			if (wxMessageBox(wxString::Format("This will %s using %llu real ground tiles.\n\nContinue?", operation, static_cast<unsigned long long>(mappedTiles)), "Confirm PNG Import", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
				return;
			}
			wxLogMessage(wxString::Format("PNG import: %s, %dx%d source, %llu mapped output tiles.", sourcePath_, document_->getWidth(), document_->getHeight(), static_cast<unsigned long long>(mappedTiles)));
			if (mode == newMapMode) {
				ApplyNewMap(options, mappedTiles);
			} else {
				ApplyToCurrentMap(options, mode, mappedTiles);
			}
		}

		bool PopulateNewMap(Editor& editor, const PngImportOptions& options, uint64_t mappedTiles) {
			wxProgressDialog progress("Import PNG", "Generating real ground tiles...", 100, this, wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
			uint64_t changed = 0;
			std::string error;
			bool success = false;
			bool cancelledByUser = false;
			try {
				success = document_->forEachMappedTile(
					mappings_, options, [&](const PngImportTile& imported, uint64_t, uint64_t) {
						Tile* tile = editor.map.getOrCreateTile(Position(imported.x, imported.y, imported.z));
						tile->addItem(Item::Create(imported.groundId));
						tile->update();
						++changed;
						if ((changed & 4095) == 0 || changed == mappedTiles) {
							if (!progress.Update(static_cast<int>(changed * 100 / std::max<uint64_t>(1, mappedTiles)))) {
								cancelledByUser = true;
								return false;
							}
						}
						return true;
					},
					error
				);
			} catch (const std::exception& exception) {
				error = exception.what();
			} catch (...) {
				error = "Unexpected failure while creating PNG ground tiles.";
			}
			if (!success) {
				editor.map.clear(true);
				editor.map.clearChanges();
				const wxString message = cancelledByUser ? wxString("PNG import cancelled. The incomplete new map was cleared.") : wxstr(error);
				wxMessageBox(message, "Import PNG", wxOK | (cancelledByUser ? wxICON_INFORMATION : wxICON_ERROR), this);
				return false;
			}
			progress.Update(100);
			return true;
		}

		void ApplyNewMap(const PngImportOptions& options, uint64_t mappedTiles) {
			if (!g_gui.NewMap()) {
				return;
			}
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !PopulateNewMap(*editor, options, mappedTiles)) {
				return;
			}
			const auto [width, height] = document_->getOutputSize(options);
			editor->map.setWidth(options.offsetX + width);
			editor->map.setHeight(options.offsetY + height);
			editor->map.setName("PNG Generated Map");
			editor->map.setMapDescription("Editable OTBM map generated from " + nstr(sourcePath_));
			editor->map.doChange();
			FinishView(options);
			saveAfterApply_ = saveAfter_->GetValue();
			wxMessageBox(wxString::Format("Created %llu real ground tiles.\nThe map is fully editable and can be saved as .otbm.", static_cast<unsigned long long>(mappedTiles)), "PNG Import Complete", wxOK | wxICON_INFORMATION, this);
			EndModal(wxID_OK);
		}

		void ApplyToCurrentMap(const PngImportOptions& options, int mode, uint64_t mappedTiles) {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor) {
				return;
			}
			std::unique_ptr<BatchAction> batch(editor->actionQueue->createBatch(ACTION_IMPORT_PNG));
			std::unique_ptr<Action> action(editor->actionQueue->createAction(batch.get()));
			wxProgressDialog progress("Import PNG", "Applying real ground tiles...", 100, this, wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
			uint64_t changed = 0;
			uint64_t visited = 0;
			std::string error;
			bool success = false;
			bool cancelledByUser = false;
			try {
				success = document_->forEachMappedTile(
					mappings_, options, [&](const PngImportTile& imported, uint64_t, uint64_t) {
						++visited;
						const Position position(imported.x, imported.y, imported.z);
						TileLocation* location = editor->map.createTileL(position);
						Tile* oldTile = location->get();
						if (!(mode == mergeEmptyMode && oldTile && oldTile->ground)) {
							std::unique_ptr<Tile> newTile(oldTile ? oldTile->deepCopy(editor->map) : editor->map.allocator(location));
							newTile->addItem(Item::Create(imported.groundId));
							newTile->update();
							action->addChange(newd Change(newTile.release()));
							++changed;
							if (action->size() >= 4096) {
								batch->addAndCommitAction(action.release());
								action.reset(editor->actionQueue->createAction(batch.get()));
							}
						}
						if ((visited & 4095) == 0 || visited == mappedTiles) {
							if (!progress.Update(static_cast<int>(visited * 100 / std::max<uint64_t>(1, mappedTiles)))) {
								cancelledByUser = true;
								return false;
							}
						}
						return true;
					},
					error
				);
			} catch (const std::exception& exception) {
				error = exception.what();
			} catch (...) {
				error = "Unexpected failure while applying PNG ground tiles.";
			}
			if (!success) {
				batch->rollback();
				const wxString message = cancelledByUser ? wxString("PNG import cancelled. All committed chunks were rolled back.") : wxstr(error);
				wxMessageBox(message, "Import PNG", wxOK | (cancelledByUser ? wxICON_INFORMATION : wxICON_ERROR), this);
				return;
			}
			batch->addAndCommitAction(action.release());
			if (batch->size() == 0) {
				wxMessageBox("No eligible current-map tile changed.", "Import PNG", wxOK | wxICON_INFORMATION, this);
				return;
			}
			editor->actionQueue->addBatch(batch.release());
			progress.Update(100);
			const auto [width, height] = document_->getOutputSize(options);
			editor->map.setWidth(std::max(editor->map.getWidth(), options.offsetX + width));
			editor->map.setHeight(std::max(editor->map.getHeight(), options.offsetY + height));
			editor->map.doChange();
			FinishView(options);
			wxMessageBox(wxString::Format("Applied %llu real ground tiles. The operation can be undone.", static_cast<unsigned long long>(changed)), "PNG Import Complete", wxOK | wxICON_INFORMATION, this);
			EndModal(wxID_OK);
		}

		void FinishView(const PngImportOptions& options) {
			const auto [width, height] = document_->getOutputSize(options);
			g_gui.SetScreenCenterPosition(Position(options.offsetX + width / 2, options.offsetY + height / 2, options.floor), false);
			MapTab* tab = g_gui.GetCurrentMapTab();
			if (tab && tab->GetCanvas()) {
				int canvasWidth = 1;
				int canvasHeight = 1;
				tab->GetCanvas()->GetClientSize(&canvasWidth, &canvasHeight);
				const double zoom = std::max(width * static_cast<double>(TileSize) / std::max(1, canvasWidth), height * static_cast<double>(TileSize) / std::max(1, canvasHeight));
				tab->GetCanvas()->SetZoom(std::clamp(zoom, 0.125, 25.0));
				tab->GetCanvas()->RefreshViewport();
			}
			g_gui.RefreshView();
			g_gui.UpdateMinimap(true);
			g_gui.UpdateTitle();
			g_gui.UpdateMenus();
		}

		wxString sourcePath_;
		std::shared_ptr<PngMapImportDocument> document_;
		std::vector<GroundCandidate> grounds_;
		PngMapImportDocument::ColorMapping mappings_;
		std::vector<uint16_t> candidateIds_;
		uint32_t selectedColor_ = 0;
		bool saveAfterApply_ = false;
		wxStaticText* fileLabel_ = nullptr;
		wxStaticText* summaryLabel_ = nullptr;
		PngPreviewPanel* preview_ = nullptr;
		wxRadioBox* modeChoice_ = nullptr;
		wxSpinCtrl* floor_ = nullptr;
		wxSpinCtrl* offsetX_ = nullptr;
		wxSpinCtrl* offsetY_ = nullptr;
		wxSpinCtrl* scale_ = nullptr;
		wxChoice* rotation_ = nullptr;
		wxSpinCtrl* transparentId_ = nullptr;
		wxChoice* palette_ = nullptr;
		wxCheckBox* flipHorizontal_ = nullptr;
		wxCheckBox* flipVertical_ = nullptr;
		wxCheckBox* saveAfter_ = nullptr;
		wxButton* autoMapButton_ = nullptr;
		wxListCtrl* colorList_ = nullptr;
		DCButton* groundPreview_ = nullptr;
		wxStaticText* mappingLabel_ = nullptr;
		wxChoice* candidateChoice_ = nullptr;
		wxButton* useCandidateButton_ = nullptr;
		wxSpinCtrl* manualId_ = nullptr;
		wxButton* manualButton_ = nullptr;
		wxButton* skipButton_ = nullptr;
		wxStaticText* mappingSummary_ = nullptr;
		wxButton* applyButton_ = nullptr;
	};

} // namespace

bool RunPngMapImport(wxWindow* parent) {
	wxFileDialog fileDialog(parent, "Import PNG as OTBM Map", wxEmptyString, wxEmptyString, "PNG images (*.png)|*.png", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (fileDialog.ShowModal() != wxID_OK) {
		return false;
	}
	auto document = std::make_shared<PngMapImportDocument>();
	std::string error;
	if (!LoadPng(fileDialog.GetPath(), *document, error)) {
		wxLogWarning("PNG import failed: " + wxstr(error));
		wxMessageBox(wxstr(error), "Import PNG", wxOK | wxICON_ERROR, parent);
		return false;
	}
	if (!LimitColorCount(parent, *document)) {
		return false;
	}
	wxLogMessage(wxString::Format("PNG import: loaded %s (%dx%d, %llu colors).", fileDialog.GetPath(), document->getWidth(), document->getHeight(), static_cast<unsigned long long>(document->getColors().size())));
	bool applied = false;
	bool saveAfter = false;
	{
		PngMapImportDialog dialog(parent, fileDialog.GetPath(), std::move(document));
		applied = dialog.ShowModal() == wxID_OK;
		saveAfter = applied && dialog.ShouldSaveAfterApply();
	}
	if (saveAfter) {
		g_gui.SaveMapAs();
	}
	wxLogMessage(applied ? "PNG import: completed." : "PNG import: cancelled.");
	return applied;
}
