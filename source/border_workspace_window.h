//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BORDER_WORKSPACE_WINDOW_H_
#define RME_BORDER_WORKSPACE_WINDOW_H_

#include <array>
#include <map>
#include <set>
#include <vector>

#include <wx/frame.h>

class DCButton;
class wxButton;
class wxChoice;
class wxCloseEvent;
class wxListBox;
class wxSearchCtrl;
class wxSpinCtrl;
class wxStaticText;

class BorderWorkspaceWindow final : public wxFrame {
public:
	struct ItemCount {
		int itemId = 0;
		size_t count = 0;
	};

	static void Open(wxWindow* parent);
	static void OpenForItems(wxWindow* parent, const std::vector<ItemCount>& items);
	static bool IsAvailableForCurrentClient();

private:
	struct BorderRecord {
		wxString sourcePath;
		wxString sourceName;
		wxString description;
		int sourceIndex = -1;
		int id = 0;
		int group = 0;
		bool optional = false;
		std::array<int, 12> items {};
	};
	struct ItemLocation {
		int recordIndex = -1;
		int slot = -1;
	};

	explicit BorderWorkspaceWindow(wxWindow* parent);
	~BorderWorkspaceWindow() override;

	void BuildLayout();
	void BindEvents();
	void OpenMaterialsFile();
	void LoadDefaultMaterialsFile();
	bool LoadCatalog(const wxString& path);
	bool ScanMaterialsFile(const wxString& path, std::set<wxString>& visited, wxString& error);
	bool EnsureCurrentClientCatalog();
	bool OpenItems(const std::vector<ItemCount>& items);
	bool FocusBorder(int recordIndex, int slot);
	void RebuildItemIndex();
	void PopulateBorderList();
	bool ResolvePendingChanges(const wxString& action);
	void LoadSelection(int recordIndex);
	void RefreshWorkspace();
	void RefreshSlot(int slot);
	void RefreshButtons();
	void SelectSlot(int slot);
	void MarkDirty();
	void ApplySelectedItem();
	void PickSelectedItem();
	bool SaveCurrentBorder();
	void RevertCurrentBorder();
	void CreateBorder();
	void DeleteCurrentBorder();
	void OnClose(wxCloseEvent& event);

	static int EdgeIndex(const wxString& edge);
	static const char* EdgeName(int index);
	static const char* EdgeLabel(int index);
	static int ItemSpriteId(int itemId);

	wxString rootMaterialsPath_;
	std::vector<BorderRecord> records_;
	std::map<int, std::vector<ItemLocation>> itemIndex_;
	std::vector<int> visibleRecords_;
	BorderRecord originalRecord_;
	int currentRecord_ = -1;
	int selectedSlot_ = 0;
	bool hasOriginalRecord_ = false;
	bool dirty_ = false;
	bool loading_ = false;

	wxSearchCtrl* filterCtrl_ = nullptr;
	wxListBox* borderList_ = nullptr;
	wxStaticText* fileLabel_ = nullptr;
	wxStaticText* headingLabel_ = nullptr;
	wxStaticText* sourceLabel_ = nullptr;
	wxSpinCtrl* idCtrl_ = nullptr;
	wxSpinCtrl* groupCtrl_ = nullptr;
	wxChoice* typeChoice_ = nullptr;
	std::array<DCButton*, 12> slotButtons_ {};
	std::array<wxStaticText*, 12> slotLabels_ {};
	std::array<DCButton*, 12> previewButtons_ {};
	wxStaticText* selectedEdgeLabel_ = nullptr;
	DCButton* selectedItemPreview_ = nullptr;
	wxSpinCtrl* selectedItemCtrl_ = nullptr;
	wxButton* saveButton_ = nullptr;
	wxButton* revertButton_ = nullptr;
	wxButton* deleteButton_ = nullptr;
};

#endif
