#include "migration_io.h"
#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>

namespace MapMigration {
	namespace {
		void ItemAttributes(IO::Cursor& fields, Usage& usage, Inventory& inventory) {
			while (fields.remaining()) {
				const auto attribute = fields.u8();
				switch (attribute) {
					case 4:
					case 5:
					case 22:
						fields.skip(2);
						break;
					case 6:
					case 7:
					case 19:
						static_cast<void>(fields.text());
						break;
					case 8:
						usage.requiredRoles |= Teleport;
						fields.skip(5);
						break;
					case 10:
						usage.requiredRoles |= Depot | Container;
						fields.skip(2);
						break;
					case 14:
						usage.requiredRoles |= Door;
						fields.skip(1);
						break;
					case 12:
					case 15:
					case 17:
					case 41:
						fields.skip(1);
						break;
					case 16:
					case 18:
						fields.skip(4);
						break;
					case 20:
					case 21:
						usage.requiredRoles |= Bed;
						fields.skip(4);
						break;
					case 40:
						throw std::runtime_error("Podium com outfit exige conversão de criaturas/montarias, fora do catálogo de itens clássico.");
					case 128: {
						const auto count = fields.u16();
						for (unsigned i = 0; i < count; ++i) {
							static_cast<void>(fields.text());
							switch (fields.u8()) {
								case 0:
									break;
								case 1:
									fields.skip(fields.u32());
									break;
								case 2:
								case 3:
									fields.skip(4);
									break;
								case 4:
									fields.skip(1);
									break;
								case 5:
									fields.skip(8);
									break;
								default:
									throw std::runtime_error("Tipo de atributo customizado não suportado.");
							}
						}
						break;
					}
					default:
						// Unknown payload can be copied, but a subsequent editor save
						// could discard it. Block instead of claiming a safe roundtrip.
						++inventory.opaqueItemAttributes;
						throw std::runtime_error("Atributo de item desconhecido: " + std::to_string(attribute) + ". Conversão bloqueada para permitir reabertura sem descarte.");
				}
			}
		}
	}
	Inventory InspectMap(const Path& path, const Catalog& catalog, const Control& control, bool collectContext) {
		Inventory inventory;
		inventory.mapFile = FingerprintFile(path, control);
		struct Context {
			Position area, position;
			bool inTile = false;
			unsigned nested = 0;
		};
		std::array<Context, 257> contexts {};
		IO::Digest structure;
		bool lastTileGround = false, hadTile = false;
		Position lastTilePosition;
		std::set<uint16_t> tileIds;
		std::array<uint32_t, 258> stack {};
		uint32_t currentStack = 0;
		const auto finishTile = [&] {if(hadTile&&!lastTileGround){++inventory.tilesWithoutGround;if(inventory.groundlessPositions.size()<128){inventory.groundlessPositions.push_back(lastTilePosition);
}} };
		const auto observe = [&](uint16_t id, Context& context, bool) -> Usage& {
			auto& usage = inventory.usedIds[id];
			++usage.occurrences;
			++inventory.items;
			if (tileIds.insert(id).second) {
				++usage.tiles;
			}
			if (usage.positions.size() < 32) {
				usage.positions.push_back(context.position);
				usage.stackIndices.push_back(currentStack);
			}
			if (context.nested) {
				++usage.nestedOccurrences;
				++inventory.nestedItems;
			}
			const auto item = catalog.items.find(id);
			const uint64_t roles = item == catalog.items.end() ? 0 : item->second.roles;
			if ((roles & Ground) && !context.nested && item->second.visual && item->second.visual->valid) {
				const auto& layout = item->second.visual->layout;
				const size_t patterns = size_t(layout.patternX) * layout.patternY * layout.patternZ;
				if (patterns <= 256) {
					if (usage.patternOccurrences.empty()) {
						usage.patternOccurrences.resize(patterns);
					}
					++usage.patternOccurrences[(size_t(context.position.z % layout.patternZ) * layout.patternY + context.position.y % layout.patternY) * layout.patternX + context.position.x % layout.patternX];
				}
			}
			if (roles & Ground) {
				usage.requiredRoles |= Ground;
			}
			if (!context.nested && (roles & Ground)) {
				++usage.groundOccurrences;
				++inventory.grounds;
				lastTileGround = true;
			}
			if (roles & Container) {
				++inventory.containers;
			}
			return usage;
		};
		IO::Walk(
			path, "OTBM", [&](std::vector<uint8_t>& payload, size_t depth, const std::vector<uint8_t>& parent) {
				auto context = depth ? contexts[depth - 1] : Context {};
				IO::Cursor fields { payload };
				const uint8_t type = fields.u8();
				currentStack = stack[depth]++;
				stack[depth + 1] = 0;
				auto normalized = payload;
				if (depth == 0) {
					inventory.version = fields.u32();
					inventory.width = fields.u16();
					inventory.height = fields.u16();
					inventory.itemMajorVersion = fields.u32();
					inventory.itemMinorVersion = fields.u32();
					if ((type != 0 && type != 1) || inventory.version > 5 || !inventory.width || !inventory.height || fields.remaining()) {
						throw std::runtime_error("Versão/cabeçalho OTBM não suportado para preservação exata.");
					}
					IO::Put32(normalized, 9, 0);
					IO::Put32(normalized, 13, 0);
				} else if (type == 2) {
					normalized = { type };
					while (fields.remaining()) {
						const auto attribute = fields.u8();
						const auto value = fields.text();
						normalized.push_back(attribute);
						if (attribute == 11 || attribute == 13 || attribute == 23 || attribute == 24) {
							if (value.empty()) {
								throw std::runtime_error("Referência vazia a arquivo externo.");
							}
							Sidecar sidecar;
							sidecar.attribute = attribute;
							sidecar.reference = value;
							const auto source = IO::FromUtf8(value);
							sidecar.file = FingerprintFile(source.is_absolute() ? source : path.parent_path() / source, control);
							inventory.sidecars.push_back(std::move(sidecar));
							IO::AppendText(normalized, "");
						} else if (attribute == 1 || attribute == 7) {
							IO::AppendText(normalized, value);
						} else {
							throw std::runtime_error("Atributo externo OTBM desconhecido: " + std::to_string(attribute));
						}
					}
				} else if (type == 4) {
					context.area = { fields.u16(), fields.u16(), fields.u8() };
					if (context.area.z > 15 || fields.remaining()) {
						throw std::runtime_error("Área OTBM inválida.");
					}
				} else if (type == 5 || type == 14) {
					if (parent.empty() || parent[0] != 4) {
						throw std::runtime_error("Tile fora de uma área OTBM.");
					}
					finishTile();
					tileIds.clear();
					hadTile = true;
					lastTileGround = false;
					const unsigned x = unsigned(context.area.x) + fields.u8(), y = unsigned(context.area.y) + fields.u8();
					if (x > 65535 || y > 65535) {
						throw std::runtime_error("Coordenada OTBM excede 16 bits.");
					}
					context.position = { static_cast<uint16_t>(x), static_cast<uint16_t>(y), context.area.z };
					lastTilePosition = context.position;
					context.inTile = true;
					context.nested = 0;
					++inventory.tiles;
					++inventory.floors[context.position.z];
					if (type == 14) {
						fields.skip(4);
					}
					while (fields.remaining()) {
						const auto attribute = fields.u8();
						if (attribute == 9) {
							const auto offset = fields.offset;
							const auto id = fields.u16();
							currentStack = stack[depth + 1]++;
							observe(id, context, true);
							IO::Put16(normalized, offset, 0);
							if (inventory.version == 0) {
								auto item = catalog.items.find(id);
								if (item == catalog.items.end()) {
									throw std::runtime_error("OTBM 1 exige catálogo completo para ler subtypes.");
								}
								if ((item->second.flags & (1u << 7)) || (item->second.roles & (Fluid | Splash))) {
									fields.skip(1);
								}
							}
						} else if (attribute == 3) {
							const auto flags = fields.u32();
							if (flags & 0x40u) {
								uint16_t zone;
								do {
									zone = fields.u16();
								} while (zone);
							}
						} else {
							throw std::runtime_error("Atributo de tile não suportado: " + std::to_string(attribute));
						}
					}
				} else if (type == 6) {
					if (!context.inTile) {
						throw std::runtime_error("Item OTBM fora de um tile.");
					}
					if (!parent.empty() && parent[0] == 6) {
						IO::Cursor parentFields { parent };
						parentFields.skip(1);
						const auto parentId = parentFields.u16();
						inventory.usedIds[parentId].requiredRoles |= Container;
						++context.nested;
					}
					const auto id = fields.u16();
					auto& usage = observe(id, context, false);
					IO::Put16(normalized, 1, 0);
					if (inventory.version == 0) {
						auto item = catalog.items.find(id);
						if (item == catalog.items.end()) {
							throw std::runtime_error("OTBM 1 exige catálogo completo para ler subtypes.");
						}
						if ((item->second.flags & (1u << 7)) || (item->second.roles & (Fluid | Splash))) {
							fields.skip(1);
						}
					}
					ItemAttributes(fields, usage, inventory);
				} else if (type == 3 || type == 7 || type == 8) {
					throw std::runtime_error("Este OTBM contém definições/referências legadas de tile que não podem ser remapeadas com segurança.");
				}
				const uint8_t depthByte = static_cast<uint8_t>(depth);
				structure.add(std::span(&depthByte, 1));
				std::array<uint8_t, 8> length {};
				for (size_t i = 0; i < 8; ++i) {
					length[i] = static_cast<uint8_t>(uint64_t(normalized.size()) >> (i * 8));
				}
				structure.add(length);
				structure.add(normalized);
				contexts[depth] = context;
			},
			control
		);
		finishTile();
		inventory.structureDigest = structure.finish();
		if (collectContext && !inventory.usedIds.empty()) {
			const auto key = [](Position p) { return uint64_t(p.x) | (uint64_t(p.y) << 16) | (uint64_t(p.z) << 32); };
			constexpr std::array<std::pair<int, int>, 8> directions = { { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 } } };
			std::unordered_map<uint64_t, uint16_t> wanted;
			for (auto& [id, usage] : inventory.usedIds) {
				usage.adjacentGrounds.resize(std::min<size_t>(usage.positions.size(), 8));
				for (size_t s = 0; s < usage.adjacentGrounds.size(); ++s) {
					for (auto [dx, dy] : directions) {
						const auto p = usage.positions[s];
						const int x = int(p.x) + dx, y = int(p.y) + dy;
						if (x >= 0 && x <= 65535 && y >= 0 && y <= 65535) {
							wanted.emplace(key({ uint16_t(x), uint16_t(y), p.z }), 0);
						}
					}
				}
			}
			if (wanted.size() * 40ull > control.memoryLimitBytes / 4) {
				throw std::runtime_error("Contexto amostral excede o limite de memória.");
			}
			std::array<Context, 257> contextStack {};
			IO::Walk(
				path, "OTBM", [&](std::vector<uint8_t>& data, size_t depth, const auto&) {
					auto c = depth ? contextStack[depth - 1] : Context {};
					IO::Cursor f { data };
					const auto type = f.u8();
					if (type == 4) {
						c.area = { f.u16(), f.u16(), f.u8() };
					} else if (type == 5 || type == 14) {
						const auto dx = f.u8(), dy = f.u8();
						c.position = { uint16_t(c.area.x + dx), uint16_t(c.area.y + dy), c.area.z };
						c.nested = 0;
						if (type == 14) {
							f.skip(4);
						}
						while (f.remaining()) {
							const auto attr = f.u8();
							if (attr == 9) {
								const auto id = f.u16();
								auto item = catalog.items.find(id);
								if (item != catalog.items.end()) {
									if (item->second.roles & Ground) {
										if (auto it = wanted.find(key(c.position)); it != wanted.end()) {
											it->second = id;
										}
									}
									if (inventory.version == 0 && ((item->second.flags & (1u << 7)) || (item->second.roles & (Fluid | Splash)))) {
										f.skip(1);
									}
								}
							} else if (attr == 3) {
								const auto flags = f.u32();
								if (flags & 0x40) {
									while (f.u16()) { };
								}
							}
						}
					} else if (type == 6) {
						if (depth && data.size() >= 3) {
							if (contextStack[depth - 1].nested) {
								c.nested = contextStack[depth - 1].nested;
							}
						}
						const auto id = f.u16();
						auto item = catalog.items.find(id);
						if (!c.nested && item != catalog.items.end() && (item->second.roles & Ground)) {
							if (auto it = wanted.find(key(c.position)); it != wanted.end()) {
								it->second = id;
							}
						}
						++c.nested;
					}
					contextStack[depth] = c;
				},
				control
			);
			for (auto& [id, usage] : inventory.usedIds) {
				for (size_t s = 0; s < usage.adjacentGrounds.size(); ++s) {
					for (size_t d = 0; d < 8; ++d) {
						const auto [dx, dy] = directions[d];
						const auto p = usage.positions[s];
						const int x = int(p.x) + dx, y = int(p.y) + dy;
						if (x >= 0 && x <= 65535 && y >= 0 && y <= 65535) {
							usage.adjacentGrounds[s][d] = wanted.at(key({ uint16_t(x), uint16_t(y), p.z }));
						}
					}
				}
			}
		}
		return inventory;
	}
}
