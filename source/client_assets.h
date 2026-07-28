#ifndef NEXAMAP_CLIENT_ASSETS_H_
#define NEXAMAP_CLIENT_ASSETS_H_

#include "client_assets_manifest.h"

#include <string>
#include <wx/arrstr.h>
#include <wx/string.h>

class ClientAssets {
public:
	static void loadConfiguredPath();
	static void saveConfiguredPath();
	static bool validatePath(const wxString& path, ClientAssetsManifest& manifest, wxString& error, wxArrayString& warnings);
	static bool load(wxString& error, wxArrayString& warnings);
	static void unload();

	static const wxString& getPath() noexcept {
		return path;
	}
	static void setPath(const wxString& newPath) {
		path = newPath;
	}
	static const std::string& getVersionName() noexcept {
		return versionName;
	}
	static bool isLoaded() noexcept {
		return loaded;
	}

private:
	static wxString path;
	static std::string versionName;
	static bool loaded;
};

#endif
