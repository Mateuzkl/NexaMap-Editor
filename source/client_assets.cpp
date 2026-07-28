#include "main.h"

#include "client_assets.h"

#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "settings.h"
#include "sprite_appearances.h"

#include <appearances.pb.h>
#include <fstream>
#include <google/protobuf/stubs/common.h>

wxString ClientAssets::path;
std::string ClientAssets::versionName;
bool ClientAssets::loaded = false;

namespace {
	std::filesystem::path ToFilesystemPath(const wxString& path) {
#ifdef __WINDOWS__
		return std::filesystem::path(path.ToStdWstring());
#else
		return std::filesystem::u8path(path.ToStdString(wxConvUTF8));
#endif
	}
}

void ClientAssets::loadConfiguredPath() {
	path = wxstr(g_settings.getString(Config::CANARY_CRYSTAL_ASSETS_DIRECTORY));
}

void ClientAssets::saveConfiguredPath() {
	g_settings.setString(Config::CANARY_CRYSTAL_ASSETS_DIRECTORY, nstr(path));
}

bool ClientAssets::validatePath(const wxString& candidate, ClientAssetsManifest& manifest, wxString& error, wxArrayString& warnings) {
	const ClientAssetsValidationResult validation = ClientAssetsManifestLoader::Validate(ToFilesystemPath(candidate));
	if (!validation.valid) {
		error = wxstr(validation.error);
		wxLogError("Canary/Crystal Assets validation failed: " + error);
		return false;
	}
	manifest = validation.manifest;
	for (const std::string& warning : manifest.warnings) {
		warnings.push_back(wxstr(warning));
		wxLogWarning("Canary/Crystal Assets: " + wxstr(warning));
	}
	return true;
}

bool ClientAssets::load(wxString& error, wxArrayString& warnings) {
	unload();

	ClientAssetsManifest manifest;
	if (!validatePath(path, manifest, error, warnings)) {
		return false;
	}
	if (!g_spriteAppearances.loadCatalog(manifest, error, warnings)) {
		wxLogError("Canary/Crystal Assets catalog failed: " + error);
		return false;
	}

	std::ifstream appearanceStream(manifest.appearancesFile, std::ios::in | std::ios::binary);
	if (!appearanceStream.is_open()) {
		error = "Could not open appearances file: " + wxstr(manifest.appearancesFile.string());
		wxLogError(error);
		g_spriteAppearances.unload();
		return false;
	}

	GOOGLE_PROTOBUF_VERIFY_VERSION;
	rme::protobuf::appearances::Appearances appearances;
	if (!appearances.ParseFromIstream(&appearanceStream)) {
		error = "The appearances file is corrupt or incompatible: " + wxstr(manifest.appearancesFile.string());
		wxLogError(error);
		g_spriteAppearances.unload();
		return false;
	}

	if (!g_items.loadFromAppearances(appearances, error, warnings)) {
		if (error.empty()) {
			error = "Could not create the Canary/Crystal item database from appearances.";
		}
		wxLogError(error);
		g_items.clear();
		g_gui.gfx.clear();
		g_spriteAppearances.unload();
		return false;
	}

	for (const auto& outfit : appearances.outfit()) {
		if (!g_gui.gfx.loadAppearanceOutfit(outfit, error, warnings)) {
			if (error.empty()) {
				error = wxString::Format("Could not load outfit appearance %u.", outfit.id());
			}
			wxLogError(error);
			g_items.clear();
			g_gui.gfx.clear();
			g_spriteAppearances.unload();
			return false;
		}
	}

	versionName = manifest.version;
	loaded = true;
	const wxString layoutName = manifest.layout == ClientAssetsLayout::OtClient ? "OTC" : "CipSoft/Crystal";
	wxLogMessage(
		"Loaded " + layoutName + " Assets version " + wxstr(versionName) + " from " + wxstr(manifest.assetsDirectory.string())
	);
	return true;
}

void ClientAssets::unload() {
	loaded = false;
	versionName.clear();
	g_spriteAppearances.unload();
}
