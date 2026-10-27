#include "migration_io.h"
#include "../file_transaction.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace MapMigration {
	namespace {
		using Json = nlohmann::json;
		void CheckSnapshot(const FileDigest& expected, const Control& control) {
			const auto current = FingerprintFile(expected.path, control);
			if (current.size != expected.size || current.sha256 != expected.sha256) {
				throw std::runtime_error("Arquivo mudou após a análise. Analise novamente: " + IO::Utf8(expected.path));
			}
		}
		void CopyVerified(const FileDigest& source, const Path& destination, const Control& control) {
			IO::StageTimer timer(control, "sidecarCopy");
			std::ifstream input(source.path, std::ios::binary);
			std::ofstream output(destination, std::ios::binary);
			if (!input || !output) {
				throw std::runtime_error("Não foi possível copiar o arquivo externo.");
			}
			std::array<uint8_t, 65536> buffer;
			IO::Digest digest;
			uint64_t bytes = 0;
			while (input) {
				control.check();
				input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
				const auto size = static_cast<size_t>(input.gcount());
				output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(size));
				digest.add(std::span(buffer.data(), size));
				bytes += size;
			}
			output.close();
			if (!input.eof() || !output || bytes != source.size || digest.finish() != source.sha256) {
				throw std::runtime_error("Arquivo externo copiado não corresponde à origem.");
			}
		}
		void CheckPlan(const Plan& plan, const Control& control) {
			std::map<Path, FileDigest> checked;
			const auto check = [&](const FileDigest& expected) {
				const auto key = FileSaveTransaction::NormalizePath(expected.path);
				if (const auto found = checked.find(key); found != checked.end()) {
					if (found->second.size != expected.size || found->second.sha256 != expected.sha256) {
						throw std::runtime_error("Snapshots de recursos inconsistentes.");
					}
					return;
				}
				CheckSnapshot(expected, control);
				checked.emplace(key, expected);
			};
			for (const auto& catalog : { plan.source, plan.target }) {
				for (const auto& file : catalog->files) {
					check(file);
				}
			}
			check(plan.inventory.mapFile);
			for (const auto& sidecar : plan.inventory.sidecars) {
				check(sidecar.file);
			}
		}
		Control Scale(const Control& control, int begin, int size, const std::string& phase) {
			Control result = control;
			result.progress = [&control, begin, size, phase](int p, const std::string& message) { control.update(begin + p * size / 100, phase + message); };
			return result;
		}
		Json PositionJson(const Position& position) {
			return { { "x", position.x }, { "y", position.y }, { "z", position.z } };
		}
		Json DigestJson(const FileDigest& file) {
			return { { "path", IO::Utf8(file.path) }, { "size", file.size }, { "sha256", file.sha256 } };
		}
		Json ReportJson(const Plan& plan, const Result* result, const Control& requestedControl) {
			Control control = requestedControl;
			control.metrics = plan.metrics;
			control.memoryLimitBytes = plan.memoryLimitBytes;
			Json report = { { "schema", 1 }, { "sourceMap", IO::Utf8(plan.options.sourceMap) }, { "outputMap", IO::Utf8(plan.options.outputMap) }, { "sourceResourceFingerprint", plan.source ? plan.source->fingerprint : "" }, { "targetResourceFingerprint", plan.target ? plan.target->fingerprint : "" }, { "totalTiles", plan.inventory.tiles }, { "totalItems", plan.inventory.items }, { "grounds", plan.inventory.grounds }, { "containers", plan.inventory.containers }, { "nestedItems", plan.inventory.nestedItems }, { "distinctSourceIds", plan.rows.size() }, { "unresolvedIds", plan.unresolvedIds() }, { "unresolvedOccurrences", plan.unresolvedOccurrences() }, { "tilesWithoutGroundInSource", plan.inventory.tilesWithoutGround }, { "opaqueAttributesPreserved", 0 }, { "unsupportedItemAttributes", "blocked" }, { "structureDigest", plan.inventory.structureDigest }, { "sourceMapDigest", DigestJson(plan.inventory.mapFile) }, { "warnings", plan.warnings }, { "error", plan.error }, { "completeCompatible", result && result->success && result->validated && plan.ready() }, { "sourceToTarget", Json::array() }, { "floors", plan.inventory.floors }, { "resourceFiles", Json::array() }, { "sidecars", Json::array() } };
			const auto approximate = std::count_if(plan.rows.begin(), plan.rows.end(), [](const auto& row) { return row.decision == Decision::Approximate || row.decision == Decision::VisualDraft; });
			report["mode"] = plan.visualDraft ? "visual-test" : "safe";
			report["destinationBlockingAndSpeedAccepted"] = plan.visualDraft;
			report["approximateMappingIds"] = approximate;
			report["requiresVisualTesting"] = approximate > 0;
			if (approximate || plan.visualDraft) {
				report["completeCompatible"] = false;
			}
			if (plan.source) {
				for (const auto& file : plan.source->files) {
					report["resourceFiles"].push_back({ { "catalog", "source" }, { "file", DigestJson(file) } });
				}
			}
			if (plan.target) {
				for (const auto& file : plan.target->files) {
					report["resourceFiles"].push_back({ { "catalog", "target" }, { "file", DigestJson(file) } });
				}
			}
			for (const auto& sidecar : plan.inventory.sidecars) {
				report["sidecars"].push_back({ { "attribute", sidecar.attribute }, { "reference", sidecar.reference }, { "file", DigestJson(sidecar.file) } });
			}
			report["decisions"] = Json::object();
			report["groundlessPositionSamples"] = Json::array();
			for (const auto& position : plan.inventory.groundlessPositions) {
				report["groundlessPositionSamples"].push_back(PositionJson(position));
			}
			for (const auto& row : plan.rows) {
				control.check();
				auto& count = report["decisions"][DecisionName(row.decision)];
				if (count.is_null()) {
					count = { { "ids", 0 }, { "occurrences", 0 } };
				}
				count["ids"] = count["ids"].get<uint64_t>() + 1;
				count["occurrences"] = count["occurrences"].get<uint64_t>() + row.usage.occurrences;
				Json value = { { "sourceServerId", row.sourceId }, { "sourceClientId", row.sourceClientId }, { "targetServerId", row.targetId }, { "targetClientId", row.targetClientId }, { "role", RoleName(row.roles) }, { "decision", DecisionName(row.decision) }, { "resolved", row.resolved() }, { "confidence", row.confidence }, { "reason", row.reason }, { "occurrences", row.usage.occurrences }, { "nestedOccurrences", row.usage.nestedOccurrences }, { "positions", Json::array() }, { "candidates", Json::array() } };
				value["tilesWithThisId"] = row.usage.tiles;
				value["patternDimensions"] = { row.patternX, row.patternY, row.patternZ };
				value["targetServerIdsByOriginalPattern"] = row.patternTargets;
				value["occurrencesByOriginalPattern"] = row.usage.patternOccurrences;
				for (size_t i = 0; i < row.usage.positions.size(); ++i) {
					auto position = PositionJson(row.usage.positions[i]);
					position["stackIndex"] = row.usage.stackIndices.at(i);
					if (i < row.usage.adjacentGrounds.size()) {
						position["adjacentSourceGroundsN_NE_E_SE_S_SW_W_NW"] = row.usage.adjacentGrounds[i];
					}
					value["positions"].push_back(std::move(position));
				}
				for (const auto& candidate : row.candidates) {
					value["candidates"].push_back({ { "targetServerId", candidate.serverId }, { "targetClientId", candidate.clientId }, { "score", candidate.score }, { "visualSimilarity", candidate.visualSimilarity }, { "allVariantsExact", candidate.completeExact }, { "evidence", candidate.evidence } });
				}
				report["sourceToTarget"].push_back(std::move(value));
			}
			if (result) {
				report["conversion"] = { { "success", result->success }, { "validated", result->validated }, { "changedItems", result->changedItems }, { "outputTilesWithoutGround", result->outputTilesWithoutGround }, { "error", result->error } };
				report["conversion"]["structurallyValidated"] = result->structurallyValidated;
				report["conversion"]["semanticallyCompatible"] = result->semanticallyCompatible;
				report["conversion"]["outputGroundlessPositionSamples"] = Json::array();
				for (const auto& position : result->outputGroundlessPositions) {
					report["conversion"]["outputGroundlessPositionSamples"].push_back(PositionJson(position));
				}
			}
			report["groundLossExplicitlyAccepted"] = plan.options.allowGroundLoss;
			if (plan.metrics) {
				std::lock_guard lock(plan.metrics->mutex);
				report["performance"] = {
					{ "fullMatchPasses", plan.metrics->fullMatchPasses }, { "candidateComparisons", plan.metrics->candidateComparisons }, { "hashedBytes", plan.metrics->hashedBytes }, { "retainedCatalogBytes", plan.metrics->retainedBytes }, { "peakResidentBytes", plan.metrics->peakResidentBytes }, { "baselineResidentBytes", plan.metrics->baselineResidentBytes }, { "stageMilliseconds", plan.metrics->stageMilliseconds }
				};
			}
			return report;
		}
		std::string CsvQuote(const std::string& value) {
			std::string result = "\"";
			for (char c : value) {
				result += c;
				if (c == '\"') {
					result += c;
				}
			}
			return result + '\"';
		}
		void SaveReports(const Plan& plan, const Path& jsonPath, const Path& csvPath, const Result* result, const Control& control) {
			std::ofstream json(jsonPath, std::ios::binary);
			json << std::setw(2) << ReportJson(plan, result, control) << '\n';
			json.close();
			if (!json) {
				throw std::runtime_error("Falha ao gravar relatório JSON.");
			}
			std::ofstream csv(csvPath, std::ios::binary);
			csv << "source_server_id,source_client_id,target_server_id,target_client_id,role,decision,occurrences,reason,target_ids_by_original_pattern\n";
			for (const auto& row : plan.rows) {
				control.check();
				csv << row.sourceId << ',' << row.sourceClientId << ',' << row.targetId << ',' << row.targetClientId << ',' << CsvQuote(RoleName(row.roles)) << ',' << CsvQuote(DecisionName(row.decision)) << ',' << row.usage.occurrences << ',' << CsvQuote(row.reason) << ',' << CsvQuote(Json(row.patternTargets).dump()) << '\n';
			}
			csv.close();
			if (!csv) {
				throw std::runtime_error("Falha ao gravar relatório CSV.");
			}
		}
		void EnsureFreshOutput(const Path& path, const Plan& plan) {
			if (path.empty()) {
				throw std::runtime_error("Selecione um novo caminho de saída.");
			}
			if (std::filesystem::exists(path)) {
				throw std::runtime_error("A saída já existe. Escolha um nome novo para preservar todos os arquivos: " + IO::Utf8(path));
			}
			for (const auto& catalog : { plan.source, plan.target }) {
				for (const auto& file : catalog->files) {
					if (FileSaveTransaction::PathsReferToSameFile(path, file.path)) {
						throw std::runtime_error("A saída não pode substituir recursos originais.");
					}
				}
			}
			if (FileSaveTransaction::PathsReferToSameFile(path, plan.options.sourceMap)) {
				throw std::runtime_error("O mapa original nunca pode ser sobrescrito.");
			}
		}
		void ProtectInputs(const Plan& plan, const Path& path) {
			if (path.empty()) {
				throw std::runtime_error("Caminho de relatório/mapping vazio.");
			}
			for (const auto& input : { plan.options.sourceMap, plan.options.outputMap }) {
				if (!input.empty() && FileSaveTransaction::PathsReferToSameFile(path, input)) {
					throw std::runtime_error("Relatório/mapping não pode substituir um mapa.");
				}
			}
			for (const auto& catalog : { plan.source, plan.target }) {
				if (catalog) {
					for (const auto& file : catalog->files) {
						if (FileSaveTransaction::PathsReferToSameFile(path, file.path)) {
							throw std::runtime_error("Relatório/mapping não pode substituir recursos originais.");
						}
					}
				}
			}
			for (const auto& set : { plan.options.source, plan.options.target }) {
				for (const auto& input : { set.otb, set.xml, set.dat, set.spr, set.otfi }) {
					if (!input.empty() && FileSaveTransaction::PathsReferToSameFile(path, input)) {
						throw std::runtime_error("Relatório/mapping não pode substituir recursos selecionados.");
					}
				}
			}
			for (const auto& sidecar : plan.inventory.sidecars) {
				if (FileSaveTransaction::PathsReferToSameFile(path, sidecar.file.path)) {
					throw std::runtime_error("Relatório/mapping não pode substituir arquivo externo do mapa.");
				}
			}
		}
	}
	Plan Analyze(const Options& options, const Control& control) {
		Plan plan;
		plan.options = options;
		plan.metrics = control.metrics;
		plan.memoryLimitBytes = control.memoryLimitBytes;
		try {
			plan.source = std::make_shared<Catalog>(LoadCatalog(options.source, Scale(control, 0, 30, "Origem: ")));
			plan.target = std::make_shared<Catalog>(LoadCatalog(options.target, Scale(control, 30, 40, "Destino: ")));
			plan.inventory = InspectMap(options.sourceMap, *plan.source, Scale(control, 70, 10, "Mapa: "));
			plan.rows = Match(plan.inventory, *plan.source, *plan.target, Scale(control, 80, 15, "Comparação: "));
			plan.matchedSourceFingerprint = plan.source->fingerprint;
			plan.matchedTargetFingerprint = plan.target->fingerprint;
			plan.matchedMapDigest = plan.inventory.mapFile.sha256;
			plan.warnings = plan.source->warnings;
			plan.warnings.insert(plan.warnings.end(), plan.target->warnings.begin(), plan.target->warnings.end());
			if (plan.inventory.tilesWithoutGround) {
				plan.warnings.push_back(std::to_string(plan.inventory.tilesWithoutGround) + " tiles já não possuem ground na origem; a conversão não inventará pisos.");
			}
			control.update(96, "Verificando fingerprints após a análise...");
			CheckPlan(plan, control);
			control.update(100, "Análise concluída.");
		} catch (const std::exception& error) {
			plan.error = error.what();
			plan.cancelled = control.cancelled && control.cancelled->load();
		}
		return plan;
	}
	Result Convert(const Plan& plan, const Control& requestedControl) {
		auto control = requestedControl;
		if (plan.metrics) {
			control.metrics = plan.metrics;
		}
		IO::StageTimer timer(control, "convert");
		Result result;
		result.outputMap = plan.options.outputMap;
		result.jsonReport = result.outputMap;
		result.jsonReport.replace_extension(".migration.json");
		result.csvReport = result.outputMap;
		result.csvReport.replace_extension(".migration.csv");
		try {
			if (!plan.ready()) {
				throw std::runtime_error("Conversão bloqueada: ainda existem IDs sem correspondência segura ou revisada. Exporte o relatório e revise as exceções.");
			}
			if (result.outputMap.extension() != Path(".otbm")) {
				throw std::runtime_error("A saída deve ser um novo arquivo .otbm.");
			}
			if (plan.visualDraft && !IO::Utf8(result.outputMap.stem()).ends_with("_teste")) {
				throw std::runtime_error("O mapa experimental deve ter nome terminado em _teste.otbm.");
			}
			EnsureFreshOutput(result.outputMap, plan);
			EnsureFreshOutput(result.jsonReport, plan);
			EnsureFreshOutput(result.csvReport, plan);
			control.update(0, "Verificando se os recursos e o mapa continuam iguais...");
			CheckPlan(plan, control);
			std::filesystem::create_directories(std::filesystem::absolute(result.outputMap).parent_path());
			FileSaveTransaction publication(FileSaveTransaction::PublicationMode::CreateOnly), scratch;
			const auto staged = scratch.Stage(result.outputMap);
			std::vector<const MappingRow*> mapping(65536, nullptr);
			std::map<uint16_t, uint64_t> expected;
			for (const auto& row : plan.rows) {
				const auto targets = row.patternTargets.empty() ? std::vector<uint16_t> { row.targetId } : row.patternTargets;
				for (auto id : targets) {
					if (!plan.target->items.contains(id) || !(plan.visualDraft ? CompatibleForVisualDraft(plan.source->items.at(row.sourceId), plan.target->items.at(id), row.usage.requiredRoles) : Compatible(plan.source->items.at(row.sourceId), plan.target->items.at(id), row.usage.requiredRoles))) {
						throw std::runtime_error("Mapping incompatível no preflight.");
					}
				}
				mapping[row.sourceId] = &row;
				if (row.patternTargets.empty()) {
					expected[row.targetId] += row.usage.occurrences;
				} else {
					if (row.patternTargets.size() != row.usage.patternOccurrences.size()) {
						throw std::runtime_error("Inventário de padrões incompleto.");
					}
					uint64_t count = 0;
					for (size_t i = 0; i < row.patternTargets.size(); ++i) {
						if (row.usage.patternOccurrences[i]) {
							expected[row.patternTargets[i]] += row.usage.patternOccurrences[i];
						}
						count += row.usage.patternOccurrences[i];
					}
					if (count != row.usage.occurrences) {
						throw std::runtime_error("Inventário contextual não cobre todas as ocorrências.");
					}
				}
			}
			std::map<uint8_t, std::string> sidecarNames;
			for (const auto& sidecar : plan.inventory.sidecars) {
				control.update(3, "Copiando arquivos externos...");
				const std::string suffix = sidecar.attribute == 11 ? "-spawn.xml" : sidecar.attribute == 13 ? "-house.xml"
					: sidecar.attribute == 23																? "-npc-spawn.xml"
																											: "-zones.xml";
				const auto name = IO::Utf8(result.outputMap.stem()) + suffix;
				if (!sidecarNames.emplace(sidecar.attribute, name).second) {
					throw std::runtime_error("Referência externa duplicada no mapa de origem.");
				}
				const auto destination = result.outputMap.parent_path() / IO::FromUtf8(name);
				EnsureFreshOutput(destination, plan);
				const auto temporary = publication.Stage(destination);
				CopyVerified(sidecar.file, temporary, control);
				sidecarNames[sidecar.attribute] = name;
			}
			const auto replaceId = [&](std::vector<uint8_t>& data, size_t offset, Position position) {
				IO::Cursor fields { data };
				fields.skip(offset);
				const auto original = fields.u16();
				if (!mapping[original]) {
					throw std::runtime_error("ID original não está no plano revisado: " + std::to_string(original));
				}
				const auto destination = TargetIdAt(*mapping[original], position);
				if (!destination) {
					throw std::runtime_error("Padrão contextual sem destino.");
				}
				if (destination != original) {
					++result.changedItems;
				}
				IO::Put16(data, offset, destination);
			};
			struct WriteContext {
				Position area, position;
			};
			std::array<WriteContext, 257> contexts {};
			control.update(5, "Gravando mapa...");
			IO::Walk(
				plan.options.sourceMap, "OTBM", [&](std::vector<uint8_t>& data, size_t depth, const auto&) {
					IO::Cursor fields { data };
					const auto type = fields.u8();
					auto context = depth ? contexts[depth - 1] : WriteContext {};
					if (type == 4) {
						context.area = { fields.u16(), fields.u16(), fields.u8() };
					} else if (type == 5 || type == 14) {
						const auto x = fields.u8(), y = fields.u8();
						context.position = { static_cast<uint16_t>(context.area.x + x), static_cast<uint16_t>(context.area.y + y), context.area.z };
						if (type == 14) {
							fields.skip(4);
						}
					}
					contexts[depth] = context;
					if (depth == 0) {
						IO::Put32(data, 9, plan.target->majorVersion);
						IO::Put32(data, 13, plan.target->minorVersion);
					} else if (type == 6) {
						replaceId(data, 1, context.position);
					} else if (type == 5 || type == 14) {
						while (fields.remaining()) {
							const auto attr = fields.u8();
							if (attr == 9) {
								const auto idOffset = fields.offset;
								const auto original = fields.u16();
								replaceId(data, idOffset, context.position);
								const auto& item = plan.source->items.at(original);
								if (plan.inventory.version == 0 && ((item.flags & (1u << 7)) || (item.roles & (Fluid | Splash)))) {
									fields.skip(1);
								}
							} else if (attr == 3) {
								const auto flags = fields.u32();
								if (flags & 0x40) {
									uint16_t zone;
									do {
										zone = fields.u16();
									} while (zone);
								}
							} else {
								throw std::runtime_error("Atributo de tile não suportado no preflight.");
							}
						}
					} else if (type == 2) {
						std::vector<uint8_t> replaced { type };
						while (fields.remaining()) {
							const auto attr = fields.u8();
							auto text = fields.text();
							if (auto found = sidecarNames.find(attr); found != sidecarNames.end()) {
								text = found->second;
							}
							replaced.push_back(attr);
							IO::AppendText(replaced, text);
						}
						data = std::move(replaced);
					}
				},
				Scale(control, 5, 65, "Gravando: "), staged
			);
			// Normalize sidecar references in the digest, but don't open their
			// final names before publication. Each staged sidecar is verified above.
			const auto actual = InspectMap(staged, *plan.target, Scale(control, 70, 15, "Validando: "), false, false);
			result.outputTilesWithoutGround = actual.tilesWithoutGround;
			result.outputGroundlessPositions = actual.groundlessPositions;
			const auto& original = plan.inventory;
			if (actual.structureDigest != original.structureDigest || actual.items != plan.inventory.items || actual.tiles != plan.inventory.tiles || actual.width != plan.inventory.width || actual.height != plan.inventory.height || actual.floors != plan.inventory.floors || actual.usedIds.size() != expected.size()) {
				throw std::runtime_error("Validação detectou alteração em geometria, pilhas ou atributos; nenhuma saída foi publicada.");
			}
			for (const auto& [id, count] : expected) {
				if (!actual.usedIds.contains(id) || actual.usedIds.at(id).occurrences != count) {
					throw std::runtime_error("Inventário final não corresponde ao plano de IDs.");
				}
			}
			if (actual.itemMajorVersion != plan.target->majorVersion || actual.itemMinorVersion != plan.target->minorVersion) {
				throw std::runtime_error("Cabeçalho final não corresponde ao OTB de destino.");
			}
			result.structurallyValidated = true;
			const bool groundPreserved = actual.tilesWithoutGround <= plan.inventory.tilesWithoutGround && actual.grounds == plan.inventory.grounds;
			if (!groundPreserved && !(plan.visualDraft && plan.options.allowGroundLoss)) {
				throw std::runtime_error("Conversão bloqueada: perdeu ground. A perda de chão exige autorização explícita para um mapa experimental.");
			}
			result.semanticallyCompatible = groundPreserved && !plan.visualDraft
				&& std::none_of(plan.rows.begin(), plan.rows.end(), [](const auto& row) { return row.decision == Decision::Approximate; });
			for (const auto& row : plan.rows) {
				const auto targets = row.patternTargets.empty() ? std::vector<uint16_t> { row.targetId } : row.patternTargets;
				for (const auto id : targets) {
					result.semanticallyCompatible &= FunctionallyEquivalent(plan.source->items.at(row.sourceId), plan.target->items.at(id), row.usage.requiredRoles);
				}
			}
			result.validated = result.structurallyValidated && result.semanticallyCompatible;
			control.update(90, "Revalidando arquivos antes da publicação...");
			CheckPlan(plan, control);
			result.success = true;
			SaveReports(plan, publication.Stage(result.jsonReport), publication.Stage(result.csvReport), &result, control);
			// Publish the map last, so every rebased sidecar already exists when
			// readers first observe the final map name. Rollback covers all files.
			std::filesystem::rename(staged, publication.Stage(result.outputMap));
			control.update(98, "Publicando arquivos novos sem sobrescrever...");
			std::string error;
			if (!publication.Commit(error)) {
				throw std::runtime_error(error);
			}
			try {
				if (control.progress) {
					control.progress(100, result.validated ? "Mapa inteiro convertido e validado." : "Mapa experimental gerado; compatibilidade funcional não comprovada.");
				}
			} catch (...) { /* Publication succeeded; a UI callback cannot undo that fact. */
			}
		} catch (const std::exception& error) {
			result.success = false;
			result.validated = false;
			result.semanticallyCompatible = false;
			result.error = error.what();
			result.cancelled = control.cancelled && control.cancelled->load();
		}
		return result;
	}
	void WriteReports(const Plan& plan, const Path& jsonPath, const Path& csvPath, const Result* result, const Control& control) {
		ProtectInputs(plan, jsonPath);
		ProtectInputs(plan, csvPath);
		FileSaveTransaction transaction;
		SaveReports(plan, transaction.Stage(jsonPath), transaction.Stage(csvPath), result, control);
		std::string error;
		if (!transaction.Commit(error)) {
			throw std::runtime_error(error);
		}
	}
	void SaveReviewedMappings(const Plan& plan, const Path& path) {
		ProtectInputs(plan, path);
		if (!plan.source || !plan.target) {
			throw std::runtime_error("Catálogos não carregados.");
		}
		Json data = { { "schema", 1 }, { "sourceResourceFingerprint", plan.source->fingerprint }, { "targetResourceFingerprint", plan.target->fingerprint }, { "mappings", Json::array() } };
		for (const auto& row : plan.rows) {
			if (row.decision == Decision::Reviewed) {
				data["mappings"].push_back({ { "source", row.sourceId }, { "target", row.targetId } });
			}
		}
		FileSaveTransaction transaction;
		const auto staged = transaction.Stage(path);
		std::ofstream file(staged, std::ios::binary);
		file << data.dump(2);
		file.close();
		if (!file) {
			throw std::runtime_error("Falha ao salvar decisões revisadas.");
		}
		std::string error;
		if (!transaction.Commit(error)) {
			throw std::runtime_error(error);
		}
	}
	void LoadReviewedMappings(Plan& plan, const Path& path) {
		std::ifstream file(path);
		const auto data = Json::parse(file);
		if (!plan.source || !plan.target || data.value("schema", 0) != 1 || data.value("sourceResourceFingerprint", "") != plan.source->fingerprint || data.value("targetResourceFingerprint", "") != plan.target->fingerprint) {
			throw std::runtime_error("Este mapping pertence a outros recursos. Nenhuma decisão foi aplicada.");
		}
		Plan reviewed = plan;
		for (const auto& row : data.at("mappings")) {
			if (!row.at("source").is_number_integer() || !row.at("target").is_number_integer()) {
				throw std::runtime_error("Mapping salvo contém IDs não inteiros.");
			}
			const auto source = row.at("source").get<uint64_t>(), target = row.at("target").get<uint64_t>();
			std::string error;
			if (source > 65535 || target > 65535) {
				throw std::runtime_error("Mapping salvo contém IDs fora de 16 bits.");
			}
			if (!plan.inventory.usedIds.contains(static_cast<uint16_t>(source))) {
				continue;
			}
			if (!Resolve(reviewed, static_cast<uint16_t>(source), static_cast<uint16_t>(target), error)) {
				throw std::runtime_error("Mapping salvo incompatível: " + error);
			}
		}
		plan = std::move(reviewed);
	}
}
