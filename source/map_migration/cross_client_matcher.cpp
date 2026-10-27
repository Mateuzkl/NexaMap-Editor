#include "migration_io.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_map>
#include "migration_io.h"

namespace MapMigration {
	namespace {
		constexpr uint32_t ImmutableFlags = 0x0007FFFFu | (1u << 22); // Including all floor changes, hooks, stacking and top order.
		constexpr uint64_t PlacementFlags = ~(1ull << 31); // Look interaction does not change pixels, draw order or item kind.
		bool SameSpatial(const Layout& a, const Layout& b) {
			return a.width == b.width && a.height == b.height && a.offsetX == b.offsetX && a.offsetY == b.offsetY && a.elevation == b.elevation && a.groundSpeed == b.groundSpeed && (a.datFlags & PlacementFlags) == (b.datFlags & PlacementFlags);
		}
		bool SamePlacement(const Layout& a, const Layout& b) {
			return a.width == b.width && a.height == b.height && a.patternX == b.patternX && a.patternY == b.patternY && a.patternZ == b.patternZ && a.frames == b.frames
				&& SameSpatial(a, b) && a.durations == b.durations;
		}
		bool SameProperties(const CatalogItem& a, const CatalogItem& b) {
			return a.group == b.group && (a.flags & ImmutableFlags) == (b.flags & ImmutableFlags) && a.topOrder == b.topOrder;
		}
		double Similarity(const Visual& a, const Visual& b) {
			if (a.preview.size() != b.preview.size() || a.preview.empty()) {
				return 0;
			}
			double colour = 0, alpha = 0, weight = 0;
			for (size_t i = 0; i < a.preview.size(); i += 4) {
				const double overlap = std::min(a.preview[i + 3], b.preview[i + 3]) / 255.0;
				const double unionAlpha = std::max(a.preview[i + 3], b.preview[i + 3]) / 255.0;
				alpha += overlap;
				weight += unionAlpha;
				for (size_t c = 0; c < 3; ++c) {
					colour += (1.0 - std::abs(int(a.preview[i + c]) - int(b.preview[i + c])) / 255.0) * overlap;
				}
			}
			if (weight == 0) {
				return 0;
			}
			return 0.65 * (alpha ? colour / (3 * alpha) : 0) + 0.35 * alpha / weight;
		}
		double AllVariantSimilarity(const Visual& a, const Visual& b) {
			if (a.variantFeatures.empty() || a.variantFeatures.size() != b.variantFeatures.size()) {
				return 0;
			}
			double worst = 1;
			for (size_t v = 0; v < a.variantFeatures.size(); ++v) {
				double overlap = 0, united = 0, colour = 0;
				for (size_t p = 0; p < 64; ++p) {
					const auto* x = a.variantFeatures[v].data() + p * 4;
					const auto* y = b.variantFeatures[v].data() + p * 4;
					const double alpha = std::min(x[3], y[3]) / 255.;
					overlap += alpha;
					united += std::max(x[3], y[3]) / 255.;
					for (size_t c = 0; c < 3; ++c) {
						colour += (1. - std::abs(int(x[c]) - int(y[c])) / 255.) * alpha;
					}
				}
				worst = std::min(worst, united ? (.65 * (overlap ? colour / (3 * overlap) : 0) + .35 * overlap / united) : 1.);
			}
			return worst;
		}
		void Assign(MappingRow& row, const Candidate& candidate, Decision decision, const std::string& reason) {
			row.targetId = candidate.serverId;
			row.targetClientId = candidate.clientId;
			row.confidence = candidate.score;
			row.decision = decision;
			row.reason = reason;
		}
		bool SameVisualLayout(const Layout& a, const Layout& b) {
			return a.width == b.width && a.height == b.height && a.patternX == b.patternX && a.patternY == b.patternY && a.patternZ == b.patternZ && a.frames == b.frames && a.offsetX == b.offsetX && a.offsetY == b.offsetY && a.elevation == b.elevation && a.durations == b.durations;
		}
		double DraftMetadataPreference(const CatalogItem& a, const CatalogItem& b) {
			double result = ((a.roles & Ground) == (b.roles & Ground)) ? 32 : 0;
			if (a.group == b.group) {
				result += 16;
			}
			result -= std::popcount((a.flags ^ b.flags) & ImmutableFlags);
			if (a.topOrder == b.topOrder) {
				result += 2;
			}
			return result;
		}
		void ResolveVisualDraftRows(Plan& plan, const Control& control) {
			std::unordered_map<std::string, std::vector<const CatalogItem*>> completeIndex, singleIndex;
			std::unordered_map<uint64_t, std::vector<const CatalogItem*>> roleIndex;
			for (const auto& [id, item] : plan.target->items) {
				if (item.visual && item.visual->valid) {
					roleIndex[item.roles & (FunctionalRoles & ~uint64_t(Ground))].push_back(&item);
					completeIndex[item.visual->completePixels].push_back(&item);
					if (item.visual->variantPixels.size() == 1) {
						singleIndex[item.visual->variantPixels.front()].push_back(&item);
					}
				}
			}
			size_t processed = 0;
			for (auto& row : plan.rows) {
				control.update(static_cast<int>(100 * ++processed / plan.rows.size()), "Escolhendo aparência para o mapa de teste...");
				if (row.resolved()) {
					continue;
				}
				const auto source = plan.source->items.find(row.sourceId);
				if (source == plan.source->items.end() || !source->second.visual || !source->second.visual->valid) {
					continue;
				}
				const auto& a = source->second;
				const auto& av = *a.visual;
				const auto acceptable = [&](const CatalogItem& b) {
					const bool visibilityCompatible = b.visual && (b.visual->visiblePixels == av.visiblePixels || (!av.visiblePixels && (row.roles & Container)));
					return b.visual && b.visual->valid && visibilityCompatible && CompatibleForVisualDraft(a, b, row.usage.requiredRoles);
				};
				const auto choose = [&](const std::vector<const CatalogItem*>& candidates, const std::function<bool(const CatalogItem&)>& extra) -> const CatalogItem* {
					const CatalogItem* best = nullptr;
					double preference = -1e9;
					for (const auto* item : candidates) {
						if (acceptable(*item) && extra(*item)) {
							const auto score = DraftMetadataPreference(a, *item);
							if (!best || score > preference || (score == preference && item->serverId < best->serverId)) {
								best = item;
								preference = score;
							}
						}
					}
					return best;
				};
				// Exact pixels win over a perceptual suggestion, even when the
				// destination uses different blocking/speed metadata as authorized.
				if (auto found = completeIndex.find(av.completePixels); found != completeIndex.end()) {
					const auto* best = choose(found->second, [&](const auto& b) { return SameVisualLayout(av.layout, b.visual->layout); });
					if (best) {
						Candidate candidate { best->serverId, best->clientId, 1, 1, true, { "todos os pixels e variações iguais", "propriedades do destino aceitas no modo visual de teste" } };
						row.candidates = { candidate };
						Assign(row, candidate, Decision::VisualDraft, "Gráficos exatos; usando as propriedades do seu destino. Bloqueio, velocidade ou classificação podem mudar. Mapa apenas para teste.");
						continue;
					}
				}
				// Expand one patterned ground to the destination's separate IDs.
				if ((row.roles & Ground) && !row.usage.nestedOccurrences && av.layout.frames == 1 && av.layout.patternZ == 1 && av.variantPixels.size() > 1 && av.variantPixels.size() <= 256 && row.usage.patternOccurrences.size() == av.variantPixels.size()) {
					std::vector<uint16_t> targets;
					for (const auto& hash : av.variantPixels) {
						const auto found = singleIndex.find(hash);
						const CatalogItem* best = nullptr;
						if (found != singleIndex.end()) {
							best = choose(found->second, [&](const auto& b) {const auto& l=b.visual->layout;return l.width==av.layout.width&&l.height==av.layout.height&&l.offsetX==av.layout.offsetX&&l.offsetY==av.layout.offsetY&&l.elevation==av.layout.elevation; });
						}
						if (!best) {
							break;
						}
						targets.push_back(best->serverId);
					}
					if (targets.size() == av.variantPixels.size()) {
						const auto& b = plan.target->items.at(targets.front());
						Candidate candidate { b.serverId, b.clientId, 1, 1, true, { "cada padrão tem pixels exatos", "ID escolhido por posição", "bloqueio e velocidade do destino aceitos" } };
						row.candidates = { candidate };
						Assign(row, candidate, Decision::VisualDraft, "Padrões gráficos exatos convertidos automaticamente por posição; propriedades do destino podem alterar bloqueios e velocidade.");
						if (!std::all_of(targets.begin(), targets.end(), [&](auto id) { return id == targets.front(); })) {
							row.patternX = av.layout.patternX;
							row.patternY = av.layout.patternY;
							row.patternZ = av.layout.patternZ;
							row.patternTargets = std::move(targets);
						}
						continue;
					}
				}
				// Bounded coarse ranking, followed by full preview comparison. The
				// result remains explicitly approximate, even if no rival is close.
				std::vector<std::pair<double, const CatalogItem*>> shortlist;
				for (const auto* target : roleIndex[(a.roles | row.usage.requiredRoles) & (FunctionalRoles & ~uint64_t(Ground))]) {
					const auto& b = *target;
					control.check();
					if (!acceptable(b)) {
						continue;
					}
					const auto& bv = *b.visual;
					double histogram = 0;
					for (size_t bin = 0; bin < av.histogram.size(); ++bin) {
						histogram += std::min(av.histogram[bin], bv.histogram[bin]);
					}
					const double alpha = 1. - std::popcount(av.shape ^ bv.shape) / 64., detail = 1. - std::popcount(av.luminanceHash ^ bv.luminanceHash) / 64.;
					const double score = .30 * histogram + .35 * alpha + .35 * detail + (av.layout.width == bv.layout.width && av.layout.height == bv.layout.height ? .04 : 0) + (((a.roles & Ground) == (b.roles & Ground)) ? .02 : 0);
					if (shortlist.size() == 64 && score < shortlist.back().first) {
						continue;
					}
					auto pos = std::lower_bound(shortlist.begin(), shortlist.end(), score, [](const auto& item, double value) { return item.first > value; });
					shortlist.insert(pos, { score, &b });
					if (shortlist.size() > 64) {
						shortlist.pop_back();
					}
				}
				std::vector<Candidate> candidates;
				for (const auto& [coarse, b] : shortlist) {
					const auto& bv = *b->visual;
					const double pixels = av.visiblePixels ? Similarity(av, bv) : bv.visiblePixels ? 0.
																								   : 1.;
					const double detail = 1. - std::popcount(av.luminanceHash ^ bv.luminanceHash) / 64.;
					const double variants = av.variantFeatures.size() == bv.variantFeatures.size() ? AllVariantSimilarity(av, bv) : pixels * .8;
					const double visual = .55 * pixels + .25 * detail + .20 * variants;
					candidates.push_back({ b->serverId, b->clientId, .8 * visual + .2 * std::min(1., coarse), visual, false, { "aproximação visual para teste", "pixels, silhueta e textura comparados", "não é equivalência comprovada" } });
				}
				std::sort(candidates.begin(), candidates.end(), [&](const auto& x, const auto& y) {if(x.score!=y.score){return x.score>y.score;
}const auto px=DraftMetadataPreference(a,plan.target->items.at(x.serverId)),py=DraftMetadataPreference(a,plan.target->items.at(y.serverId));return px!=py?px>py:x.serverId<y.serverId; });
				if (candidates.empty()) {
					continue;
				}
				if (candidates.size() > 12) {
					candidates.resize(12);
				}
				row.candidates = std::move(candidates);
				const auto best = row.candidates.front();
				Assign(row, best, Decision::VisualDraft, "Melhor aproximação visual disponível no destino, selecionada para o mapa de teste. Pode diferir bastante do original; alternativas e pontuações constam no relatório.");
				if (!av.visiblePixels && plan.target->items.at(best.serverId).visual->visiblePixels) {
					row.reason = "Container transparente na origem sem equivalente transparente no destino. Container visível escolhido para preservar a classe, os atributos e eventuais conteúdos; diferença visual explícita no mapa de teste.";
				}
			}
		}
	}
	bool MappingRow::resolved() const noexcept {
		if (decision == Decision::ContextualExact) {
			return targetId != 0 && !patternTargets.empty() && patternTargets.size() == size_t(patternX) * patternY * patternZ && std::all_of(patternTargets.begin(), patternTargets.end(), [](auto id) { return id != 0; });
		}
		return targetId != 0 && (decision == Decision::Identity || decision == Decision::Exact || decision == Decision::HighConfidence || decision == Decision::Reviewed || decision == Decision::Approximate || decision == Decision::VisualDraft);
	}
	uint16_t TargetIdAt(const MappingRow& row, Position position) {
		if (row.patternTargets.empty()) {
			return row.targetId;
		}
		if (!row.patternX || !row.patternY || !row.patternZ) {
			return 0;
		}
		const size_t index = (size_t(position.z % row.patternZ) * row.patternY + position.y % row.patternY) * row.patternX + position.x % row.patternX;
		return index < row.patternTargets.size() ? row.patternTargets[index] : 0;
	}
	uint64_t Plan::unresolvedIds() const noexcept {
		return std::count_if(rows.begin(), rows.end(), [](const auto& row) { return !row.resolved(); });
	}
	uint64_t Plan::unresolvedOccurrences() const noexcept {
		uint64_t total = 0;
		for (const auto& row : rows) {
			if (!row.resolved()) {
				total += row.usage.occurrences;
			}
		}
		return total;
	}
	bool Plan::ready() const noexcept {
		return error.empty() && !cancelled && source && target && unresolvedIds() == 0;
	}
	bool Compatible(const CatalogItem& source, const CatalogItem& target, uint64_t required) {
		const uint64_t sourceRoles = (source.roles | required) & FunctionalRoles, targetRoles = target.roles & FunctionalRoles;
		if (sourceRoles != targetRoles) {
			return false;
		}
		// XML/brush hints are strict only when both catalogs actually know them.
		const auto known = source.knownRoles & target.knownRoles & (Wall | Border | Window);
		if ((source.roles & known) != (target.roles & known)) {
			return false;
		}
		constexpr uint32_t directions = 0x1F00u | (7u << 16) | (1u << 7) | (1u << 22);
		if ((source.flags & directions) != (target.flags & directions)) {
			return false;
		}
		if (source.visual && target.visual && source.visual->valid && target.visual->valid) {
			constexpr uint64_t kinds = (1ull << 4) | (1ull << 5) | (1ull << 10) | (1ull << 11) | (7ull << 17);
			if ((source.visual->layout.datFlags & kinds) != (target.visual->layout.datFlags & kinds)) {
				return false;
			}
		}
		return true;
	}
	bool CompatibleForVisualDraft(const CatalogItem& source, const CatalogItem& target, uint64_t required) {
		const auto sourceRoles = (source.roles | required) & FunctionalRoles, targetRoles = target.roles & FunctionalRoles;
		if ((sourceRoles & ~uint64_t(Ground)) != (targetRoles & ~uint64_t(Ground))) {
			return false;
		}
		// An ordinary source object must not become a second ground and displace
		// the tile's existing ground on reopen. The reverse is an explicit trial
		// tradeoff, with the original item/attributes retained in the new file.
		if (!(sourceRoles & Ground) && (targetRoles & Ground)) {
			return false;
		}
		constexpr uint32_t structural = 0x1F00u | (7u << 16) | (1u << 7) | (1u << 22);
		if ((source.flags & structural) != (target.flags & structural)) {
			return false;
		}
		if (source.visual && target.visual) {
			constexpr uint64_t persisted = (1ull << 4) | (1ull << 5) | (1ull << 10) | (1ull << 11);
			if ((source.visual->layout.datFlags & persisted) != (target.visual->layout.datFlags & persisted)) {
				return false;
			}
		}
		return true;
	}
	bool FunctionallyEquivalent(const CatalogItem& source, const CatalogItem& target, uint64_t required) {
		return Compatible(source, target, required) && SameProperties(source, target)
			&& source.visual && target.visual && SameSpatial(source.visual->layout, target.visual->layout)
			&& source.xmlProperties == target.xmlProperties;
	}
	std::vector<MappingRow> Match(const Inventory& inventory, const Catalog& source, const Catalog& target, const Control& control) {
		IO::StageTimer timer(control, "match");
		if (control.metrics) {
			std::lock_guard lock(control.metrics->mutex);
			++control.metrics->fullMatchPasses;
		}
		std::unordered_map<std::string, std::vector<const CatalogItem*>> exactIndex;
		std::unordered_map<uint64_t, std::vector<const CatalogItem*>> roleIndex;
		for (const auto& [id, item] : target.items) {
			roleIndex[item.roles & FunctionalRoles].push_back(&item);
			if (item.visual && item.visual->valid) {
				exactIndex[item.visual->completePixels].push_back(&item);
			}
		}
		std::vector<MappingRow> rows;
		size_t complete = 0;
		for (const auto& [id, usage] : inventory.usedIds) {
			control.update(static_cast<int>(++complete * 100 / inventory.usedIds.size()), "Comparando IDs usados pelo mapa inteiro...");
			MappingRow row;
			row.sourceId = id;
			row.usage = usage;
			const auto found = source.items.find(id);
			if (found == source.items.end()) {
				row.decision = Decision::Unverifiable;
				row.reason = "ServerID não existe no OTB de origem; não é possível inferir seus pixels pelo OTBM.";
				rows.push_back(std::move(row));
				continue;
			}
			const auto& item = found->second;
			row.sourceClientId = item.clientId;
			row.roles = item.roles | usage.requiredRoles;
			if (!item.visual || !item.visual->valid) {
				row.decision = Decision::Unverifiable;
				row.reason = item.visual ? item.visual->error : "Gráfico de origem não carregado.";
				rows.push_back(std::move(row));
				continue;
			}
			if ((row.roles & Ground) && !item.visual->visiblePixels) {
				row.decision = Decision::Unverifiable;
				row.reason = "Ground de origem totalmente transparente: não confundir ausência de pixels com piso escuro.";
				rows.push_back(std::move(row));
				continue;
			}
			std::vector<Candidate> exact;
			if (auto candidates = exactIndex.find(item.visual->completePixels); candidates != exactIndex.end()) {
				for (const auto* candidate : candidates->second) {
					if (!Compatible(item, *candidate, usage.requiredRoles) || !SamePlacement(item.visual->layout, candidate->visual->layout) || !SameProperties(item, *candidate)) {
						continue;
					}
					Candidate match { candidate->serverId, candidate->clientId, 1, 1, true, { "pixels de todas as variações idênticos", "tipo funcional e propriedades compatíveis", "orientação, animação e deslocamento preservados" } };
					exact.push_back(std::move(match));
				}
			}
			// Entry identity requires the same catalog fingerprint. Equal IDs in
			// different catalogs cannot settle an otherwise ambiguous match.
			auto identity = std::find_if(exact.begin(), exact.end(), [&](const Candidate& candidate) {
				const auto& destination = target.items.at(candidate.serverId);
				return source.fingerprint == target.fingerprint && candidate.serverId == id && destination.otbRecordDigest == item.otbRecordDigest
					&& (source.resources.xml.empty() || item.xmlProperties == destination.xmlProperties);
			});
			if (identity != exact.end()) {
				row.candidates = exact;
				Assign(row, *identity, Decision::Identity, "Registro OTB e todas as variações gráficas comprovam a identidade deste item.");
			} else if (exact.size() == 1 && item.visual->visiblePixels) {
				row.candidates = exact;
				Assign(row, exact.front(), Decision::Exact, "Único item de destino com todas as variações gráficas e propriedades compatíveis.");
			} else if (exact.size() > 1) {
				row.candidates.assign(exact.begin(), exact.begin() + std::min<size_t>(exact.size(), 12));
				row.decision = Decision::Ambiguous;
				row.reason = "Vários ServerIDs compatíveis têm os mesmos gráficos; escolha o ID correto uma vez para o mapa inteiro.";
			} else {
				bool compatibleTarget = false;
				for (const auto* destination : roleIndex[(item.roles | usage.requiredRoles) & FunctionalRoles]) {
					const auto& candidate = *destination;
					const auto candidateId = candidate.serverId;
					control.check();
					if (!Compatible(item, candidate, usage.requiredRoles)) {
						continue;
					}
					compatibleTarget = true;
					if (!candidate.visual || !candidate.visual->valid || !candidate.visual->visiblePixels) {
						continue;
					}
					const auto& a = *item.visual;
					const auto& b = *candidate.visual;
					if (a.layout.width != b.layout.width || a.layout.height != b.layout.height) {
						continue;
					}
					const auto mask = static_cast<unsigned>(std::popcount(a.shape ^ b.shape));
					if (mask > 24) {
						continue;
					}
					double histogram = 0;
					for (size_t bin = 0; bin < a.histogram.size(); ++bin) {
						histogram += std::min(a.histogram[bin], b.histogram[bin]);
					}
					if (histogram < 0.40) {
						continue;
					}
					if (control.metrics) {
						std::lock_guard lock(control.metrics->mutex);
						++control.metrics->candidateComparisons;
					}
					const double preview = Similarity(a, b);
					const bool samePlacement = SamePlacement(a.layout, b.layout);
					const double allVariants = samePlacement ? AllVariantSimilarity(a, b) : 0;
					const double visual = allVariants > 0 ? std::min(preview, allVariants) : preview * .90;
					if (visual < 0.65) {
						continue;
					}
					const double property = SameProperties(item, candidate) ? 1.0 : 0.70;
					const double placement = SamePlacement(a.layout, b.layout) ? 1.0 : 0.5;
					const double shape = 1. - std::popcount(a.luminanceHash ^ b.luminanceHash) / 64.;
					Candidate match { candidateId, candidate.clientId, visual * 0.76 + property * 0.12 + placement * 0.08 + shape * .04, visual, false, { allVariants > 0 ? "descritores de todas as variações comparados na mesma orientação" : "quantidade/layout de variações diferente; apenas sugestão", "tipo funcional compatível", "comparação de forma, transparência, cor e textura" } };
					if (row.roles & (Border | Wall | Window)) {
						match.evidence.push_back("bordas/orientações preservadas nos descritores; contexto de vizinhos disponível no relatório; revisão obrigatória");
					}
					row.candidates.push_back(std::move(match));
				}
				std::sort(row.candidates.begin(), row.candidates.end(), [](const auto& a, const auto& b) { return a.score == b.score ? a.serverId < b.serverId : a.score > b.score; });
				if (row.candidates.size() > 12) {
					row.candidates.resize(12);
				}
				if (row.candidates.empty()) {
					row.decision = compatibleTarget ? Decision::Missing : Decision::Incompatible;
					row.reason = compatibleTarget ? "Nenhum equivalente gráfico e funcional confiável foi encontrado no destino." : "Nenhum item do destino preserva os tipos funcionais e direções exigidos.";
				} else if (row.candidates.size() > 1 && row.candidates[0].score - row.candidates[1].score < 0.04) {
					row.decision = Decision::Ambiguous;
					row.reason = "As melhores sugestões são próximas demais para decisão automática.";
				} else {
					const auto& best = row.candidates.front();
					const auto& destination = target.items.at(best.serverId);
					const double margin = row.candidates.size() > 1 ? best.score - row.candidates[1].score : 1.;
					// Structural tiles use the strictest threshold. Functional objects,
					// walls and borders remain review-only unless all pixels are exact.
					const auto special = row.roles & (FunctionalRoles & ~uint64_t(Ground));
					const double threshold = (row.roles & Ground) ? .998 : .995;
					const auto textureBits = std::popcount(item.visual->luminanceHash);
					if (!special && !(row.roles & (Border | Wall | Window)) && textureBits > 4 && textureBits < 60 && best.visualSimilarity >= threshold && margin >= .06 && SamePlacement(item.visual->layout, destination.visual->layout) && SameProperties(item, destination)) {
						Assign(row, best, Decision::HighConfidence, "Variações, forma e propriedades quase idênticas com candidato isolado; pequena diferença visual registrada, sem promessa de pixels idênticos.");
					} else {
						row.decision = Decision::Suggested;
						row.confidence = best.score;
						row.reason = "Sugestão visual disponível. Revise antes de substituir este ID em todas as ocorrências.";
					}
				}
			}
			rows.push_back(std::move(row));
		}
		// Some clients store terrain variations in one DAT item, while another
		// stores the same pixels under separate server IDs. Resolve each original
		// X/Y/Z pattern independently, using the renderer's coordinate convention.
		std::unordered_map<std::string, std::vector<const CatalogItem*>> singlePatternIndex;
		for (const auto& [id, item] : target.items) {
			if (item.visual && item.visual->valid && item.visual->visiblePixels && (item.roles & FunctionalRoles) == Ground && item.visual->layout.frames == 1 && item.visual->variantPixels.size() == 1) {
				singlePatternIndex[item.visual->variantPixels.front()].push_back(&item);
			}
		}
		for (auto& row : rows) {
			control.check();
			if (row.resolved() || row.usage.nestedOccurrences || (row.roles & FunctionalRoles) != Ground) {
				continue;
			}
			const auto it = source.items.find(row.sourceId);
			if (it == source.items.end() || !it->second.visual || !it->second.visual->valid) {
				continue;
			}
			const auto& item = it->second;
			const auto& visual = *item.visual;
			const auto& layout = visual.layout;
			if (layout.frames != 1 || visual.variantPixels.size() < 2 || visual.variantPixels.size() > 256 || row.usage.patternOccurrences.size() != visual.variantPixels.size()) {
				continue;
			}
			std::vector<uint16_t> targets;
			bool all = true;
			for (const auto& hash : visual.variantPixels) {
				const auto found = singlePatternIndex.find(hash);
				uint16_t selected = 0;
				size_t count = 0;
				if (found != singlePatternIndex.end()) {
					for (const auto* targetItem : found->second) {
						if (Compatible(item, *targetItem, row.usage.requiredRoles) && SameProperties(item, *targetItem) && SameSpatial(layout, targetItem->visual->layout)) {
							selected = targetItem->serverId;
							++count;
						}
					}
				}
				if (count != 1) {
					all = false;
					break;
				}
				targets.push_back(selected);
			}
			if (!all) {
				continue;
			}
			row.targetId = targets.front();
			row.targetClientId = target.items.at(row.targetId).clientId;
			row.confidence = 1;
			row.candidates.clear();
			for (auto id : targets) {
				if (std::none_of(row.candidates.begin(), row.candidates.end(), [id](const auto& c) { return c.serverId == id; })) {
					row.candidates.push_back({ id, target.items.at(id).clientId, 1, 1, true, { "pixels exatos do padrão original", "ground, bloqueios, velocidade e posicionamento compatíveis" } });
				}
			}
			if (std::all_of(targets.begin(), targets.end(), [&](auto id) { return id == targets.front(); })) {
				row.decision = Decision::Exact;
				row.reason = "Todos os padrões da origem repetem os pixels de um único item de destino; equivalência gráfica completa comprovada.";
			} else {
				row.patternTargets = std::move(targets);
				row.patternX = layout.patternX;
				row.patternY = layout.patternY;
				row.patternZ = layout.patternZ;
				row.decision = Decision::ContextualExact;
				row.reason = "Cada padrão tem um único equivalente exato. O ID de destino será escolhido automaticamente pela posição X/Y/Z de cada tile.";
			}
		}
		return rows;
	}
	void ResolveAutomatically(Plan& plan, AutomaticPolicy policy, const Control& control) {
		if (!plan.source || !plan.target || !plan.error.empty()) {
			throw std::runtime_error("Analise os recursos de origem e destino primeiro.");
		}
		std::map<uint16_t, uint16_t> reviewed;
		for (const auto& row : plan.rows) {
			if (row.decision == Decision::Reviewed) {
				reviewed[row.sourceId] = row.targetId;
			}
		}
		Plan next;
		next.source = plan.source;
		next.target = plan.target;
		// Immutable catalogs and inventory bind the existing evidence. Re-run only
		// when dropping a more permissive policy, never immediately after Analyze.
		next.rows = policy < plan.automaticPolicy || plan.rows.size() != plan.inventory.usedIds.size()
				|| plan.matchedSourceFingerprint != plan.source->fingerprint || plan.matchedTargetFingerprint != plan.target->fingerprint
				|| plan.matchedMapDigest != plan.inventory.mapFile.sha256
			? Match(plan.inventory, *plan.source, *plan.target, control)
			: plan.rows;
		for (const auto& [sourceId, targetId] : reviewed) {
			std::string error;
			if (!Resolve(next, sourceId, targetId, error)) {
				throw std::runtime_error("Decisão revisada deixou de ser compatível: " + error);
			}
		}
		if (policy == AutomaticPolicy::CompatibleApproximation) {
			for (auto& row : next.rows) {
				control.check();
				if (row.resolved() || row.candidates.empty() || row.decision == Decision::Unverifiable || row.decision == Decision::Incompatible) {
					continue;
				}
				const auto& best = row.candidates.front();
				const double margin = row.candidates.size() > 1 ? best.score - row.candidates[1].score : 1.;
				const auto& source = plan.source->items.at(row.sourceId);
				const auto& target = plan.target->items.at(best.serverId);
				// Generic texture similarity is insufficient for interactive objects
				// and uncertain walls. Do not turn the highest score into a blind rule.
				if (row.roles & (FunctionalRoles & ~uint64_t(Ground))) {
					continue;
				}
				if (source.flags & ((1u << 0) | (1u << 1) | (1u << 4) | (1u << 14) | (1u << 15))) {
					continue;
				}
				if (!source.visual || !target.visual || !source.visual->allVariantsVerified || !target.visual->allVariantsVerified) {
					continue;
				}
				const auto& a = *source.visual;
				const auto& b = *target.visual;
				const double threshold = (row.roles & Ground) ? .97 : .95;
				if (best.visualSimilarity < threshold || margin < .035 || !SameProperties(source, target) || !SamePlacement(a.layout, b.layout)) {
					continue;
				}
				if (std::popcount(a.shape ^ b.shape) > 4 || std::popcount(a.luminanceHash ^ b.luminanceHash) > 8) {
					continue;
				}
				Assign(row, best, Decision::Approximate, "Aproximação automática em lote solicitada. Todas as variações, tipo e propriedades comparados; a aparência difere e deve ser testada. Não é equivalência exata.");
			}
		}
		next.visualDraft = policy == AutomaticPolicy::VisualDraft;
		if (next.visualDraft) {
			ResolveVisualDraftRows(next, control);
		}
		control.check();
		plan.rows = std::move(next.rows);
		plan.visualDraft = next.visualDraft;
		plan.automaticPolicy = policy;
		plan.matchedSourceFingerprint = plan.source->fingerprint;
		plan.matchedTargetFingerprint = plan.target->fingerprint;
		plan.matchedMapDigest = plan.inventory.mapFile.sha256;
	}
	bool Resolve(Plan& plan, uint16_t sourceId, uint16_t targetId, std::string& error) {
		error.clear();
		if (!plan.source || !plan.target) {
			error = "Analise os dois catálogos primeiro.";
			return false;
		}
		auto row = std::find_if(plan.rows.begin(), plan.rows.end(), [&](const auto& value) { return value.sourceId == sourceId; });
		const auto source = plan.source->items.find(sourceId), target = plan.target->items.find(targetId);
		if (row == plan.rows.end() || source == plan.source->items.end() || target == plan.target->items.end()) {
			error = "ID ausente do mapa ou dos catálogos.";
			return false;
		}
		if (!target->second.visual || !target->second.visual->valid) {
			error = "O gráfico do destino não pode ser carregado.";
			return false;
		}
		if (!Compatible(source->second, target->second, row->usage.requiredRoles)) {
			error = "Tipos funcionais incompatíveis: ground, container, porta, teleporte ou direção de floor-change não podem ser trocados.";
			return false;
		}
		if ((row->roles & Ground) && !target->second.visual->visiblePixels && source->second.visual && source->second.visual->visiblePixels) {
			error = "O destino deste ground é totalmente transparente.";
			return false;
		}
		row->targetId = targetId;
		row->targetClientId = target->second.clientId;
		row->patternTargets.clear();
		row->patternX = row->patternY = row->patternZ = 1;
		row->decision = Decision::Reviewed;
		row->confidence = 0;
		row->reason = "Substituição revisada pelo usuário, aplicada uma única vez aos IDs originais de todas as ocorrências.";
		if ((row->roles & Ground) && !target->second.visual->visiblePixels) {
			row->reason += " Ground intencionalmente transparente, confirmado explicitamente; não produz piso visível.";
		}
		return true;
	}
	std::string DecisionName(Decision decision) {
		switch (decision) {
			case Decision::Identity:
				return "Auto (identidade)";
			case Decision::Exact:
				return "Auto (exato)";
			case Decision::HighConfidence:
				return "Auto (alta confiança)";
			case Decision::Suggested:
				return "Sugestão";
			case Decision::Ambiguous:
				return "Ambíguo";
			case Decision::Missing:
				return "Ausente";
			case Decision::Incompatible:
				return "Incompatível";
			case Decision::Unverifiable:
				return "Não verificável";
			case Decision::Reviewed:
				return "Revisado";
			case Decision::Approximate:
				return "Auto (aproximação para teste)";
			case Decision::ContextualExact:
				return "Auto (padrão por posição)";
			case Decision::VisualDraft:
				return "Auto (teste visual)";
		}
		return "Desconhecido";
	}
	std::string RoleName(uint64_t roles) {
		if (roles & Ground) {
			return "Ground";
		}
		if (roles & Door) {
			return "Door";
		}
		if (roles & Window) {
			return "Window";
		}
		if (roles & Container) {
			return "Container";
		}
		if (roles & Wall) {
			return "Wall";
		}
		if (roles & Border) {
			return "Border";
		}
		if (roles & FunctionalRoles) {
			return "Special";
		}
		return "Decoration";
	}
}
