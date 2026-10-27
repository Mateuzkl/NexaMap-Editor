#include "../main.h"
#include "map_migration_window.h"
#include "map_migration.h"
#include "../gui.h"
#include "../map.h"
#include "../workspace_session.h"
#include "../file_transaction.h"
#include <wx/filepicker.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/filedlg.h>
#include <mutex>
#include <thread>

namespace MapMigrationWindowDetails {
	using namespace MapMigration;
	wxString Text(const std::string& text) {
		return wxString::FromUTF8(text);
	}
	wxString WxPath(const Path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return Text(path.string());
#endif
	}
	Path NativePath(const wxString& path) {
#ifdef __WINDOWS__
		return Path(path.ToStdWstring());
#else
		return Path(path.ToStdString(wxConvUTF8));
#endif
	}
	wxBitmap Preview(const std::shared_ptr<const Visual>& visual) {
		if (!visual || visual->preview.empty()) {
			return wxBitmap(128, 128);
		}
		wxImage image(visual->previewWidth, visual->previewHeight);
		image.InitAlpha();
		for (size_t i = 0; i < visual->preview.size() / 4; ++i) {
			std::copy_n(visual->preview.data() + i * 4, 3, image.GetData() + i * 3);
			image.GetAlpha()[i] = visual->preview[i * 4 + 3];
		}
		return wxBitmap(image.Scale(128, 128, wxIMAGE_QUALITY_NEAREST));
	}
	struct Work {
		std::mutex mutex;
		std::atomic<bool> cancel = false;
		int percent = 0;
		std::string message;
		bool done = false, converting = false, automatic = false;
		Plan plan;
		Result result;
	};
	class Dialog final : public wxDialog {
	public:
		Dialog(wxWindow* parent) :
			wxDialog(parent, wxID_ANY, Text("Converter mapa inteiro para meu cliente"), wxDefaultPosition, wxSize(1120, 850), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), timer(this) {
			workspaceGeneration = g_workspace.getGeneration();
			auto* root = new wxBoxSizer(wxVERTICAL);
			auto* intro = new wxStaticText(this, wxID_ANY, Text("Selecione os arquivos e clique em Converter automaticamente. O NexaMap compara todos os IDs e gera a saída quando há correspondências suficientes, sem clicar item por item. O original e os assets permanecem intactos."));
			intro->Wrap(1050);
			root->Add(intro, 0, wxEXPAND | wxALL, 12);
			pages = new wxNotebook(this, wxID_ANY);
			files = new wxScrolledWindow(pages);
			files->SetScrollRate(10, 10);
			auto* fileSizer = new wxBoxSizer(wxVERTICAL);
			sourceMap = Picker(files, fileSizer, "Mapa completo de origem", "*.otbm", true);
			sourceMap->Bind(wxEVT_FILEPICKER_CHANGED, [this](wxFileDirPickerEvent&) {SourceChanged();Invalidate(); });
			fileSizer->Add(new wxStaticText(files, wxID_ANY, Text("ORIGEM — recursos usados para desenhar o mapa antigo")), 0, wxTOP | wxBOTTOM, 10);
			sourceOtb = Picker(files, fileSizer, "items.otb da origem", "*.otb", true);
			sourceDat = Picker(files, fileSizer, "Tibia.dat da origem", "*.dat", true);
			sourceSpr = Picker(files, fileSizer, "Tibia.spr da origem", "*.spr", true);
			sourceXml = Picker(files, fileSizer, "items.xml da origem (opcional)", "*.xml", true);
			currentTarget = new wxCheckBox(files, wxID_ANY, Text("Usar os recursos atuais do NexaMap como destino"));
			fileSizer->Add(currentTarget, 0, wxTOP | wxBOTTOM, 14);
			targetOtb = Picker(files, fileSizer, "items.otb do meu servidor", "*.otb", true);
			targetXml = Picker(files, fileSizer, "items.xml do meu servidor (opcional)", "*.xml", true);
			targetDat = Picker(files, fileSizer, "Tibia.dat do meu client", "*.dat", true);
			targetSpr = Picker(files, fileSizer, "Tibia.spr do meu client", "*.spr", true);
			targetOtb->SetPath(WxPath(g_workspace.getServer().itemsOtbPath));
			targetXml->SetPath(WxPath(g_workspace.getServer().itemsXmlPath));
			const wxFileName dat = g_gui.gfx.getMetadataFileName(), spr = g_gui.gfx.getSpritesFileName();
			targetDat->SetPath(dat.IsOk() ? dat.GetFullPath() : wxFileName(g_workspace.getClient().rootPath, "Tibia.dat").GetFullPath());
			targetSpr->SetPath(spr.IsOk() ? spr.GetFullPath() : wxFileName(g_workspace.getClient().rootPath, "Tibia.spr").GetFullPath());
			currentTarget->SetValue(g_gui.IsVersionLoaded() && g_workspace.getClient().mode == WorkspaceClientMode::Classic && wxFileExists(targetOtb->GetPath()) && wxFileExists(targetDat->GetPath()) && wxFileExists(targetSpr->GetPath()));
			currentTarget->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {if(currentTarget->GetValue()){LoadCurrentTarget();
}TargetState();Invalidate(); });
			TargetState();
			outputMap = Picker(files, fileSizer, "Novo mapa de saída", "*.otbm", false);
			fileSizer->Add(new wxStaticText(files, wxID_ANY, Text("OTFI é lido ao lado do DAT. Sem OTB ou gráficos da origem, a ferramenta informa a limitação e não inventa equivalências.")), 0, wxTOP | wxBOTTOM, 12);
			files->SetSizer(fileSizer);
			pages->AddPage(files, Text("1. Arquivos"));
			auto* review = new wxPanel(pages);
			auto* reviewSizer = new wxBoxSizer(wxVERTICAL);
			summary = new wxStaticText(review, wxID_ANY, Text("Selecione os arquivos e clique em Analisar."));
			reviewSizer->Add(summary, 0, wxEXPAND | wxALL, 8);
			auto* filters = new wxBoxSizer(wxHORIZONTAL);
			roleFilter = new wxChoice(review, wxID_ANY);
			for (const auto& role : { "Todos", "Ground", "Wall", "Border", "Door", "Window", "Container", "Special", "Decoration" }) {
				roleFilter->Append(Text(role));
			}
			roleFilter->SetSelection(0);
			stateFilter = new wxChoice(review, wxID_ANY);
			for (const auto& state : { "Todos os estados", "Somente pendentes", "Automáticos", "Revisados" }) {
				stateFilter->Append(Text(state));
			}
			stateFilter->SetSelection(0);
			filters->Add(roleFilter, 0, wxRIGHT, 8);
			filters->Add(stateFilter, 0, wxRIGHT, 8);
			auto* exceptions = new wxButton(review, wxID_ANY, Text("Revisar exceções"));
			exceptions->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {stateFilter->SetSelection(1);FillRows(); });
			filters->Add(exceptions);
			reviewSizer->Add(filters, 0, wxALL, 8);
			roleFilter->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { FillRows(); });
			stateFilter->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { FillRows(); });
			list = new wxListCtrl(review, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
			const std::array<wxString, 7> columns = { Text("ServerID origem"), Text("ClientID origem"), Text("ServerID destino"), Text("Papel"), Text("Decisão"), Text("Ocorrências"), Text("Motivo") };
			for (size_t i = 0; i < columns.size(); ++i) {
				list->InsertColumn(static_cast<long>(i), columns[i], wxLIST_FORMAT_LEFT, i == 6 ? 420 : i == 4 ? 155
																											   : 115);
			}
			list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { SelectionChanged(); });
			reviewSizer->Add(list, 1, wxEXPAND | wxALL, 8);
			auto* detail = new wxBoxSizer(wxHORIZONTAL);
			sourcePreview = new wxStaticBitmap(review, wxID_ANY, wxBitmap(128, 128));
			targetPreview = new wxStaticBitmap(review, wxID_ANY, wxBitmap(128, 128));
			detail->Add(sourcePreview, 0, wxALL, 8);
			detail->Add(targetPreview, 0, wxALL, 8);
			auto* choices = new wxBoxSizer(wxVERTICAL);
			candidates = new wxChoice(review, wxID_ANY);
			candidates->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {auto row=Selected();if(!row){return;
}const auto index=candidates->GetSelection();if(index>=0&&static_cast<size_t>(index)<row->candidates.size()){targetId->SetValue(row->candidates[index].serverId);TargetPreview();} });
			choices->Add(candidates, 0, wxEXPAND | wxBOTTOM, 8);
			auto* resolution = new wxBoxSizer(wxHORIZONTAL);
			targetId = new wxSpinCtrl(review, wxID_ANY);
			targetId->SetRange(1, 65535);
			targetId->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { TargetPreview(); });
			resolution->Add(targetId, 0, wxRIGHT, 8);
			resolve = new wxButton(review, wxID_ANY, Text("Usar este ID no mapa inteiro"));
			resolve->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResolveSelected(); });
			resolution->Add(resolve);
			choices->Add(resolution, 0, wxBOTTOM, 8);
			auto* navigate = new wxButton(review, wxID_ANY, Text("Ir para ocorrência na aba aberta"));
			navigate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(); });
			choices->Add(navigate);
			choices->Add(new wxStaticText(review, wxID_ANY, Text("Esquerda: origem. Direita: destino. O preview mostra a composição inicial; os automáticos exatos verificam todas as variações.")), 0, wxTOP, 8);
			detail->Add(choices, 1, wxEXPAND | wxALL, 8);
			reviewSizer->Add(detail, 0, wxEXPAND);
			review->SetSizer(reviewSizer);
			pages->AddPage(review, Text("2. Comparação e revisão"));
			report = new wxTextCtrl(pages, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
			pages->AddPage(report, Text("3. Resultado e diagnóstico"));
			root->Add(pages, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
			approximation = new wxCheckBox(this, wxID_ANY, Text("Gerar OTBM de teste priorizando aparência (aceita propriedades, bloqueio e velocidade do destino)"));
			approximation->SetValue(true);
			root->Add(approximation, 0, wxLEFT | wxRIGHT | wxTOP, 12);
			status = new wxStaticText(this, wxID_ANY, Text("Pronto para analisar."));
			root->Add(status, 0, wxEXPAND | wxALL, 8);
			gauge = new wxGauge(this, wxID_ANY, 100);
			root->Add(gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
			auto* actions = new wxBoxSizer(wxHORIZONTAL);
			automatic = new wxButton(this, wxID_ANY, Text("Converter automaticamente"));
			analyze = new wxButton(this, wxID_ANY, Text("Só analisar"));
			generate = new wxButton(this, wxID_ANY, Text("Gerar novo mapa"));
			exportReport = new wxButton(this, wxID_ANY, Text("Exportar relatório"));
			saveMappings = new wxButton(this, wxID_ANY, Text("Salvar decisões"));
			loadMappings = new wxButton(this, wxID_ANY, Text("Carregar decisões"));
			cancel = new wxButton(this, wxID_CANCEL, Text("Fechar"));
			auto* primaryActions = new wxBoxSizer(wxHORIZONTAL);
			for (auto* button : { automatic, analyze, generate }) {
				primaryActions->Add(button, 0, wxRIGHT, 8);
			}
			root->Add(primaryActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
			for (auto* button : { exportReport, saveMappings, loadMappings }) {
				actions->Add(button, 0, wxRIGHT, 8);
			}
			actions->AddStretchSpacer();
			actions->Add(cancel);
			root->Add(actions, 0, wxEXPAND | wxALL, 12);
			SetSizer(root);
			SetMinSize(wxSize(900, 650));
			CentreOnParent();
			analyze->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartAnalysis(); });
			automatic->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartAutomatic(); });
			generate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartConversion(); });
			exportReport->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Export(); });
			saveMappings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Mappings(true); });
			loadMappings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Mappings(false); });
			cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CancelOrClose(); });
			Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {if(busy){closing=true;work->cancel=true;event.Veto();}else{ EndModal(wxID_CANCEL);
} });
			Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
			timer.Start(80);
			for (auto* picker : { sourceOtb, sourceDat, sourceSpr, sourceXml, targetOtb, targetXml, targetDat, targetSpr }) {
				picker->Bind(wxEVT_FILEPICKER_CHANGED, [this](wxFileDirPickerEvent&) { Invalidate(); });
			}
			outputMap->Bind(wxEVT_FILEPICKER_CHANGED, [this](wxFileDirPickerEvent&) { plan.options.outputMap = NativePath(outputMap->GetPath()); });
			UpdateButtons();
		}
		~Dialog() override {
			timer.Stop();
			if (work) {
				work->cancel = true;
			}
			if (worker.joinable()) {
				worker.join();
			}
		}
		Path output() const {
			return result.outputMap;
		}
		bool canOpen() const {
			return currentTarget->GetValue() && workspaceGeneration == g_workspace.getGeneration() && g_gui.IsVersionLoaded() && !g_workspace.getServer().trackedResourcesChanged();
		}

	private:
		wxFilePickerCtrl* Picker(wxWindow* parent, wxSizer* sizer, const std::string& label, const wxString& wildcard, bool input) {
			auto* row = new wxBoxSizer(wxHORIZONTAL);
			row->Add(new wxStaticText(parent, wxID_ANY, Text(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
			auto* picker = new wxFilePickerCtrl(parent, wxID_ANY, "", Text(label), wildcard, wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL | (input ? (wxFLP_OPEN | wxFLP_FILE_MUST_EXIST) : wxFLP_SAVE));
			row->Add(picker, 1, wxEXPAND);
			sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 9);
			return picker;
		}
		void SourceChanged() {
			const auto source = NativePath(sourceMap->GetPath());
			if (source.empty()) {
				return;
			}
			for (const auto& [picker, name] : std::array<std::pair<wxFilePickerCtrl*, const char*>, 4> { { { sourceOtb, "items.otb" }, { sourceDat, "Tibia.dat" }, { sourceSpr, "Tibia.spr" }, { sourceXml, "items.xml" } } }) {
				picker->SetPath(wxString());
				const auto path = source.parent_path() / name;
				if (std::filesystem::is_regular_file(path)) {
					picker->SetPath(WxPath(path));
				}
			}
			auto name = source.stem();
			name += "_convertido.otbm";
			outputMap->SetPath(WxPath(source.parent_path() / "convertido" / name));
		}
		void LoadCurrentTarget() {
			targetOtb->SetPath(WxPath(g_workspace.getServer().itemsOtbPath));
			targetXml->SetPath(WxPath(g_workspace.getServer().itemsXmlPath));
			targetDat->SetPath(g_gui.gfx.getMetadataFileName().GetFullPath());
			targetSpr->SetPath(g_gui.gfx.getSpritesFileName().GetFullPath());
		}
		void TargetState() {
			for (auto* picker : { targetOtb, targetXml, targetDat, targetSpr }) {
				picker->Enable(!currentTarget->GetValue());
			}
		}
		void Invalidate() {
			if (!busy) {
				plan = {};
				result = {};
				list->DeleteAllItems();
				summary->SetLabel(Text("Os arquivos mudaram. Analise novamente."));
				UpdateButtons();
			}
		}
		Options OptionsFromControls() const {
			Options value;
			value.sourceMap = NativePath(sourceMap->GetPath());
			value.outputMap = NativePath(outputMap->GetPath());
			value.source = { NativePath(sourceOtb->GetPath()), NativePath(sourceXml->GetPath()), NativePath(sourceDat->GetPath()), NativePath(sourceSpr->GetPath()), {} };
			value.target = { NativePath(targetOtb->GetPath()), NativePath(targetXml->GetPath()), NativePath(targetDat->GetPath()), NativePath(targetSpr->GetPath()), {} };
			return value;
		}
		void UpdateButtons() {
			automatic->Enable(!busy);
			approximation->Enable(!busy);
			analyze->Enable(!busy);
			generate->Enable(!busy && plan.ready());
			exportReport->Enable(!busy && plan.source);
			saveMappings->Enable(!busy && plan.target);
			loadMappings->Enable(!busy && plan.target);
			resolve->Enable(!busy && plan.target);
			files->Enable(!busy);
			cancel->SetLabel(Text(busy ? "Cancelar" : "Fechar"));
			if (!busy) {
				TargetState();
			}
		}
		void Begin(bool converting, bool automaticRun = false) {
			if (worker.joinable()) {
				worker.join();
			}
			work = std::make_shared<Work>();
			work->converting = converting;
			work->automatic = automaticRun;
			busy = true;
			UpdateButtons();
			const auto state = work;
			Control control;
			control.cancelled = &state->cancel;
			control.progress = [state](int p, const std::string& text) {std::lock_guard lock(state->mutex);state->percent=p;state->message=text; };
			if (automaticRun) {
				const auto options = OptionsFromControls();
				const auto previous = plan;
				const auto policy = approximation->GetValue() ? AutomaticPolicy::VisualDraft : AutomaticPolicy::Safe;
				worker = std::jthread([state, options, previous, policy, control] {
					Plan value = previous;
					Result outcome;
					try {
						if (!value.source || !value.target || !value.error.empty() || value.cancelled) {
							value = Analyze(options, control);
						}
						value.options.outputMap = options.outputMap;
						if (!value.error.empty()) {
							throw std::runtime_error(value.error);
						}
						ResolveAutomatically(value, policy, control);
						if (value.ready()) {
							outcome = Convert(value, control);
						} else {
							outcome.error = "O processamento automático terminou. " + std::to_string(value.rows.size() - value.unresolvedIds()) + " IDs resolvidos; " + std::to_string(value.unresolvedIds()) + " ainda não têm equivalente demonstrável. Nenhum mapa foi publicado: a maior pontuação sozinha pode representar outro objeto.\n\nO diagnóstico foi salvo para localizar as diferenças.";
							if (!value.options.outputMap.empty()) {
								outcome.jsonReport = value.options.outputMap;
								outcome.jsonReport.replace_extension(".automatic-analysis.json");
								outcome.csvReport = value.options.outputMap;
								outcome.csvReport.replace_extension(".automatic-analysis.csv");
								std::filesystem::create_directories(std::filesystem::absolute(outcome.jsonReport).parent_path());
								WriteReports(value, outcome.jsonReport, outcome.csvReport);
							}
						}
					} catch (const std::exception& error) {
						outcome.error = error.what();
						outcome.cancelled = state->cancel.load();
					}
					std::lock_guard lock(state->mutex);
					state->plan = std::move(value);
					state->result = std::move(outcome);
					state->done = true;
				});
			} else if (converting) {
				const auto snapshot = plan;
				worker = std::jthread([state, snapshot, control] {auto value=Convert(snapshot,control);std::lock_guard lock(state->mutex);state->result=std::move(value);state->done=true; });
			} else {
				const auto options = OptionsFromControls();
				worker = std::jthread([state, options, control] {auto value=Analyze(options,control);std::lock_guard lock(state->mutex);state->plan=std::move(value);state->done=true; });
			}
		}
		void StartAnalysis() {
			workspaceGeneration = g_workspace.getGeneration();
			Begin(false);
		}
		void StartAutomatic() {
			if (outputMap->GetPath().empty()) {
				wxMessageBox(Text("Selecione o caminho do novo mapa de saída na aba Arquivos."), Text("Saída necessária"), wxOK | wxICON_INFORMATION, this);
				return;
			}
			if (approximation->GetValue()) {
				auto output = NativePath(outputMap->GetPath());
				if (!output.empty() && !WxPath(output.stem()).EndsWith("_teste")) {
					auto name = output.stem();
					name += "_teste.otbm";
					outputMap->SetPath(WxPath(output.parent_path() / name));
				}
			}
			workspaceGeneration = g_workspace.getGeneration();
			Begin(false, true);
		}
		void StartConversion() {
			if (!plan.ready()) {
				return;
			}
			plan.options.outputMap = NativePath(outputMap->GetPath());
			if (std::filesystem::exists(plan.options.outputMap)) {
				wxMessageBox(Text("Escolha um nome novo para a saída; nenhum arquivo existente será sobrescrito."), Text("Saída já existe"), wxOK | wxICON_INFORMATION, this);
				return;
			}
			Begin(true);
		}
		void Poll() {
			if (!busy || !work) {
				return;
			}
			bool done, converting;
			{
				std::lock_guard lock(work->mutex);
				gauge->SetValue(work->percent);
				status->SetLabel(Text(work->message));
				done = work->done;
				converting = work->converting || work->automatic;
			}
			if (!done) {
				return;
			}
			if (worker.joinable()) {
				worker.join();
			}
			busy = false;
			if (work->automatic) {
				plan = std::move(work->plan);
				FillRows();
			}
			if (converting) {
				result = std::move(work->result);
				if (result.success) {
					const bool approximate = plan.visualDraft || std::any_of(plan.rows.begin(), plan.rows.end(), [](const auto& row) { return row.decision == Decision::Approximate; });
					report->SetValue(Text(approximate ? "Mapa para teste gerado automaticamente. A estrutura foi validada; existem aproximações visuais registradas no relatório.\n\nSaída: " : "Mapa inteiro convertido, reaberto pelo validador e preservado estruturalmente.\n\nSaída: ") + WxPath(result.outputMap) + Text("\nRelatório JSON: ") + WxPath(result.jsonReport) + Text("\nRelatório CSV: ") + WxPath(result.csvReport));
					pages->SetSelection(2);
					UpdateButtons();
					if (canOpen()) {
						EndModal(wxID_OK);
						return;
					}
				} else {
					report->SetValue(Text(result.error) + Text("\n\nJSON: ") + WxPath(result.jsonReport) + Text("\nCSV: ") + WxPath(result.csvReport));
					pages->SetSelection(2);
				}
			} else {
				plan = std::move(work->plan);
				FillRows();
				if (!plan.error.empty()) {
					report->SetValue(Text(plan.error));
					pages->SetSelection(2);
				} else {
					pages->SetSelection(1);
				}
			}
			UpdateButtons();
			if (closing) {
				EndModal(wxID_CANCEL);
			}
		}
		void FillRows() {
			list->Freeze();
			list->DeleteAllItems();
			for (size_t i = 0; i < plan.rows.size(); ++i) {
				const auto& row = plan.rows[i];
				const auto role = RoleName(row.roles);
				if (roleFilter->GetSelection() > 0 && roleFilter->GetStringSelection() != Text(role)) {
					continue;
				}
				const int filter = stateFilter->GetSelection();
				if ((filter == 1 && row.resolved()) || (filter == 2 && (!row.resolved() || row.decision == Decision::Reviewed)) || (filter == 3 && row.decision != Decision::Reviewed)) {
					continue;
				}
				const long index = list->InsertItem(list->GetItemCount(), wxString::Format("%u", row.sourceId));
				list->SetItemData(index, i);
				list->SetItem(index, 1, wxString::Format("%u", row.sourceClientId));
				const auto targetLabel = !row.patternTargets.empty() ? Text("por posição") : row.targetId ? wxString::Format("%u", row.targetId)
																										  : Text("—");
				list->SetItem(index, 2, targetLabel);
				list->SetItem(index, 3, Text(role));
				list->SetItem(index, 4, Text(DecisionName(row.decision)));
				list->SetItem(index, 5, wxString::Format("%llu", static_cast<unsigned long long>(row.usage.occurrences)));
				list->SetItem(index, 6, Text(row.reason));
			}
			list->Thaw();
			summary->SetLabel(Text(std::to_string(plan.inventory.tiles) + " tiles | " + std::to_string(plan.inventory.items) + " itens | " + std::to_string(plan.rows.size()) + " IDs | " + std::to_string(plan.unresolvedIds()) + " IDs pendentes (" + std::to_string(plan.unresolvedOccurrences()) + " ocorrências). Automáticos seguros já estão no plano."));
			std::string text = "Diagnóstico da origem\nTiles sem ground: " + std::to_string(plan.inventory.tilesWithoutGround) + "\nItens aninhados: " + std::to_string(plan.inventory.nestedItems) + "\n";
			for (const auto& warning : plan.warnings) {
				text += "\n" + warning;
			}
			report->SetValue(Text(text));
			UpdateButtons();
		}
		MappingRow* Selected() {
			const auto index = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (index < 0) {
				return nullptr;
			}
			const auto row = static_cast<size_t>(list->GetItemData(index));
			return row < plan.rows.size() ? &plan.rows[row] : nullptr;
		}
		void SelectionChanged() {
			auto* row = Selected();
			if (!row || !plan.source || !plan.target) {
				return;
			}
			candidates->Clear();
			if (auto found = plan.source->items.find(row->sourceId); found != plan.source->items.end()) {
				sourcePreview->SetBitmap(Preview(found->second.visual));
			}
			for (const auto& candidate : row->candidates) {
				const auto& item = plan.target->items.at(candidate.serverId);
				candidates->Append(Text(std::to_string(candidate.serverId) + " / CID " + std::to_string(candidate.clientId) + " — " + item.name + " (" + std::to_string(int(candidate.score * 100)) + "%)"));
			}
			if (!row->candidates.empty()) {
				candidates->SetSelection(0);
			}
			targetId->SetValue(row->targetId ? row->targetId : row->candidates.empty() ? 1
																					   : row->candidates.front().serverId);
			TargetPreview();
		}
		void TargetPreview() {
			if (!plan.target) {
				return;
			}
			auto found = plan.target->items.find(static_cast<uint16_t>(targetId->GetValue()));
			targetPreview->SetBitmap(Preview(found == plan.target->items.end() ? nullptr : found->second.visual));
		}
		void ResolveSelected() {
			auto* row = Selected();
			if (!row) {
				return;
			}
			std::string error;
			if (!Resolve(plan, row->sourceId, static_cast<uint16_t>(targetId->GetValue()), error)) {
				wxMessageBox(Text(error), Text("Substituição bloqueada"), wxOK | wxICON_WARNING, this);
				return;
			}
			FillRows();
		}
		void Navigate() {
			auto* row = Selected();
			if (!row || row->usage.positions.empty() || !g_gui.GetCurrentEditor()) {
				return;
			}
			const auto open = NativePath(wxString::FromUTF8(g_gui.GetCurrentMap().getFilename()));
			if (!FileSaveTransaction::PathsReferToSameFile(open, plan.options.sourceMap) && !FileSaveTransaction::PathsReferToSameFile(open, plan.options.outputMap)) {
				wxMessageBox(Text("Abra o mapa de origem ou a saída convertida para navegar até esta ocorrência."), Text("Navegar"), wxOK, this);
				return;
			}
			const auto& p = row->usage.positions.front();
			g_gui.SetScreenCenterPosition(::Position(p.x, p.y, p.z));
		}
		void Export() {
			wxFileDialog dialog(this, Text("Exportar relatório"), "", "conversao.json", "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			try {
				auto json = NativePath(dialog.GetPath()), csv = json;
				csv.replace_extension(".csv");
				WriteReports(plan, json, csv);
				status->SetLabel(Text("Relatórios JSON e CSV salvos."));
			} catch (const std::exception& error) {
				wxMessageBox(Text(error.what()), Text("Falha ao exportar"), wxOK | wxICON_ERROR, this);
			}
		}
		void Mappings(bool save) {
			wxFileDialog dialog(this, Text(save ? "Salvar decisões revisadas" : "Carregar decisões revisadas"), "", "mappings.json", "JSON (*.json)|*.json", save ? (wxFD_SAVE | wxFD_OVERWRITE_PROMPT) : (wxFD_OPEN | wxFD_FILE_MUST_EXIST));
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			try {
				if (save) {
					SaveReviewedMappings(plan, NativePath(dialog.GetPath()));
				} else {
					LoadReviewedMappings(plan, NativePath(dialog.GetPath()));
					FillRows();
				}
			} catch (const std::exception& error) {
				wxMessageBox(Text(error.what()), Text("Mappings de recursos"), wxOK | wxICON_WARNING, this);
			}
		}
		void CancelOrClose() {
			if (busy) {
				work->cancel = true;
				status->SetLabel(Text("Cancelando sem publicar a saída..."));
			} else {
				EndModal(result.success && canOpen() ? wxID_OK : wxID_CANCEL);
			}
		}
		wxNotebook* pages;
		wxScrolledWindow* files;
		wxFilePickerCtrl *sourceMap, *sourceOtb, *sourceDat, *sourceSpr, *sourceXml, *targetOtb, *targetXml, *targetDat, *targetSpr, *outputMap;
		wxCheckBox* currentTarget;
		wxCheckBox* approximation;
		wxChoice *roleFilter, *stateFilter, *candidates;
		wxListCtrl* list;
		wxStaticBitmap *sourcePreview, *targetPreview;
		wxSpinCtrl* targetId;
		wxStaticText *summary, *status;
		wxGauge* gauge;
		wxTextCtrl* report;
		wxButton *automatic, *analyze, *generate, *exportReport, *saveMappings, *loadMappings, *resolve, *cancel;
		wxTimer timer;
		Plan plan;
		Result result;
		uint64_t workspaceGeneration = 0;
		bool busy = false, closing = false;
		std::shared_ptr<Work> work;
		std::jthread worker;
	};
}

bool RunWholeMapMigration(wxWindow* parent) {
	MapMigrationWindowDetails::Dialog dialog(parent);
	if (dialog.ShowModal() != wxID_OK || !dialog.canOpen()) {
		return false;
	}
	return g_gui.LoadValidatedConvertedMap(FileName(MapMigrationWindowDetails::WxPath(dialog.output())));
}
