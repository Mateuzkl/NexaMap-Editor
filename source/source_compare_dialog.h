//////////////////////////////////////////////////////////////////////
// Side-by-side comparison for externally changed editor sources.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SOURCE_COMPARE_DIALOG_H_
#define NEXAMAP_SOURCE_COMPARE_DIALOG_H_

#include "editor_source_monitor.h"

class wxWindow;

enum class SourceConflictChoice {
	KeepEditing,
	Reopen,
};

[[nodiscard]] SourceConflictChoice ShowSourceConflictDialog(wxWindow* parent, const std::vector<EditorSourceChange>& changes);

#endif // NEXAMAP_SOURCE_COMPARE_DIALOG_H_
