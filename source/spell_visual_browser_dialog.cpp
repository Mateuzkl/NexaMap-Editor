//////////////////////////////////////////////////////////////////////
// Searchable browser for active-client magic effects and missiles.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spell_visual_browser_dialog.h"

#include "graphics.h"
#include "gui.h"
#include "theme.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <unordered_set>

namespace {
	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	}

	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	GameSprite* VisualSprite(ServerVisualKind kind, uint32_t id) {
		return kind == ServerVisualKind::MagicEffect ? g_gui.gfx.getEffectSprite(static_cast<int>(id)) : g_gui.gfx.getDistanceSprite(static_cast<int>(id));
	}

	std::pair<int, int> DirectionPattern(int direction, const GameSprite& sprite) {
		static constexpr int offsets[8][2] {
			{ 0, -1 },
			{ 1, -1 },
			{ 1, 0 },
			{ 1, 1 },
			{ 0, 1 },
			{ -1, 1 },
			{ -1, 0 },
			{ -1, -1 },
		};
		const int selected = std::clamp(direction, 0, 7);
		const int x = sprite.pattern_x >= 3 ? offsets[selected][0] + 1 : std::min<int>(selected, std::max<int>(0, sprite.pattern_x - 1));
		const int y = sprite.pattern_y >= 3 ? offsets[selected][1] + 1 : 0;
		return { x, y };
	}

	wxBitmap BitmapFromRgba(const std::vector<uint8_t>& rgba, int width, int height) {
		if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4) {
			return {};
		}
		auto* rgb = new unsigned char[static_cast<std::size_t>(width) * height * 3];
		auto* alpha = new unsigned char[static_cast<std::size_t>(width) * height];
		for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
			rgb[pixel * 3] = rgba[pixel * 4];
			rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
			rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
			alpha[pixel] = rgba[pixel * 4 + 3];
		}
		wxImage image(width, height, rgb, alpha, false);
		return wxBitmap(image);
	}

	class VisualSpritePanel final : public wxPanel {
	public:
		VisualSpritePanel(wxWindow* parent, ServerVisualKind visualKind) :
			wxPanel(parent, wxID_ANY), kind(visualKind), timer(this) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			SetMinSize(FromDIP(wxSize(230, 250)));
			Bind(wxEVT_PAINT, &VisualSpritePanel::onPaint, this);
			Bind(wxEVT_TIMER, &VisualSpritePanel::onTimer, this, timer.GetId());
			timer.Start(120);
		}

		void setVisual(uint32_t visualId, int selectedDirection) {
			id = visualId;
			direction = selectedDirection;
			frame = 0;
			cachedId = std::numeric_limits<uint32_t>::max();
			Refresh();
		}

		void setDirection(int selectedDirection) {
			direction = selectedDirection;
			cachedDirection = -1;
			Refresh();
		}

	private:
		void onTimer(wxTimerEvent&) {
			GameSprite* sprite = VisualSprite(kind, id);
			if (sprite && sprite->frames > 1) {
				frame = (frame + 1) % sprite->frames;
			}
			Refresh(false);
		}

		void onPaint(wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
			dc.Clear();
			const wxSize size = GetClientSize();
			GameSprite* sprite = VisualSprite(kind, id);
			if (!sprite) {
				dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
				dc.DrawLabel(id == 0 ? "No visual effect" : "This ID is not available in the active client.", wxRect(FromDIP(12), FromDIP(12), size.x - FromDIP(24), size.y - FromDIP(24)), wxALIGN_CENTER);
				return;
			}
			if (cachedId != id || cachedFrame != frame || cachedDirection != direction) {
				std::vector<uint8_t> pixels;
				int width = 0;
				int height = 0;
				bool pending = false;
				const auto [patternX, patternY] = DirectionPattern(direction, *sprite);
				if (!sprite->getVisualPreviewRGBA(pixels, width, height, pending, true, nullptr, 0, frame, 0, patternX, patternY)) {
					dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
					dc.DrawLabel(pending ? "Loading sprite..." : "Sprite unavailable", GetClientRect(), wxALIGN_CENTER);
					return;
				}
				cachedBitmap = BitmapFromRgba(pixels, width, height);
				cachedId = id;
				cachedFrame = frame;
				cachedDirection = direction;
			}
			wxBitmap bitmap = cachedBitmap;
			if (!bitmap.IsOk()) {
				return;
			}
			const int width = bitmap.GetWidth();
			const int height = bitmap.GetHeight();
			const int maximum = std::max(32, std::min(size.x - FromDIP(28), size.y - FromDIP(58)));
			const int scale = std::max(1, std::min(maximum / width, maximum / height));
			if (scale > 1) {
				bitmap = wxBitmap(bitmap.ConvertToImage().Scale(width * scale, height * scale, wxIMAGE_QUALITY_NEAREST));
			}
			const int x = (size.x - bitmap.GetWidth()) / 2;
			const int y = (size.y - bitmap.GetHeight()) / 2;
			dc.DrawBitmap(bitmap, x, y, true);
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			dc.DrawText(wxString::Format("ID %u  |  frame %d/%u", id, frame + 1, std::max<unsigned>(1, sprite->frames)), FromDIP(8), size.y - FromDIP(28));
		}

		ServerVisualKind kind;
		wxTimer timer;
		uint32_t id = 0;
		int direction = 0;
		int frame = 0;
		wxBitmap cachedBitmap;
		uint32_t cachedId = std::numeric_limits<uint32_t>::max();
		int cachedFrame = -1;
		int cachedDirection = -1;
	};
}

SpellVisualBrowserDialog::SpellVisualBrowserDialog(
	wxWindow* parent,
	ServerVisualKind visualKind,
	const ServerVisualCatalog& catalog,
	const std::string& currentValue
) :
	wxDialog(parent, wxID_ANY, visualKind == ServerVisualKind::MagicEffect ? "Choose Magic Effect" : "Choose Projectile", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	kind(visualKind),
	values(visualKind == ServerVisualKind::MagicEffect ? catalog.effects() : catalog.projectiles()) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	std::unordered_set<uint32_t> namedIds;
	for (const ServerVisualConstant& entry : values) {
		namedIds.insert(entry.id);
	}
	const uint32_t clientMaximum = kind == ServerVisualKind::MagicEffect ? g_gui.gfx.getEffectSpriteMaxID() : g_gui.gfx.getDistanceSpriteMaxID();
	for (uint32_t id = 1; id <= clientMaximum; ++id) {
		if (!namedIds.contains(id)) {
			values.push_back({ kind, std::to_string(id), id, {} });
		}
	}
	std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
		return left.id != right.id ? left.id < right.id : left.name < right.name;
	});

	auto* root = newd wxBoxSizer(wxVERTICAL);
	search = newd wxSearchCtrl(this, wxID_ANY);
	search->SetDescriptiveText(kind == ServerVisualKind::MagicEffect ? "Search effects by constant name or ID..." : "Search projectiles by constant name or ID...");
	search->ShowCancelButton(true);
	root->Add(search, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* body = newd wxBoxSizer(wxHORIZONTAL);
	list = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	list->AppendColumn("Constant", wxLIST_FORMAT_LEFT, FromDIP(280));
	list->AppendColumn("ID", wxLIST_FORMAT_RIGHT, FromDIP(70));
	list->AppendColumn("Active client", wxLIST_FORMAT_LEFT, FromDIP(110));
	body->Add(list, 3, wxEXPAND | wxRIGHT, FromDIP(10));
	auto* side = newd wxBoxSizer(wxVERTICAL);
	preview = newd VisualSpritePanel(this, kind);
	side->Add(preview, 1, wxEXPAND);
	if (kind == ServerVisualKind::DistanceEffect) {
		auto* directionChoice = newd wxChoice(this, wxID_ANY);
		for (const wxString& direction : { wxString("North"), wxString("North-East"), wxString("East"), wxString("South-East"), wxString("South"), wxString("South-West"), wxString("West"), wxString("North-West") }) {
			directionChoice->Append(direction);
		}
		directionChoice->SetSelection(2);
		side->Add(directionChoice, 0, wxEXPAND | wxTOP, FromDIP(8));
		directionChoice->Bind(wxEVT_CHOICE, [this, directionChoice](wxCommandEvent&) {
			static_cast<VisualSpritePanel*>(preview)->setDirection(directionChoice->GetSelection());
		});
	}
	body->Add(side, 2, wxEXPAND);
	root->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	details = newd wxStaticText(this, wxID_ANY, "Select a visual to inspect its source and active-client availability.");
	details->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	details->SetMinSize(FromDIP(wxSize(-1, 38)));
	root->Add(details, 0, wxEXPAND | wxALL, FromDIP(12));
	root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	openButton = FindWindow(wxID_OK);
	if (openButton) {
		openButton->SetLabel("Use Selected");
		openButton->Enable(false);
	}
	SetSizer(root);
	SetMinSize(FromDIP(wxSize(760, 500)));
	SetSize(FromDIP(wxSize(930, 650)));
	CentreOnParent();

	search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { rebuildList(); });
	search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { search->Clear(); });
	search->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { openSelection(); });
	list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { updateSelection(); });
	list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { updateSelection(); });
	list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { openSelection(); });
	Bind(
		wxEVT_BUTTON, [this](wxCommandEvent&) { openSelection(); }, wxID_OK
	);
	rebuildList();
	if (const auto current = catalog.resolve(kind, currentValue)) {
		for (long row = 0; row < list->GetItemCount(); ++row) {
			if (visible[static_cast<std::size_t>(row)] < values.size() && values[visible[static_cast<std::size_t>(row)]].id == *current) {
				list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				list->EnsureVisible(row);
				break;
			}
		}
	}
}

std::optional<std::string> SpellVisualBrowserDialog::selectedValue() const {
	if (!list) {
		return std::nullopt;
	}
	const long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (row < 0 || static_cast<std::size_t>(row) >= visible.size()) {
		return std::nullopt;
	}
	return values[visible[static_cast<std::size_t>(row)]].name;
}

void SpellVisualBrowserDialog::rebuildList() {
	const std::string needle = LowerAscii(search->GetValue().ToStdString(wxConvUTF8));
	visible.clear();
	list->DeleteAllItems();
	for (std::size_t index = 0; index < values.size(); ++index) {
		const ServerVisualConstant& value = values[index];
		const std::string searchable = LowerAscii(value.name + " " + std::to_string(value.id));
		if (!needle.empty() && searchable.find(needle) == std::string::npos) {
			continue;
		}
		visible.push_back(index);
		const long row = list->InsertItem(list->GetItemCount(), wxString::FromUTF8(value.name));
		list->SetItem(row, 1, wxString::Format("%u", value.id));
		list->SetItem(row, 2, VisualSprite(kind, value.id) ? "Available" : "Unavailable");
	}
	if (!visible.empty()) {
		list->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
	updateSelection();
}

void SpellVisualBrowserDialog::updateSelection() {
	const long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	const bool selected = row >= 0 && static_cast<std::size_t>(row) < visible.size();
	if (openButton) {
		openButton->Enable(selected);
	}
	if (!selected) {
		details->SetLabel("No matching visual selected.");
		static_cast<VisualSpritePanel*>(preview)->setVisual(0, 0);
		return;
	}
	const ServerVisualConstant& value = values[visible[static_cast<std::size_t>(row)]];
	const bool available = VisualSprite(kind, value.id) != nullptr;
	const wxString source = value.sourcePath.empty() ? wxString("numeric client entry") : PathText(value.sourcePath);
	const wxString availability = available ? wxString("available in active client") : wxString("not available in active client");
	const wxString label = wxString::Format("%s = %u  |  %s  |  %s", wxString::FromUTF8(value.name), value.id, availability, source);
	openButton->Enable(available || value.id == 0);
	details->SetLabel(label);
	details->SetToolTip(label);
	static_cast<VisualSpritePanel*>(preview)->setVisual(value.id, kind == ServerVisualKind::DistanceEffect ? 2 : 0);
}

void SpellVisualBrowserDialog::openSelection() {
	if (selectedValue() && openButton && openButton->IsEnabled()) {
		EndModal(wxID_OK);
	}
}
