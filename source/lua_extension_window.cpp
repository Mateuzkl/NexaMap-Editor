// SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"
#include "lua_extension_window.h"

#include "editor.h"
#include "gui.h"
#include "lua_extension_editor.h"
#include "lua_extension_registry.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>

namespace {

	class LuaExtensionsDialog final : public wxDialog {
	public:
		explicit LuaExtensionsDialog(wxWindow* parent) :
			wxDialog(parent, wxID_ANY, "Lua Extensions", wxDefaultPosition, wxSize(760, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
			auto* root = new wxBoxSizer(wxVERTICAL);
			auto* heading = new wxStaticText(this, wxID_ANY, "Lua Extensions — preview before applying");
			root->Add(heading, 0, wxALL, 10);
			auto* columns = new wxBoxSizer(wxHORIZONTAL);
			auto* left = new wxBoxSizer(wxVERTICAL);
			search_ = new wxTextCtrl(this, wxID_ANY);
			search_->SetHint("Search extensions...");
			left->Add(search_, 0, wxEXPAND | wxBOTTOM, 6);
			list_ = new wxListBox(this, wxID_ANY);
			left->Add(list_, 1, wxEXPAND);
			columns->Add(left, 1, wxEXPAND | wxRIGHT, 10);
			auto* right = new wxBoxSizer(wxVERTICAL);
			info_ = new wxStaticText(this, wxID_ANY, "Select an extension, then click Inspect or Preview. Scripts are never run on discovery.");
			info_->Wrap(400);
			right->Add(info_, 0, wxEXPAND | wxBOTTOM, 8);
			parametersPanel_ = new wxPanel(this);
			parametersSizer_ = new wxFlexGridSizer(2, 6, 8);
			parametersSizer_->AddGrowableCol(1, 1);
			parametersPanel_->SetSizer(parametersSizer_);
			right->Add(parametersPanel_, 0, wxEXPAND | wxBOTTOM, 8);
			preview_ = new wxTextCtrl(this, wxID_ANY, "No preview yet.", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
			right->Add(preview_, 1, wxEXPAND);
			columns->Add(right, 2, wxEXPAND);
			root->Add(columns, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
			auto* actions = new wxBoxSizer(wxHORIZONTAL);
			auto addButton = [&](const wxString& title) {
				auto* button = new wxButton(this, wxID_ANY, title);
				actions->Add(button, 0, wxRIGHT, 6);
				return button;
			};
			auto* reload = addButton("Reload");
			auto* openFolder = addButton("Open Folder");
			auto* inspect = addButton("Inspect");
			auto* randomize = addButton("Randomize Seed");
			auto* preview = addButton("Preview");
			apply_ = addButton("Apply Preview");
			apply_->Enable(false);
			actions->AddStretchSpacer();
			auto* close = addButton("Close");
			root->Add(actions, 0, wxEXPAND | wxALL, 10);
			SetSizer(root);
			search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Populate(); });
			list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { SelectionChanged(); });
			reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LuaExtension::Registry::Instance().Reload(); ClearPreview(); Populate(); });
			openFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenUserFolder(); });
			inspect->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Inspect(); });
			randomize->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RandomizeSeed(); });
			preview->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Preview(); });
			apply_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Apply(); });
			close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); });
			LuaExtension::Registry::Instance().Reload();
			Populate();
		}

	private:
		struct ParameterControl {
			std::string name;
			std::string type;
			wxWindow* control = nullptr;
			nlohmann::json schema;
		};

		std::string SelectedId() const {
			const int selected = list_->GetSelection();
			return selected == wxNOT_FOUND || static_cast<size_t>(selected) >= visibleIds_.size() ? std::string() : visibleIds_[selected];
		}

		void Populate() {
			const std::string previous = SelectedId();
			list_->Clear();
			visibleIds_.clear();
			const wxString query = search_->GetValue().Lower();
			for (const auto& extension : LuaExtension::Registry::Instance().Entries()) {
				const wxString label = wxString::FromUTF8(extension.metadata.name) + "  [" + wxString::FromUTF8(extension.id) + "]";
				if (!query.empty() && !label.Lower().Contains(query)) {
					continue;
				}
				visibleIds_.push_back(extension.id);
				list_->Append(label);
				if (extension.id == previous) {
					list_->SetSelection(static_cast<int>(visibleIds_.size() - 1));
				}
			}
		}

		void ClearPreview() {
			plan_.reset();
			previewId_.clear();
			previewSource_.clear();
			apply_->Enable(false);
			preview_->SetValue("No preview yet.");
		}

		void SelectionChanged() {
			ClearPreview();
			parameters_.clear();
			parametersSizer_->Clear(true);
			parametersPanel_->Layout();
			const auto* extension = LuaExtension::Registry::Instance().Find(SelectedId());
			if (!extension) {
				return;
			}
			info_->SetLabel("Source: " + wxString::FromUTF8(extension->path.string()) + "\nClick Inspect to read metadata. Preview does not modify the map.");
			Layout();
		}

		const LuaExtension::RegisteredExtension* Inspect() {
			const std::string id = SelectedId();
			if (id.empty()) {
				preview_->SetValue("Select an extension first.");
				return nullptr;
			}
			const auto* extension = LuaExtension::Registry::Instance().Inspect(id);
			if (!extension) {
				return nullptr;
			}
			if (!extension->error.empty()) {
				preview_->SetValue(wxString::FromUTF8(extension->error));
				return nullptr;
			}
			info_->SetLabel(wxString::FromUTF8(extension->metadata.name) + "  v" + wxString::FromUTF8(extension->metadata.version) + "\n" + wxString::FromUTF8(extension->metadata.description) + "\nSource: " + wxString::FromUTF8(extension->path.string()));
			BuildParameters(extension->metadata.parameters);
			Layout();
			return extension;
		}

		void BuildParameters(const nlohmann::json& schemas) {
			parameters_.clear();
			parametersSizer_->Clear(true);
			for (const auto& schema : schemas) {
				if (!schema.is_object() || !schema.contains("name") || !schema.contains("type")) {
					continue;
				}
				const std::string name = schema["name"].get<std::string>();
				const std::string type = schema["type"].get<std::string>();
				parametersSizer_->Add(new wxStaticText(parametersPanel_, wxID_ANY, wxString::FromUTF8(name)), 0, wxALIGN_CENTER_VERTICAL);
				wxWindow* control = nullptr;
				if (type == "boolean") {
					auto* checkbox = new wxCheckBox(parametersPanel_, wxID_ANY, "");
					checkbox->SetValue(schema.value("default", false));
					control = checkbox;
				} else if (type == "enum" && schema.contains("options") && schema["options"].is_array()) {
					auto* choice = new wxChoice(parametersPanel_, wxID_ANY);
					for (const auto& option : schema["options"]) {
						if (option.is_string()) {
							choice->Append(wxString::FromUTF8(option.get<std::string>()));
						}
					}
					if (choice->GetCount()) {
						choice->SetSelection(0);
					}
					control = choice;
				} else {
					std::string value;
					if (schema.contains("default")) {
						const auto& initial = schema["default"];
						value = initial.is_string() ? initial.get<std::string>() : initial.dump();
					}
					control = new wxTextCtrl(parametersPanel_, wxID_ANY, wxString::FromUTF8(value));
				}
				parametersSizer_->Add(control, 1, wxEXPAND);
				parameters_.push_back({ name, type, control, schema });
			}
			parametersPanel_->Layout();
		}

		nlohmann::json ReadParameters() const {
			nlohmann::json values = nlohmann::json::object();
			for (const auto& parameter : parameters_) {
				if (parameter.type == "boolean") {
					values[parameter.name] = static_cast<wxCheckBox*>(parameter.control)->GetValue();
				} else if (parameter.type == "enum") {
					values[parameter.name] = static_cast<wxChoice*>(parameter.control)->GetStringSelection().ToStdString();
				} else {
					const std::string input = static_cast<wxTextCtrl*>(parameter.control)->GetValue().ToStdString();
					if (parameter.type == "integer" || parameter.type == "seed" || parameter.type == "item") {
						values[parameter.name] = std::stoll(input);
					} else if (parameter.type == "number") {
						values[parameter.name] = std::stod(input);
					} else {
						values[parameter.name] = input;
					}
				}
				if (parameter.schema.contains("min") && values[parameter.name].is_number() && values[parameter.name].get<double>() < parameter.schema["min"].get<double>()) {
					throw std::runtime_error(parameter.name + " is below its minimum.");
				}
				if (parameter.schema.contains("max") && values[parameter.name].is_number() && values[parameter.name].get<double>() > parameter.schema["max"].get<double>()) {
					throw std::runtime_error(parameter.name + " exceeds its maximum.");
				}
			}
			return values;
		}

		void Preview() {
			ClearPreview();
			const auto* extension = Inspect();
			if (!extension) {
				return;
			}
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor) {
				preview_->SetValue("Open a map and select a target area first.");
				return;
			}
			std::string source, error;
			if (!LuaExtension::Registry::Instance().ReadSource(*extension, source, error)) {
				preview_->SetValue(wxString::FromUTF8(error));
				return;
			}
			auto context = LuaExtension::CaptureContext(*editor, error);
			if (!error.empty()) {
				preview_->SetValue(wxString::FromUTF8(error));
				return;
			}
			try {
				plan_ = LuaExtension::Runtime::Preview(source, extension->path.filename().string(), context, ReadParameters());
				if (!plan_->valid()) {
					preview_->SetValue(wxString::FromUTF8(plan_->error));
					plan_.reset();
					return;
				}
				if (!LuaExtension::ValidatePlan(*editor, *plan_, error)) {
					preview_->SetValue(wxString::FromUTF8(error));
					plan_.reset();
					return;
				}
				std::map<std::string, size_t> byBrush;
				for (const auto& operation : plan_->operations) {
					++byBrush[operation.kind == LuaExtension::OperationKind::Brush ? operation.brush : "Raw item " + std::to_string(operation.itemId)];
				}
				wxString summary = wxString::Format("Validated preview: %zu operations in %zu selected tiles.\nSeed: %llu\nNo map changes yet.\n\n", plan_->operations.size(), context.selection.size(), static_cast<unsigned long long>(plan_->seed));
				for (const auto& [name, count] : byBrush) {
					summary += wxString::Format("%s: %zu\n", wxString::FromUTF8(name), count);
				}
				preview_->SetValue(summary);
				previewId_ = extension->id;
				previewSource_ = std::move(source);
				apply_->Enable(true);
			} catch (const std::exception& exception) {
				preview_->SetValue(wxString::FromUTF8(exception.what()));
				plan_.reset();
			}
		}

		void RandomizeSeed() {
			for (auto& parameter : parameters_) {
				if (parameter.type == "seed" || (parameter.name == "seed" && parameter.type == "integer")) {
					const uint32_t seed = std::random_device {}();
					static_cast<wxTextCtrl*>(parameter.control)->SetValue(wxString::Format("%u", seed));
					ClearPreview();
					return;
				}
			}
			preview_->SetValue("This extension has no seed parameter.");
		}

		void Apply() {
			if (!plan_ || previewId_ != SelectedId()) {
				ClearPreview();
				return;
			}
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor) {
				preview_->SetValue("Map closed. Preview again.");
				ClearPreview();
				return;
			}
			const auto* extension = LuaExtension::Registry::Instance().Find(previewId_);
			std::string source, error;
			if (!extension || !LuaExtension::Registry::Instance().ReadSource(*extension, source, error) || source != previewSource_) {
				preview_->SetValue("Extension file changed since preview. Reload and preview again.");
				plan_.reset();
				apply_->Enable(false);
				return;
			}
			if (!LuaExtension::ApplyPlan(*editor, *plan_, error)) {
				preview_->SetValue(wxString::FromUTF8(error));
			} else {
				preview_->SetValue(wxString::Format("Applied %zu operations as one Undo transaction.", plan_->operations.size()));
			}
			plan_.reset();
			apply_->Enable(false);
		}

		void OpenUserFolder() {
			const auto directories = LuaExtension::Registry::Instance().Directories();
			if (directories.size() < 2) {
				return;
			}
			std::error_code error;
			std::filesystem::create_directories(directories[1], error);
			if (error) {
				preview_->SetValue("Could not create user extension directory.");
				return;
			}
			wxLaunchDefaultApplication(wxString(directories[1].wstring()));
		}

		wxTextCtrl* search_ = nullptr;
		wxListBox* list_ = nullptr;
		wxStaticText* info_ = nullptr;
		wxPanel* parametersPanel_ = nullptr;
		wxFlexGridSizer* parametersSizer_ = nullptr;
		wxTextCtrl* preview_ = nullptr;
		wxButton* apply_ = nullptr;
		std::vector<std::string> visibleIds_;
		std::vector<ParameterControl> parameters_;
		std::optional<LuaExtension::Plan> plan_;
		std::string previewId_;
		std::string previewSource_;
	};

} // namespace

void ShowLuaExtensionsDialog(wxWindow* parent) {
	LuaExtensionsDialog dialog(parent);
	dialog.ShowModal();
}
