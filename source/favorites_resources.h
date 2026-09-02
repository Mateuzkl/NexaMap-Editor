#ifndef NEXAMAP_FAVORITES_RESOURCES_H_
#define NEXAMAP_FAVORITES_RESOURCES_H_

#include "favorites_manager.h"
#include <optional>
#include <wx/bitmap.h>

class Brush;
class wxMenu;
class wxWindow;

// Adapter to the currently active EditorResourceSession. Resolved pointers are
// borrowed only for the immediate call; neither the model nor the UI caches them.
namespace FavoriteResources {
	std::string CaptureContext();
	std::string ActiveContext();
	std::optional<FavoriteEntry> FromBrush(Brush* brush);
	std::optional<FavoriteEntry> FromStamp(const std::string& name);
	Brush* ResolveBrush(const FavoriteEntry& entry);
	bool IsAvailable(const FavoriteEntry& entry);
	wxBitmap Preview(const FavoriteEntry& entry, int pixels);
	wxString Tooltip(const FavoriteEntry& entry);
	bool IsCurrentPalette(const wxWindow* window);
	void AppendMenu(wxMenu& menu, wxWindow* parent, const FavoriteEntry& entry, const wxString& label = {});
	void Popup(wxWindow* parent, Brush* brush);
}

#endif
