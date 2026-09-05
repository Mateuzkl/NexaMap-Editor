#include <wx/app.h>
#include <wx/dcmemory.h>
#include "map_overlay_text.h"
#include <cstdlib>
#include <iostream>

namespace {
	void Check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}
}

class OverlayTestApp : public wxApp {
public:
	bool OnInit() override {
		return true;
	}
};
wxIMPLEMENT_APP_NO_MAIN(OverlayTestApp);

int main(int argc, char** argv) {
	if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) {
		return 1;
	}
	{
		wxBitmap bitmap(1, 1, 24);
		wxMemoryDC dc(bitmap);
		for (int pixels : { 12, 15, 18, 24 }) {
			dc.SetFont(wxFont(wxFontInfo(wxSize(0, pixels)).Family(wxFONTFAMILY_SWISS)));
			const wxString name = wxString::FromUTF8("Sarcophagus of the ancient guardian — coleção de relíquias");
			for (int width : { 70, 200, 460 }) {
				const auto layout = LayoutMapOverlayText(dc, name, width, 10000);
				wxString restored;
				for (const auto& line : layout.lines) {
					restored += line;
					Check(dc.GetTextExtent(line).x <= width, "wrapped name fits available width at every DPI");
				}
				Check(restored == name, "full Unicode item name survives wrapping");
				Check(layout.height >= dc.GetCharHeight(), "height includes font metrics");
			}
			const wxString token('W', 300);
			const auto longWord = LayoutMapOverlayText(dc, token, 200, 10000);
			wxString restored;
			for (const auto& line : longWord.lines) {
				restored += line;
				Check(dc.GetTextExtent(line).x <= 200, "unbroken names wrap too");
			}
			Check(restored == token, "no legacy 255-character truncation");
			const auto caption = LayoutMapOverlayText(dc, "Sarcophagus", 320, 200);
			Check(caption.lines.size() == 1 && caption.width == dc.GetTextExtent("Sarcophagus").x, "one-slot container caption uses full measured width");
			const wxString details = "Item ID: 1234\nAction ID: 4500\nUnique ID: 5001\nContainer contents (1/20):\n  ancient guardian's relic (Item ID: 2160)";
			const auto lines = LayoutMapOverlayText(dc, details, 460, 2000);
			Check(lines.hiddenLines == 0, "normal item details fit without omission");
			wxString visible;
			for (const auto& line : lines.lines) {
				visible += line;
			}
			Check(visible.Contains("1234") && visible.Contains("4500") && visible.Contains("5001") && visible.Contains("2160"), "all item and attribute IDs are retained");
			const auto limited = LayoutMapOverlayText(dc, details, 300, pixels * 3);
			Check(limited.hiddenLines > 0 && limited.lines.back().Contains("+"), "physical viewport overflow is explicit");
			Check(limited.height <= pixels * 3, "overflow stays inside viewport");
			Check(LayoutMapOverlayText(dc, "", 200, 200).width >= 1, "empty text is safe");
		}
	}
	wxTheApp->OnExit();
	wxEntryCleanup();
	std::cout << "PASS: tooltip layout, full names, IDs, containers, Unicode, long words, viewport bounds and 100/125/150/200% font scaling\n";
	return 0;
}
