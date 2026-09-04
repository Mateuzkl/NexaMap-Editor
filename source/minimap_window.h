#ifndef RME_MINIMAP_WINDOW_H_
#define RME_MINIMAP_WINDOW_H_

class MinimapCanvas;
class MinimapToolButton;
class Position;

class MinimapWindow final : public wxPanel {
public:
	explicit MinimapWindow(wxWindow* parent);
	~MinimapWindow() override = default;

	void DelayedUpdate();
	void RefreshMap(bool invalidateContent = true);

private:
	void BuildLayout();
	void ApplyTheme();
	void UpdateState();
	void UpdateControlsVisibility();
	void PositionOverlayControls();
	void NavigateToInput();
	bool ParsePosition(const wxString& value, Position& position) const;
	void ShowOptionsMenu();
	void SetInputError(const wxString& message);

	void OnClose(wxCloseEvent& event);
	void OnDelayedUpdate(wxTimerEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnCanvasSize(wxSizeEvent& event);
	void OnSystemColourChanged(wxSysColourChangedEvent& event);

	wxTimer updateTimer_;
	wxPanel* headerPanel_ = nullptr;
	wxPanel* goPanel_ = nullptr;
	wxStaticText* coordinateLabel_ = nullptr;
	wxTextCtrl* goToInput_ = nullptr;
	MinimapCanvas* canvas_ = nullptr;
	MinimapToolButton* floorUpButton_ = nullptr;
	MinimapToolButton* floorDownButton_ = nullptr;
	MinimapToolButton* zoomInButton_ = nullptr;
	MinimapToolButton* zoomOutButton_ = nullptr;
	MinimapToolButton* centerButton_ = nullptr;
	MinimapToolButton* optionsButton_ = nullptr;
	MinimapToolButton* goButton_ = nullptr;
	bool showControls_ = true;
	bool inputHasError_ = false;
};

#endif
