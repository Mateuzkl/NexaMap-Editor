#include "main.h"

#include "cross_client_paste_dialog.h"

#include "brush.h"
#include "items.h"
#include "materials.h"
#include "replace_tool/replace_item_grid_panel.h"
#include "theme.h"
#include "tileset.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

namespace {
	constexpr int HighConfidenceThreshold = 88;

	void StyleText(wxStaticText* label, const wxColour& foreground, const wxColour& background) {
		label->SetForegroundColour(foreground);
		label->SetBackgroundColour(background);
	}

	wxBitmap EmptyBitmap(int size) {
		wxImage empty(size, size, true);
		empty.InitAlpha();
		std::memset(empty.GetData(), 0, static_cast<size_t>(size) * size * 3);
		std::memset(empty.GetAlpha(), 0, static_cast<size_t>(size) * size);
		return wxBitmap(empty);
	}

	wxBitmap PreviewBitmap(const CrossClientItemSnapshot& item, int size) {
		if (item.previewWidth <= 0 || item.previewHeight <= 0 || item.previewRgba.size() != static_cast<size_t>(item.previewWidth) * item.previewHeight * 4) {
			return EmptyBitmap(size);
		}
		wxImage image(item.previewWidth, item.previewHeight, false);
		image.InitAlpha();
		unsigned char* rgb = image.GetData();
		unsigned char* alpha = image.GetAlpha();
		for (size_t pixel = 0; pixel < static_cast<size_t>(item.previewWidth) * item.previewHeight; ++pixel) {
			rgb[pixel * 3 + 0] = item.previewRgba[pixel * 4 + 0];
			rgb[pixel * 3 + 1] = item.previewRgba[pixel * 4 + 1];
			rgb[pixel * 3 + 2] = item.previewRgba[pixel * 4 + 2];
			alpha[pixel] = item.previewRgba[pixel * 4 + 3];
		}
		image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
		return wxBitmap(image);
	}

	struct ResolverCatalogEntry {
		ReplaceLibraryItem item;
		wxString searchText;
		std::vector<wxString> categories;
	};

	void AddCategory(std::vector<wxString>& categories, const wxString& category) {
		if (std::find(categories.begin(), categories.end(), category) == categories.end()) {
			categories.push_back(category);
		}
	}

	bool ContainsInsensitive(const wxString& text, const wxString& value) {
		return text.Lower().Find(value.Lower()) != wxNOT_FOUND;
	}

	class CrossClientItemResolverDialog final : public wxDialog {
	public:
		CrossClientItemResolverDialog(wxWindow* parent, const CrossClientPasteRow& row) :
			wxDialog(parent, wxID_ANY, "Choose Destination Item", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			row(row) {
			BuildCatalog();
			BuildLayout();
			RefreshItems();
		}

		uint16_t GetDestinationId() const noexcept {
			return selectedDestinationId;
		}

	private:
		void BuildCatalog() {
			std::unordered_map<const Brush*, std::vector<wxString>> brushTilesets;
			for (const auto& [tilesetName, tileset] : g_materials.tilesets) {
				if (!tileset || tilesetName.empty()) {
					continue;
				}
				const wxString category = "Tileset: " + wxString::FromUTF8(tilesetName);
				for (const TilesetCategory* tilesetCategory : tileset->categories) {
					if (!tilesetCategory) {
						continue;
					}
					for (const Brush* brush : tilesetCategory->brushlist) {
						if (brush) {
							brushTilesets[brush].push_back(category);
						}
					}
				}
			}

			const size_t maximumId = std::min<size_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max());
			for (size_t id = 1; id <= maximumId; ++id) {
				if (!g_items.typeExists(static_cast<int>(id)) || !CrossClientClipboard::isCompatibleDestination(row.source, static_cast<uint16_t>(id))) {
					continue;
				}
				const ItemType& itemType = g_items[id];
				if (itemType.id == 0 || itemType.clientID == 0 || itemType.isMetaItem()) {
					continue;
				}

				ResolverCatalogEntry entry;
				entry.item.key = itemType.id;
				entry.item.serverId = ServerItemId(itemType.id);
				entry.item.clientId = itemType.clientID;
				entry.item.name = itemType.name.empty() ? "Unnamed item" : itemType.name;
				entry.searchText = wxString::Format("%u %u ", itemType.id, itemType.clientID) + wxString::FromUTF8(entry.item.name);

				Brush* brushes[] = { itemType.brush, itemType.doodad_brush, itemType.collection_brush, itemType.raw_brush };
				bool categorized = false;
				auto semanticCategory = [&](bool condition, const wxString& category) {
					if (condition) {
						AddCategory(entry.categories, category);
						categorized = true;
					}
				};
				semanticCategory(itemType.isGroundTile() || (itemType.brush && itemType.brush->isGround()), "Ground / Floors");
				semanticCategory(itemType.isWall || (itemType.brush && itemType.brush->isWall()), "Walls");
				semanticCategory(itemType.isBorder || itemType.isOptionalBorder, "Borders");
				semanticCategory(itemType.isDoor() || itemType.isBrushDoor || (itemType.brush && itemType.brush->isDoor()), "Doors / Windows");
				semanticCategory(itemType.isTable || itemType.isCarpet, "Tables / Carpets");
				semanticCategory(itemType.isContainer(), "Containers");
				semanticCategory(itemType.doodad_brush || (itemType.brush && itemType.brush->isDoodad()), "Doodads / Objects");

				for (Brush* brush : brushes) {
					if (!brush) {
						continue;
					}
					const wxString brushName = wxString::FromUTF8(brush->getName());
					entry.searchText += " " + brushName;
					if (ContainsInsensitive(brushName, "mountain") || ContainsInsensitive(brushName, "cliff") || ContainsInsensitive(brushName, "rock")) {
						AddCategory(entry.categories, "Mountains / Cliffs");
						categorized = true;
					}
					const auto tilesetIterator = brushTilesets.find(brush);
					if (tilesetIterator != brushTilesets.end()) {
						for (const wxString& tilesetCategory : tilesetIterator->second) {
							AddCategory(entry.categories, tilesetCategory);
							entry.searchText += " " + tilesetCategory;
						}
					}
				}
				if (!categorized) {
					AddCategory(entry.categories, "Other / Raw");
				}
				for (const wxString& category : entry.categories) {
					categoryNames.insert(category);
				}
				catalog.push_back(std::move(entry));
			}
		}

		void BuildLayout() {
			const wxColour surface = Theme::GetDark(Theme::Role::Surface);
			const wxColour raised = Theme::GetDark(Theme::Role::RaisedSurface);
			const wxColour text = Theme::GetDark(Theme::Role::Text);
			const wxColour subtle = Theme::GetDark(Theme::Role::TextSubtle);
			const wxColour accent(116, 76, 238);
			SetBackgroundColour(surface);

			auto* root = newd wxBoxSizer(wxVERTICAL);
			auto* sourcePanel = newd wxPanel(this, wxID_ANY);
			sourcePanel->SetBackgroundColour(raised);
			auto* sourceSizer = newd wxBoxSizer(wxHORIZONTAL);
			auto* sourcePreview = newd wxStaticBitmap(sourcePanel, wxID_ANY, PreviewBitmap(row.source, FROM_DIP(this, 48)));
			sourceSizer->Add(sourcePreview, 0, wxALL | wxALIGN_CENTER_VERTICAL, FROM_DIP(this, 10));
			auto* sourceTextSizer = newd wxBoxSizer(wxVERTICAL);
			auto* sourceTitle = newd wxStaticText(sourcePanel, wxID_ANY, wxString::Format("Missing source item %u", row.source.sourceId));
			wxFont sourceFont = sourceTitle->GetFont();
			sourceFont.SetWeight(wxFONTWEIGHT_BOLD);
			sourceTitle->SetFont(sourceFont);
			StyleText(sourceTitle, text, raised);
			wxString sourceDescription = row.source.name.empty() ? wxString("No item name") : wxString::FromUTF8(row.source.name);
			sourceDescription += wxString::Format("  |  Client ID %u  |  %u uses", row.source.sourceClientId, row.source.occurrences);
			auto* sourceDetails = newd wxStaticText(sourcePanel, wxID_ANY, sourceDescription);
			StyleText(sourceDetails, subtle, raised);
			sourceTextSizer->Add(sourceTitle, 0, wxBOTTOM, FROM_DIP(this, 4));
			sourceTextSizer->Add(sourceDetails, 0);
			sourceSizer->Add(sourceTextSizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 10));
			sourcePanel->SetSizer(sourceSizer);
			root->Add(sourcePanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));

			auto* explanation = newd wxStaticText(this, wxID_ANY, "Choose an existing destination item. Recommended matches are visual suggestions; review the ID before applying.");
			StyleText(explanation, subtle, surface);
			root->Add(explanation, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));

			auto* filters = newd wxBoxSizer(wxHORIZONTAL);
			categoryChoice = newd wxChoice(this, wxID_ANY);
			categoryChoice->Append("Recommended");
			categoryChoice->Append("All compatible items");
			const wxString preferredCategories[] = {
				"Ground / Floors",
				"Walls",
				"Mountains / Cliffs",
				"Borders",
				"Doors / Windows",
				"Doodads / Objects",
				"Tables / Carpets",
				"Containers",
				"Other / Raw",
			};
			for (const wxString& category : preferredCategories) {
				if (categoryNames.contains(category)) {
					categoryChoice->Append(category);
					categoryNames.erase(category);
				}
			}
			for (const wxString& category : categoryNames) {
				categoryChoice->Append(category);
			}
			categoryChoice->SetMinSize(FROM_DIP(this, wxSize(190, -1)));
			categoryChoice->SetMaxSize(FROM_DIP(this, wxSize(220, -1)));
			categoryChoice->SetSelection(row.recommendations.empty() ? 1 : 0);
			search = newd wxSearchCtrl(this, wxID_ANY);
			search->SetDescriptiveText("Search by Server ID, Client ID, item or brush name");
			search->ShowCancelButton(true);
			filters->Add(categoryChoice, 0, wxRIGHT, FROM_DIP(this, 8));
			filters->Add(search, 1);
			root->Add(filters, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));

			itemGrid = newd ReplaceItemGridPanel(this, [this](const ReplaceLibraryItem& item) {
				SelectDestination(item.serverId.value);
			});
			itemGrid->SetMinSize(FROM_DIP(this, wxSize(640, 250)));
			root->Add(itemGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));

			selectedLabel = newd wxStaticText(this, wxID_ANY, "No destination selected");
			StyleText(selectedLabel, subtle, surface);
			root->Add(selectedLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 10));

			auto* manualRow = newd wxBoxSizer(wxHORIZONTAL);
			auto* manualLabel = newd wxStaticText(this, wxID_ANY, "Destination Server ID");
			StyleText(manualLabel, subtle, surface);
			const int maximumDestinationId = std::max(1, std::min<int>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max()));
			manualId = newd wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FROM_DIP(this, wxSize(120, -1)), wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, 1, maximumDestinationId, 1);
			auto* selectId = newd wxButton(this, wxID_ANY, "Check ID");
			manualRow->Add(manualLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 8));
			manualRow->Add(manualId, 0, wxRIGHT, FROM_DIP(this, 8));
			manualRow->Add(selectId, 0);
			manualRow->AddStretchSpacer();
			auto* cancel = newd wxButton(this, wxID_CANCEL, "Cancel");
			useButton = newd wxButton(this, wxID_OK, "Use Selected Item");
			useButton->SetBackgroundColour(accent);
			useButton->SetForegroundColour(Theme::GetDark(Theme::Role::TextOnAccent));
			useButton->Enable(false);
			manualRow->Add(cancel, 0, wxRIGHT, FROM_DIP(this, 8));
			manualRow->Add(useButton, 0);
			root->Add(manualRow, 0, wxEXPAND | wxALL, FROM_DIP(this, 14));

			SetSizer(root);
			SetMinClientSize(FROM_DIP(this, wxSize(640, 440)));
			SetClientSize(FROM_DIP(this, wxSize(720, 520)));
			CentreOnParent();
			useButton->SetDefault();

			categoryChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
				RefreshItems();
			});
			search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
				RefreshItems();
			});
			selectId->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
				SelectDestination(static_cast<uint16_t>(manualId->GetValue()), true);
			});
			manualId->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
				SelectDestination(static_cast<uint16_t>(manualId->GetValue()), true);
			});
		}

		void RefreshItems() {
			const wxString selectedCategory = categoryChoice->GetStringSelection();
			const wxString query = search->GetValue().Lower();
			std::vector<ReplaceLibraryItem> visible;
			if (selectedCategory == "Recommended") {
				for (const CrossClientItemRecommendation& recommendation : row.recommendations) {
					auto found = std::find_if(catalog.begin(), catalog.end(), [&](const ResolverCatalogEntry& entry) {
						return entry.item.serverId.value == recommendation.destinationId;
					});
					if (found == catalog.end() || (!query.empty() && found->searchText.Lower().Find(query) == wxNOT_FOUND)) {
						continue;
					}
					ReplaceLibraryItem suggested = found->item;
					const wxString destinationName = wxString::FromUTF8(found->item.name);
					suggested.name = wxString::Format("%u%%  %s", recommendation.confidence, destinationName.c_str()).ToStdString();
					visible.push_back(std::move(suggested));
				}
			} else {
				for (const ResolverCatalogEntry& entry : catalog) {
					const bool categoryMatches = selectedCategory == "All compatible items"
						|| std::find(entry.categories.begin(), entry.categories.end(), selectedCategory) != entry.categories.end();
					if (categoryMatches && (query.empty() || entry.searchText.Lower().Find(query) != wxNOT_FOUND)) {
						visible.push_back(entry.item);
					}
				}
			}
			itemGrid->SetItems(std::move(visible));
		}

		void SelectDestination(uint16_t destinationId, bool reportError = false) {
			if (!CrossClientClipboard::isCompatibleDestination(row.source, destinationId)) {
				if (reportError) {
					wxMessageBox(
						"That ID does not exist in the destination or has an incompatible item type. Choose an item from the compatible list.",
						"Destination item not compatible",
						wxOK | wxICON_WARNING,
						this
					);
				}
				return;
			}
			const ItemType& item = g_items[destinationId];
			selectedDestinationId = destinationId;
			manualId->SetValue(destinationId);
			wxString label = wxString::Format("Selected: Server ID %u  |  Client ID %u", destinationId, item.clientID);
			if (!item.name.empty()) {
				label += "  |  " + wxString::FromUTF8(item.name);
			}
			selectedLabel->SetLabel(label);
			selectedLabel->SetForegroundColour(wxColour(51, 201, 111));
			useButton->Enable(true);
		}

		const CrossClientPasteRow& row;
		std::vector<ResolverCatalogEntry> catalog;
		std::set<wxString> categoryNames;
		wxChoice* categoryChoice = nullptr;
		wxSearchCtrl* search = nullptr;
		ReplaceItemGridPanel* itemGrid = nullptr;
		wxStaticText* selectedLabel = nullptr;
		wxSpinCtrl* manualId = nullptr;
		wxButton* useButton = nullptr;
		uint16_t selectedDestinationId = 0;
	};
}

CrossClientPasteDialog::CrossClientPasteDialog(wxWindow* parent, const CrossClientPasteAnalysis& analysis) :
	wxDialog(parent, wxID_ANY, "Cross-Client Paste", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	analysis(analysis) {
	const wxColour surface = Theme::GetDark(Theme::Role::Surface);
	const wxColour background = Theme::GetDark(Theme::Role::Background);
	const wxColour raised = Theme::GetDark(Theme::Role::RaisedSurface);
	const wxColour text = Theme::GetDark(Theme::Role::Text);
	const wxColour subtle = Theme::GetDark(Theme::Role::TextSubtle);
	SetBackgroundColour(surface);

	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(this, wxID_ANY, "Verify resources before pasting");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	titleFont.SetPointSize(titleFont.GetPointSize() + 1);
	title->SetFont(titleFont);
	StyleText(title, text, surface);
	root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 16));

	auto* explanation = newd wxStaticText(this, wxID_ANY, "Review exact matches, choose destination IDs for missing items, then apply the converted paste.");
	StyleText(explanation, subtle, surface);
	root->Add(explanation, 0, wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));

	auto* resourceGrid = newd wxFlexGridSizer(2, FROM_DIP(this, 5), FROM_DIP(this, 12));
	resourceGrid->AddGrowableCol(1, 1);
	auto addResource = [&](const wxString& labelText, const wxString& value, const wxString& tooltip) {
		auto* label = newd wxStaticText(this, wxID_ANY, labelText);
		auto* resource = newd wxStaticText(this, wxID_ANY, CompactPath(value));
		StyleText(label, subtle, surface);
		StyleText(resource, text, surface);
		resource->SetToolTip(tooltip);
		resourceGrid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
		resourceGrid->Add(resource, 1, wxEXPAND);
	};
	addResource("Source", analysis.sourceClient, analysis.sourceClient + "\n" + analysis.sourceServer);
	addResource("Destination", analysis.destinationClient, analysis.destinationClient + "\n" + analysis.destinationServer);
	root->Add(resourceGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 16));

	auto* summaryPanel = newd wxPanel(this, wxID_ANY);
	summaryPanel->SetBackgroundColour(raised);
	auto* summary = newd wxBoxSizer(wxHORIZONTAL);
	auto addSummary = [&](const wxString& label, const wxColour& colour, wxStaticText*& valueLabel) {
		valueLabel = newd wxStaticText(summaryPanel, wxID_ANY, label);
		wxFont font = valueLabel->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		valueLabel->SetFont(font);
		StyleText(valueLabel, colour, raised);
		summary->Add(valueLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 22));
	};
	summary->AddSpacer(FROM_DIP(this, 12));
	addSummary("Matched 0", wxColour(51, 201, 111), matchedValue);
	addSummary("Remapped 0", wxColour(65, 205, 230), remappedValue);
	addSummary("Missing 0", wxColour(238, 90, 105), missingValue);
	auto* occurrences = newd wxStaticText(summaryPanel, wxID_ANY, wxString::Format("%u item instances", analysis.totalOccurrences));
	StyleText(occurrences, subtle, raised);
	summary->AddStretchSpacer();
	summary->Add(occurrences, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	summaryPanel->SetSizer(summary);
	summaryPanel->SetMinSize(wxSize(-1, FROM_DIP(this, 34)));
	root->Add(summaryPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 16));

	itemList = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_VRULES | wxBORDER_SIMPLE);
	itemList->SetBackgroundColour(background);
	itemList->SetForegroundColour(text);
	itemList->InsertColumn(0, "Source item");
	itemList->InsertColumn(1, "Destination item");
	itemList->InsertColumn(2, "Result");
	itemList->InsertColumn(3, "Uses", wxLIST_FORMAT_RIGHT);
	itemList->InsertColumn(4, "Action");
	root->Add(itemList, 1, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 16));

	auto* actionRow = newd wxBoxSizer(wxHORIZONTAL);
	auto* actionHint = newd wxStaticText(this, wxID_ANY, "Double-click any row to choose or change its destination ID.");
	StyleText(actionHint, subtle, surface);
	recommendedButton = newd wxButton(this, wxID_ANY, "Map High-Confidence");
	recommendedButton->SetToolTip("Preview-map missing rows whose best visual recommendation is at least 88% similar. You can still review every result before pasting.");
	resolveButton = newd wxButton(this, wxID_ANY, "Choose Destination...");
	actionRow->Add(actionHint, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	actionRow->Add(recommendedButton, 0, wxRIGHT, FROM_DIP(this, 8));
	actionRow->Add(resolveButton, 0);
	root->Add(actionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 12));

	auto* footer = newd wxBoxSizer(wxHORIZONTAL);
	footerStatus = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	StyleText(footerStatus, subtle, surface);
	footer->Add(footerStatus, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	auto* cancel = newd wxButton(this, wxID_CANCEL, "Cancel");
	applyButton = newd wxButton(this, wxID_OK, "Apply && Paste");
	footer->Add(cancel, 0, wxRIGHT, FROM_DIP(this, 8));
	footer->Add(applyButton, 0);
	root->Add(footer, 0, wxEXPAND | wxALL, FROM_DIP(this, 16));

	SetSizer(root);
	SetMinClientSize(FROM_DIP(this, wxSize(720, 440)));
	SetClientSize(FROM_DIP(this, wxSize(820, 520)));
	CentreOnParent();
	applyButton->SetDefault();

	itemList->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		UpdateColumnWidths();
		event.Skip();
	});
	itemList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
		ResolveRow(static_cast<size_t>(itemList->GetItemData(event.GetIndex())));
	});
	itemList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
		const size_t rowIndex = static_cast<size_t>(itemList->GetItemData(event.GetIndex()));
		const bool missing = rowIndex < this->analysis.rows.size() && this->analysis.rows[rowIndex].state == CrossClientMatchState::Missing;
		resolveButton->SetLabel(missing ? wxString("Choose Destination...") : wxString("Change Mapping..."));
	});
	resolveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		ResolveSelected();
	});
	recommendedButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		ApplyRecommendedMappings();
	});
	RefreshState();
}

void CrossClientPasteDialog::PopulateRows() {
	long selectedRowData = -1;
	const long previousSelection = itemList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (previousSelection != -1) {
		selectedRowData = itemList->GetItemData(previousSelection);
	}
	itemList->DeleteAllItems();

	std::vector<size_t> order(analysis.rows.size());
	for (size_t index = 0; index < order.size(); ++index) {
		order[index] = index;
	}
	std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
		return static_cast<int>(analysis.rows[left].state) > static_cast<int>(analysis.rows[right].state);
	});

	const int imageSize = FROM_DIP(this, 32);
	auto* images = newd wxImageList(imageSize, imageSize, true, static_cast<int>(std::max<size_t>(1, analysis.rows.size())));
	long selectionToRestore = -1;
	for (size_t visibleIndex = 0; visibleIndex < order.size(); ++visibleIndex) {
		const size_t rowIndex = order[visibleIndex];
		const CrossClientPasteRow& row = analysis.rows[rowIndex];
		const int imageIndex = images->Add(PreviewBitmap(row.source, imageSize));
		wxString sourceLabel = wxString::Format("%u", row.source.sourceId);
		if (!row.source.name.empty()) {
			sourceLabel += "  " + wxString::FromUTF8(row.source.name);
		}
		const long listIndex = itemList->InsertItem(static_cast<long>(visibleIndex), sourceLabel, imageIndex);
		itemList->SetItemData(listIndex, static_cast<long>(rowIndex));
		if (static_cast<long>(rowIndex) == selectedRowData) {
			selectionToRestore = listIndex;
		}
		if (row.state == CrossClientMatchState::Missing) {
			const wxString recommendation = row.recommendations.empty()
				? wxString("Not found")
				: wxString::Format("Suggested: %u (%u%%)", row.recommendations.front().destinationId, row.recommendations.front().confidence);
			itemList->SetItem(listIndex, 1, recommendation);
			itemList->SetItem(listIndex, 2, "!  Missing");
			itemList->SetItem(listIndex, 4, "Choose ID...");
			itemList->SetItemTextColour(listIndex, wxColour(238, 90, 105));
		} else {
			wxString destination = wxString::Format("%u", row.destinationId);
			if (!row.destinationName.empty()) {
				destination += "  " + wxString::FromUTF8(row.destinationName);
			}
			itemList->SetItem(listIndex, 1, destination);
			const bool remapped = row.state == CrossClientMatchState::Remapped;
			itemList->SetItem(listIndex, 2, remapped ? "->  Remapped" : "OK  Matched");
			itemList->SetItem(listIndex, 4, "Change...");
			itemList->SetItemTextColour(listIndex, remapped ? wxColour(65, 205, 230) : Theme::GetDark(Theme::Role::Text));
		}
		itemList->SetItem(listIndex, 3, wxString::Format("%u", row.source.occurrences));
	}
	itemList->AssignImageList(images, wxIMAGE_LIST_SMALL);
	if (selectionToRestore == -1 && itemList->GetItemCount() > 0) {
		selectionToRestore = 0;
	}
	if (selectionToRestore != -1) {
		itemList->SetItemState(selectionToRestore, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
		itemList->EnsureVisible(selectionToRestore);
	}
	UpdateColumnWidths();
}

void CrossClientPasteDialog::RefreshState() {
	matchedValue->SetLabel(wxString::Format("Matched %u", analysis.matched));
	remappedValue->SetLabel(wxString::Format("Remapped %u", analysis.remapped));
	missingValue->SetLabel(wxString::Format("Missing %u", analysis.missing));
	const bool hasHighConfidence = std::any_of(analysis.rows.begin(), analysis.rows.end(), [](const CrossClientPasteRow& row) {
		return row.state == CrossClientMatchState::Missing && !row.recommendations.empty() && row.recommendations.front().confidence >= HighConfidenceThreshold;
	});
	recommendedButton->Enable(hasHighConfidence);
	resolveButton->Enable(!analysis.rows.empty());

	const bool canApply = analysis.canApply();
	applyButton->SetBackgroundColour(canApply ? wxColour(116, 76, 238) : Theme::GetDark(Theme::Role::RaisedSurface));
	applyButton->SetForegroundColour(canApply ? Theme::GetDark(Theme::Role::TextOnAccent) : Theme::GetDark(Theme::Role::TextSubtle));
	applyButton->Enable(canApply);
	if (canApply) {
		footerStatus->SetLabel("All item IDs are resolved. Source resources remain unchanged.");
		footerStatus->SetForegroundColour(wxColour(51, 201, 111));
		applyButton->SetToolTip(wxEmptyString);
	} else {
		footerStatus->SetLabel("Resolve each Missing row by choosing a compatible destination ID.");
		footerStatus->SetForegroundColour(wxColour(238, 150, 70));
		applyButton->SetToolTip("Resolve every missing destination resource before applying this paste.");
	}
	PopulateRows();
}

void CrossClientPasteDialog::ResolveSelected() {
	long selected = itemList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected == -1) {
		for (long index = 0; index < itemList->GetItemCount(); ++index) {
			const size_t rowIndex = static_cast<size_t>(itemList->GetItemData(index));
			if (rowIndex < analysis.rows.size() && analysis.rows[rowIndex].state == CrossClientMatchState::Missing) {
				selected = index;
				break;
			}
		}
	}
	if (selected != -1) {
		ResolveRow(static_cast<size_t>(itemList->GetItemData(selected)));
	}
}

void CrossClientPasteDialog::ResolveRow(size_t rowIndex) {
	if (rowIndex >= analysis.rows.size()) {
		return;
	}
	CrossClientItemResolverDialog dialog(this, analysis.rows[rowIndex]);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	wxString error;
	if (!CrossClientClipboard::resolveMapping(analysis, rowIndex, dialog.GetDestinationId(), error)) {
		wxMessageBox(error, "Could not map destination item", wxOK | wxICON_ERROR, this);
		return;
	}
	RefreshState();
	for (long index = 0; index < itemList->GetItemCount(); ++index) {
		if (static_cast<size_t>(itemList->GetItemData(index)) == rowIndex) {
			itemList->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			itemList->EnsureVisible(index);
			break;
		}
	}
}

void CrossClientPasteDialog::ApplyRecommendedMappings() {
	std::vector<std::pair<size_t, uint16_t>> mappings;
	for (size_t rowIndex = 0; rowIndex < analysis.rows.size(); ++rowIndex) {
		const CrossClientPasteRow& row = analysis.rows[rowIndex];
		if (row.state == CrossClientMatchState::Missing && !row.recommendations.empty() && row.recommendations.front().confidence >= HighConfidenceThreshold) {
			mappings.emplace_back(rowIndex, row.recommendations.front().destinationId);
		}
	}
	if (mappings.empty()) {
		wxMessageBox("No unresolved item currently has a visual recommendation above the safe threshold.", "No high-confidence matches", wxOK | wxICON_INFORMATION, this);
		return;
	}
	const int answer = wxMessageBox(
		wxString::Format("Map %zu missing item types to their best visual recommendations?\n\nThis only updates the review. You can inspect or change every mapping before Apply & Paste.", mappings.size()),
		"Review recommended mappings",
		wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
		this
	);
	if (answer != wxYES) {
		return;
	}
	for (const auto& [rowIndex, destinationId] : mappings) {
		wxString error;
		CrossClientClipboard::resolveMapping(analysis, rowIndex, destinationId, error);
	}
	RefreshState();
}

void CrossClientPasteDialog::UpdateColumnWidths() {
	if (!itemList) {
		return;
	}
	int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, itemList);
	if (scrollbarWidth <= 0) {
		scrollbarWidth = FROM_DIP(this, 17);
	}
	const int rightGutter = scrollbarWidth + FROM_DIP(this, 12);
	const int available = std::max(FROM_DIP(this, 650), itemList->GetClientSize().x - rightGutter);
	const int usesWidth = FROM_DIP(this, 62);
	const int resultWidth = FROM_DIP(this, 108);
	const int actionWidth = FROM_DIP(this, 96);
	const int resourceWidth = std::max(FROM_DIP(this, 185), (available - usesWidth - resultWidth - actionWidth) / 2);
	itemList->SetColumnWidth(0, resourceWidth);
	itemList->SetColumnWidth(1, resourceWidth);
	itemList->SetColumnWidth(2, resultWidth);
	itemList->SetColumnWidth(3, usesWidth);
	itemList->SetColumnWidth(4, std::max(actionWidth, available - resourceWidth * 2 - resultWidth - usesWidth));
}

wxString CrossClientPasteDialog::CompactPath(const wxString& path) const {
	if (path.length() <= 64) {
		return path;
	}
	return path.Left(22) + "..." + path.Right(38);
}
