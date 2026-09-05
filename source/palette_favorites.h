#ifndef NEXAMAP_PALETTE_FAVORITES_H_
#define NEXAMAP_PALETTE_FAVORITES_H_

#include "palette_common.h"
#include "favorites_manager.h"
#include <memory>
#include <map>
#include <optional>
#include <set>

class EditorResourceSession;

class FavoritesPalettePanel : public PalettePanel {
public:
	explicit FavoritesPalettePanel(wxWindow* parent);
	~FavoritesPalettePanel() override;
	wxString GetName() const override {
		return "Favorites";
	}
	PaletteType GetType() const override {
		return TILESET_FAVORITES;
	}
	Brush* GetSelectedBrush() const override;
	int GetSelectedBrushSize() const override;
	bool SelectBrush(const Brush* brush) override;
	void LoadCurrentContents() override;
	void InvalidateContents() override;
	void OnSwitchIn() override;
	void OnSwitchOut() override;
	void OnUpdate() override;
	void RefreshFavorites();

private:
	class Grid;
	bool HasCurrentResources() const;
	void Use(const FavoriteEntry& entry);
	void ApplyColours();
	void ReleaseSearchFocus();
	std::weak_ptr<EditorResourceSession> session_;
	std::string context_;
	std::optional<FavoriteEntry> selected_;
	std::set<FavoriteCategory> collapsed_;
	std::map<std::string, wxBitmap> previews_;
	uint64_t revision_ = 0;
	bool searchHasHotkeys_ = false;
	wxTextCtrl* search_;
	wxButton* clear_;
	Grid* grid_;
};

#endif
