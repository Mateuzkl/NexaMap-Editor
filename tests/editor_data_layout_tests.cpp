#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int failures = 0;
	int checks = 0;

	void check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	bool IsNumericVersion(const std::filesystem::path& path) {
		const std::string name = path.filename().string();
		return !name.empty() && name.find_first_not_of("0123456789") == std::string::npos;
	}
}

int main() {
#ifdef NEXAMAP_SOURCE_DIR
	const std::filesystem::path sourceRoot = NEXAMAP_SOURCE_DIR;
#else
	const std::filesystem::path sourceRoot = std::filesystem::current_path();
#endif
	const std::filesystem::path dataRoot = sourceRoot / "data";
	const std::filesystem::path editorRoot = dataRoot / "editor";

	check(std::filesystem::is_directory(editorRoot), "canonical data/editor directory exists");
	for (const char* file : { "materials.xml", "borders.xml", "grounds.xml", "walls.xml", "doodads.xml", "tilesets.xml", "creatures.xml" }) {
		check(std::filesystem::is_regular_file(editorRoot / file), std::string("canonical ") + file + " exists");
	}

	std::ifstream materialsFile(editorRoot / "materials.xml", std::ios::binary);
	const std::string materials((std::istreambuf_iterator<char>(materialsFile)), std::istreambuf_iterator<char>());
	for (const char* include : { "borders.xml", "grounds.xml", "walls.xml", "doodads.xml", "tilesets.xml" }) {
		check(materials.find(std::string("file=\"") + include + "\"") != std::string::npos, std::string("materials.xml includes ") + include);
	}

	std::size_t numericDirectories = 0;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dataRoot)) {
		if (entry.is_directory() && IsNumericVersion(entry.path())) {
			++numericDirectories;
		}
	}
	check(numericDirectories == 0, "numeric version directories have been fully retired");
	check(std::filesystem::is_regular_file(dataRoot / "canary-crystal" / "items" / "items.xml"), "Canary/Crystal compatibility data remains isolated");

	std::ifstream monsterDialogFile(sourceRoot / "source" / "monster_editor_dialog.cpp", std::ios::binary);
	const std::string monsterDialog((std::istreambuf_iterator<char>(monsterDialogFile)), std::istreambuf_iterator<char>());
	const std::size_t saveBegin = monsterDialog.find("void MonsterEditorDialog::onSave");
	const std::size_t saveDocumentBegin = monsterDialog.find("bool MonsterEditorDialog::saveDocument", saveBegin);
	check(saveBegin != std::string::npos && saveDocumentBegin != std::string::npos, "Monster Editor save handler is present");
	if (saveBegin != std::string::npos && saveDocumentBegin != std::string::npos) {
		const std::string saveHandler = monsterDialog.substr(saveBegin, std::min<std::size_t>(200, monsterDialog.size() - saveBegin));
		check(saveHandler.find("EndModal") == std::string::npos, "Monster Editor Save and autosave keep the window open");
		check(saveHandler.find("saveDocument(true)") != std::string::npos, "Monster Editor Save uses the validated source save path");
	}
	check(monsterDialog.find("< Back to Monsters") != std::string::npos && monsterDialog.find("Browse Monsters...") != std::string::npos, "Monster Editor exposes internal back and browse navigation");
	check(monsterDialog.find("StartOnce(650)") != std::string::npos, "Monster Editor uses debounced autosave");

	std::ifstream npcDialogFile(sourceRoot / "source" / "npc_editor_dialog.cpp", std::ios::binary);
	const std::string npcDialog((std::istreambuf_iterator<char>(npcDialogFile)), std::istreambuf_iterator<char>());
	const std::size_t npcSaveBegin = npcDialog.find("void NpcEditorDialog::onSave");
	const std::size_t npcSaveDocumentBegin = npcDialog.find("bool NpcEditorDialog::saveDocument", npcSaveBegin);
	check(npcSaveBegin != std::string::npos && npcSaveDocumentBegin != std::string::npos, "NPC Editor save handler is present");
	if (npcSaveBegin != std::string::npos && npcSaveDocumentBegin != std::string::npos) {
		const std::string saveHandler = npcDialog.substr(npcSaveBegin, npcSaveDocumentBegin - npcSaveBegin);
		check(saveHandler.find("EndModal") == std::string::npos && saveHandler.find("saveDocument(true)") != std::string::npos, "NPC Editor Save validates and keeps the window open");
	}
	check(npcDialog.find("< Back to NPCs") != std::string::npos && npcDialog.find("Browse NPCs...") != std::string::npos, "NPC Editor exposes internal back and browse navigation");
	check(npcDialog.find("StartOnce(650)") != std::string::npos, "NPC Editor uses debounced autosave");

	std::ifstream spellDialogFile(sourceRoot / "source" / "spell_editor_dialog.cpp", std::ios::binary);
	const std::string spellDialog((std::istreambuf_iterator<char>(spellDialogFile)), std::istreambuf_iterator<char>());
	const std::size_t spellSaveBegin = spellDialog.find("void SpellEditorDialog::onSave");
	const std::size_t spellSaveDocumentBegin = spellDialog.find("bool SpellEditorDialog::saveDocument", spellSaveBegin);
	check(spellSaveBegin != std::string::npos && spellSaveDocumentBegin != std::string::npos, "Spell Editor save handler is present");
	if (spellSaveBegin != std::string::npos && spellSaveDocumentBegin != std::string::npos) {
		const std::string saveHandler = spellDialog.substr(spellSaveBegin, spellSaveDocumentBegin - spellSaveBegin);
		check(saveHandler.find("EndModal") == std::string::npos, "Spell Editor Save keeps the editor open");
		check(saveHandler.find("saveDocument(true)") != std::string::npos, "Spell Editor Save uses the validated provider transaction");
	}
	check(spellDialog.find("< Back to Spells") != std::string::npos && spellDialog.find("Browse Spells...") != std::string::npos, "Spell Editor exposes internal back and browse navigation");
	check(spellDialog.find("StartOnce(650)") != std::string::npos, "Spell Editor uses debounced autosave");
	check(spellDialog.find("wxVSCROLL") != std::string::npos, "Spell Editor uses scrollable content pages");
	check(spellDialog.find("Registration XML") != std::string::npos && spellDialog.find("Implementation Lua") != std::string::npos, "Spell Editor exposes paired registration and implementation sources");
	check(spellDialog.find("SpellVisualBrowserDialog") != std::string::npos && spellDialog.find("Browse...") != std::string::npos, "Spell Editor exposes visual effect and projectile browsers");
	check(spellDialog.find("North-East") != std::string::npos && spellDialog.find("South-West") != std::string::npos, "Spell visual controls expose all eight projectile directions");
	check(spellDialog.find("Animation speed") != std::string::npos && spellDialog.find("SetPlaying(true)") != std::string::npos, "Spell visual preview has play, stop and speed controls");
	check(spellDialog.find("g_workspace.getSpellAreaResolver()") != std::string::npos && spellDialog.find("g_workspace.getServerVisualCatalog()") != std::string::npos && spellDialog.find("g_workspace.getServerVocations()") != std::string::npos, "Spell editors reuse workspace-scoped metadata caches");
	check(spellDialog.find("StartOnce(80)") != std::string::npos && spellDialog.find("IsVisualField(field)") != std::string::npos, "Spell visual refreshes are field-aware and coalesced");
	check(spellDialog.find("Compare External...") != std::string::npos && spellDialog.find("onSourceWatch") != std::string::npos, "Spell Editor exposes external-change monitoring and comparison");
	check(monsterDialog.find("Compare External...") != std::string::npos && npcDialog.find("Compare External...") != std::string::npos, "Monster and NPC editors expose external-change comparison");
	std::ifstream areaDialogFile(sourceRoot / "source" / "spell_area_editor_dialog.cpp", std::ios::binary);
	const std::string areaDialog((std::istreambuf_iterator<char>(areaDialogFile)), std::istreambuf_iterator<char>());
	check(areaDialog.find("wxPanel(parent, wxID_ANY)") != std::string::npos && areaDialog.find("SetInitialSize(FromDIP") != std::string::npos, "spell-area grid applies DPI sizing only after wxPanel construction");

	std::ifstream workspaceSessionFile(sourceRoot / "source" / "workspace_session.cpp", std::ios::binary);
	const std::string workspaceSession((std::istreambuf_iterator<char>(workspaceSessionFile)), std::istreambuf_iterator<char>());
	const std::size_t configureBegin = workspaceSession.find("bool WorkspaceSession::configureServer");
	const std::size_t selectMapBegin = workspaceSession.find("bool WorkspaceSession::selectDetectedMap", configureBegin);
	check(configureBegin != std::string::npos && selectMapBegin != std::string::npos && workspaceSession.substr(configureBegin, selectMapBegin - configureBegin).find("ServerContentIndex::Build") == std::string::npos, "fast workspace activation defers the heavy content index");
	check(workspaceSession.find("ServerContentIndex::RefreshPaths") != std::string::npos, "workspace session exposes targeted content refresh");

	std::ifstream browserFile(sourceRoot / "source" / "server_content_browser_dialog.cpp", std::ios::binary);
	const std::string browser((std::istreambuf_iterator<char>(browserFile)), std::istreambuf_iterator<char>());
	check(browser.find("wxLC_VIRTUAL") != std::string::npos && browser.find("searchKeys.push_back") != std::string::npos, "content browser virtualizes rows and precomputes normalized search keys");
	check(browser.find("StartOnce(100)") != std::string::npos, "content browser debounces live filtering");

	std::ifstream guiFile(sourceRoot / "source" / "gui.cpp", std::ios::binary);
	const std::string gui((std::istreambuf_iterator<char>(guiFile)), std::istreambuf_iterator<char>());
	const std::size_t monsterEditorBegin = gui.find("void GUI::ShowMonsterEditor(const std::string&");
	const std::size_t hotkeyBegin = gui.find("void GUI::SetHotkey", monsterEditorBegin);
	check(monsterEditorBegin != std::string::npos && hotkeyBegin != std::string::npos && gui.substr(monsterEditorBegin, hotkeyBegin - monsterEditorBegin).find("rescanServer") == std::string::npos, "Monster, NPC and Spell saves avoid global server-content rescans");

	std::ifstream applicationFile(sourceRoot / "source" / "application.cpp", std::ios::binary);
	const std::string application((std::istreambuf_iterator<char>(applicationFile)), std::istreambuf_iterator<char>());
	std::ifstream settingsFile(sourceRoot / "source" / "settings.cpp", std::ios::binary);
	const std::string settings((std::istreambuf_iterator<char>(settingsFile)), std::istreambuf_iterator<char>());
	std::ifstream cmakeFile(sourceRoot / "CMakeLists.txt", std::ios::binary);
	const std::string cmake((std::istreambuf_iterator<char>(cmakeFile)), std::istreambuf_iterator<char>());
	std::ifstream projectFile(sourceRoot / "vcproj" / "Project" / "Editor.vcxproj", std::ios::binary);
	const std::string project((std::istreambuf_iterator<char>(projectFile)), std::istreambuf_iterator<char>());
	check(settings.find("Int(SHOW_DIAGNOSTIC_CONSOLE, 0)") != std::string::npos, "diagnostic console is disabled by default");
	check(application.find("StartDiagnosticConsole()") != std::string::npos && application.find("Config::SHOW_DIAGNOSTIC_CONSOLE") != std::string::npos, "diagnostic console is allocated only from the persisted startup option");
	check(cmake.find("WIN32_EXECUTABLE TRUE") != std::string::npos && project.find("<SubSystem>Console</SubSystem>") == std::string::npos, "Windows builds start without an unconditional console window");

	std::ifstream graphicsFile(sourceRoot / "source" / "graphics.cpp", std::ios::binary);
	const std::string graphics((std::istreambuf_iterator<char>(graphicsFile)), std::istreambuf_iterator<char>());
	check(graphics.find("getEffectSprite") != std::string::npos && graphics.find("getDistanceSprite") != std::string::npos, "GraphicManager exposes active-session effect and projectile sprites");
	check(graphics.find("creature_count + effect_count + distance_count") != std::string::npos, "classic DAT metadata loads items, outfits, effects and projectiles");
	check(graphics.find("swap(effect_count, other.effect_count)") != std::string::npos && graphics.find("swap(distance_count, other.distance_count)") != std::string::npos, "effect and projectile ranges swap with each EditorResourceSession");
	check(graphics.find("deferredEffectAppearances") != std::string::npos && graphics.find("materializeAppearanceVisual") != std::string::npos, "protobuf effect and projectile sprites are materialized lazily on first preview");
	check(graphics.find("g_workspace") == std::string::npos, "low-level graphics preview does not depend on global workspace state");

	std::ifstream assetsFile(sourceRoot / "source" / "client_assets.cpp", std::ios::binary);
	const std::string assets((std::istreambuf_iterator<char>(assetsFile)), std::istreambuf_iterator<char>());
	check(assets.find("appearances.effect()") != std::string::npos && assets.find("appearances.missile()") != std::string::npos, "protobuf clients load effect and missile appearances");

	if (failures == 0) {
		std::cout << checks << " editor data layout checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
