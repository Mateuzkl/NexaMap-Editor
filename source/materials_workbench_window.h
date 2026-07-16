//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MATERIALS_WORKBENCH_WINDOW_H_
#define RME_MATERIALS_WORKBENCH_WINDOW_H_

#include <set>
#include <vector>

#include <wx/frame.h>

namespace pugi {
	class xml_node;
}

class MaterialsBrushPreviewPanel;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxCloseEvent;
class wxListBox;
class wxNotebook;
class wxPanel;
class wxSearchCtrl;
class wxSpinCtrl;
class wxStaticBoxSizer;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxTreeEvent;

class MaterialsWorkbenchWindow final : public wxFrame {
public:
	static void Open(wxWindow* parent);

private:
	struct ItemEntry {
		int id = 0;
		int chance = 10;
	};
	struct DoorEntry {
		int id = 0;
		wxString type = "normal";
		bool open = false;
		bool locked = false;
		bool hate = false;
	};
	struct NodeEntry {
		wxString key;
		std::vector<ItemEntry> items;
		std::vector<DoorEntry> doors;
	};
	struct TileEntry {
		int x = 0;
		int y = 0;
		int z = 0;
		int id = 0;
	};
	struct CompositeEntry {
		int chance = 10;
		std::vector<TileEntry> tiles;
	};
	struct AlternativeEntry {
		std::vector<ItemEntry> singles;
		std::vector<CompositeEntry> composites;
	};
	struct LinkEntry {
		wxString relation;
		wxString target;
	};
	struct BrushRecord {
		wxString sourcePath;
		wxString sourceName;
		int sourceIndex = -1;
		wxString name;
		wxString type;
		int serverLookId = 0;
		int lookId = 0;
		int zOrder = 0;
		wxString thickness;
		bool draggable = false;
		bool onBlocking = false;
		bool onDuplicate = false;
		bool redoBorders = false;
		bool oneSize = false;
		bool wrappedAlternatives = false;
		std::vector<ItemEntry> groundItems;
		std::vector<NodeEntry> nodes;
		std::vector<AlternativeEntry> alternatives;
		std::vector<LinkEntry> links;
	};

	explicit MaterialsWorkbenchWindow(wxWindow* parent);
	~MaterialsWorkbenchWindow() override;

	void BuildLayout();
	void BindEvents();
	void LoadDefaultMaterialsFile();
	void OpenMaterialsFile();
	bool LoadCatalog(const wxString& path);
	bool ScanMaterialsFile(const wxString& path, std::set<wxString>& visited, wxString& error);
	BrushRecord ParseBrush(pugi::xml_node brush, const wxString& sourcePath, const wxString& sourceName, int sourceIndex) const;
	AlternativeEntry ParseAlternative(pugi::xml_node node) const;
	void PopulateCatalog();
	void LoadBrush(int index);
	bool ResolvePendingChanges(const wxString& action);
	void PopulateMetadata();
	void PopulateEditor();
	void PopulateContexts();
	void PopulateItems();
	void PopulateDoors();
	void PopulateComposites();
	void PopulateTiles();
	void PopulateLinks();
	void RefreshPreview();
	void RefreshHeader();
	void RefreshActionState();
	void MarkDirty();

	std::vector<ItemEntry>* CurrentItems();
	std::vector<DoorEntry>* CurrentDoors();
	AlternativeEntry* CurrentAlternative();
	CompositeEntry* CurrentComposite();
	void SelectItem(int index);
	void PickItem();
	void ApplyItem(bool add);
	void RemoveItem();
	void AddContext();
	void RemoveContext();
	void SelectDoor(int index);
	void ApplyDoor(bool add);
	void RemoveDoor();
	void AddComposite();
	void RemoveComposite();
	void SelectTile(int index);
	void ApplyTile(bool add);
	void RemoveTile();
	void CreateBrush();
	void DeleteBrush();
	void ShowUsedBy();
	bool SaveCurrentBrush();
	void RevertCurrentBrush();
	void OnClose(wxCloseEvent& event);

	static wxString TypeLabel(const wxString& type);

	wxString rootMaterialsPath_;
	std::vector<BrushRecord> records_;
	int currentBrush_ = -1;
	int currentContext_ = 0;
	int currentItem_ = -1;
	int currentDoor_ = -1;
	int currentComposite_ = -1;
	int currentTile_ = -1;
	bool dirty_ = false;
	bool loading_ = false;

	wxSearchCtrl* filterCtrl_ = nullptr;
	wxChoice* typeFilterCtrl_ = nullptr;
	wxTreeCtrl* catalogTree_ = nullptr;
	wxStaticText* fileLabel_ = nullptr;
	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxNotebook* notebook_ = nullptr;
	wxTextCtrl* nameCtrl_ = nullptr;
	wxChoice* typeCtrl_ = nullptr;
	wxSpinCtrl* serverLookCtrl_ = nullptr;
	wxSpinCtrl* clientLookCtrl_ = nullptr;
	wxSpinCtrl* zOrderCtrl_ = nullptr;
	wxTextCtrl* thicknessCtrl_ = nullptr;
	wxCheckBox* draggableCtrl_ = nullptr;
	wxCheckBox* onBlockingCtrl_ = nullptr;
	wxCheckBox* onDuplicateCtrl_ = nullptr;
	wxCheckBox* redoBordersCtrl_ = nullptr;
	wxCheckBox* oneSizeCtrl_ = nullptr;
	wxStaticText* editorTitleLabel_ = nullptr;
	wxStaticText* editorHintLabel_ = nullptr;
	wxStaticText* contextLabel_ = nullptr;
	wxChoice* contextCtrl_ = nullptr;
	wxButton* addContextButton_ = nullptr;
	wxButton* removeContextButton_ = nullptr;
	wxListBox* itemList_ = nullptr;
	wxSpinCtrl* itemIdCtrl_ = nullptr;
	wxSpinCtrl* itemChanceCtrl_ = nullptr;
	wxButton* updateItemButton_ = nullptr;
	wxButton* removeItemButton_ = nullptr;
	MaterialsBrushPreviewPanel* previewPanel_ = nullptr;
	wxStaticBoxSizer* doorBox_ = nullptr;
	wxListBox* doorList_ = nullptr;
	wxSpinCtrl* doorIdCtrl_ = nullptr;
	wxChoice* doorTypeCtrl_ = nullptr;
	wxCheckBox* doorOpenCtrl_ = nullptr;
	wxCheckBox* doorLockedCtrl_ = nullptr;
	wxStaticBoxSizer* compositeBox_ = nullptr;
	wxListBox* compositeList_ = nullptr;
	wxSpinCtrl* compositeChanceCtrl_ = nullptr;
	wxListBox* tileList_ = nullptr;
	wxSpinCtrl* tileIdCtrl_ = nullptr;
	wxSpinCtrl* tileXCtrl_ = nullptr;
	wxSpinCtrl* tileYCtrl_ = nullptr;
	wxSpinCtrl* tileZCtrl_ = nullptr;
	wxListBox* linksList_ = nullptr;
	wxButton* newBrushButton_ = nullptr;
	wxButton* deleteBrushButton_ = nullptr;
	wxButton* usedByButton_ = nullptr;
	wxButton* saveButton_ = nullptr;
	wxButton* revertButton_ = nullptr;
};

#endif
