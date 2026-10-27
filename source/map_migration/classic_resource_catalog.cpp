#include "migration_io.h"
#include "../ext/pugixml.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace MapMigration {
	namespace {
		using IO::Cursor;
		struct ClassicOptions {
			bool extended = false, alpha = false, durations = false, groups = false;
			bool specified = false;
		};
		ClassicOptions ReadOptions(ResourceSet& paths) {
			ClassicOptions result;
			if (paths.otfi.empty()) {
				auto candidate = paths.dat;
				candidate.replace_extension(".otfi");
				if (std::filesystem::is_regular_file(candidate)) {
					paths.otfi = candidate;
				}
			}
			if (paths.otfi.empty()) {
				return result;
			}
			std::ifstream file(paths.otfi);
			if (!file) {
				throw std::runtime_error("Não foi possível ler o OTFI.");
			}
			std::string line;
			while (std::getline(file, line)) {
				const auto colon = line.find(':');
				if (colon == std::string::npos) {
					continue;
				}
				auto key = line.substr(0, colon), value = line.substr(colon + 1);
				const auto trim = [](std::string& s) { const auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos){s.clear();return;}s=s.substr(a,s.find_last_not_of(" \t\r")-a+1); };
				trim(key);
				trim(value);
				const bool enabled = value == "true" || value == "1";
				if (key == "extended") {
					result.extended = enabled;
				} else if (key == "transparency") {
					result.alpha = enabled;
				} else if (key == "frame-durations") {
					result.durations = enabled;
				} else if (key == "frame-groups") {
					result.groups = enabled;
				} else if (key == "extended-items" && enabled) {
					throw std::runtime_error("DAT com IDs de item de 32 bits não é suportado pelo OTBM clássico de 16 bits.");
				} else if (key == "sprite-size" && value != "32") {
					throw std::runtime_error("Este conjunto clássico usa tamanho de sprite não suportado; esperado 32 pixels.");
				}
			}
			result.specified = true;
			return result;
		}
		uint8_t NormalizeFlag(uint8_t flag, DatDialect dialect) {
			if (dialect == DatDialect::Modern1010) {
				return flag == 16 ? 253 : (flag > 16 ? flag - 1 : flag);
			}
			if (dialect == DatDialect::Classic78) {
				return flag == 8 ? 254 : (flag > 8 ? flag - 1 : flag);
			}
			if (dialect == DatDialect::Classic755) {
				return flag == 23 ? 252 : flag;
			}
			if (dialect == DatDialect::Classic74) {
				if (flag > 0 && flag <= 15) {
					++flag;
				} else {
					static constexpr std::array<uint8_t, 13> map = { 21, 252, 30, 25, 24, 21, 28, 20, 26, 17, 18, 19, 27 };
					if (flag >= 16 && flag <= 28) {
						flag = map[flag - 16];
					}
				}
				if (flag == 6) {
					flag = 7;
				} else if (flag == 7) {
					flag = 6;
				}
			}
			return flag;
		}
		Layout ReadLayout(Cursor& cursor, const ClassicOptions&, DatDialect dialect) {
			Layout layout;
			bool end = false;
			for (int count = 0; count < 255; ++count) {
				const auto raw = cursor.u8();
				if (raw == 255) {
					end = true;
					break;
				}
				const auto flag = NormalizeFlag(raw, dialect);
				if (flag < 64) {
					layout.datFlags |= uint64_t(1) << flag;
				}
				switch (flag) {
					case 0:
						layout.groundSpeed = cursor.u16();
						break;
					case 8:
					case 9:
					case 29:
					case 32:
					case 34:
						cursor.skip(2);
						break;
					case 21:
						cursor.skip(4);
						break;
					case 24:
						if (dialect == DatDialect::Classic74) {
							layout.offsetX = 8;
							layout.offsetY = 8;
						} else {
							layout.offsetX = cursor.u16();
							layout.offsetY = cursor.u16();
						}
						break;
					case 25:
						layout.elevation = cursor.u16();
						break;
					case 28:
						cursor.skip(2);
						break;
					case 33:
						cursor.skip(6);
						static_cast<void>(cursor.text());
						cursor.skip(4);
						break;
					default:
						if (flag > 37 && flag < 252) {
							throw std::runtime_error("Flag DAT desconhecida: " + std::to_string(raw));
						}
						break;
				}
			}
			if (!end) {
				throw std::runtime_error("Lista de flags DAT sem terminador.");
			}
			return layout;
		}
		void ReadGroups(Cursor& cursor, Layout& layout, const ClassicOptions& options, DatDialect dialect) {
			layout.width = cursor.u8();
			layout.height = cursor.u8();
			if (layout.width > 1 || layout.height > 1) {
				cursor.skip(1);
			}
			layout.layers = cursor.u8();
			layout.patternX = cursor.u8();
			layout.patternY = cursor.u8();
			layout.patternZ = dialect == DatDialect::Classic74 ? 1 : cursor.u8();
			layout.frames = cursor.u8();
			const uint64_t count = uint64_t(layout.width) * layout.height * layout.layers * layout.patternX * layout.patternY * layout.patternZ * layout.frames;
			if (!count || count > 262144 || layout.width > 16 || layout.height > 16) {
				throw std::runtime_error("Dimensões ou variações DAT inválidas.");
			}
			if (layout.frames > 1 && options.durations) {
				const auto header = cursor.take(6);
				for (auto value : header) {
					layout.durations.push_back(value);
				}
				for (unsigned frame = 0; frame < layout.frames; ++frame) {
					layout.durations.push_back(cursor.u32());
					layout.durations.push_back(cursor.u32());
				}
			}
			layout.spriteIds.reserve(static_cast<size_t>(count));
			for (uint64_t i = 0; i < count; ++i) {
				layout.spriteIds.push_back(options.extended ? cursor.u32() : cursor.u16());
			}
		}
		std::map<uint16_t, Layout> ParseDat(const std::vector<uint8_t>& bytes, ClassicOptions options, DatDialect dialect, const Control& control) {
			Cursor cursor { bytes };
			static_cast<void>(cursor.u32());
			std::array<uint16_t, 4> counts { cursor.u16(), cursor.u16(), cursor.u16(), cursor.u16() };
			if (counts[0] < 100) {
				throw std::runtime_error("DAT não contém catálogo de itens clássico.");
			}
			std::map<uint16_t, Layout> result;
			for (unsigned category = 0; category < 4; ++category) {
				for (uint32_t id = category == 0 ? 100 : 1; id <= counts[category]; ++id) {
					if (id % 128 == 0) {
						control.check();
					}
					auto prototype = ReadLayout(cursor, options, dialect);
					const unsigned groups = category == 1 && options.groups ? cursor.u8() : 1;
					if (groups == 0 || groups > 4) {
						throw std::runtime_error("Grupo de animação DAT inválido.");
					}
					for (unsigned group = 0; group < groups; ++group) {
						if (category == 1 && options.groups) {
							static_cast<void>(cursor.u8());
						}
						auto layout = prototype;
						ReadGroups(cursor, layout, options, dialect);
						if (category == 0) {
							result.emplace(static_cast<uint16_t>(id), std::move(layout));
						}
					}
				}
			}
			if (cursor.remaining() != 0) {
				throw std::runtime_error("Layout DAT não corresponde ao arquivo completo (bytes adicionais).");
			}
			return result;
		}
		class SpriteReader {
		public:
			SpriteReader(const Path& path, const ClassicOptions& options) :
				file(path, std::ios::binary), alpha(options.alpha) {
				if (!file) {
					throw std::runtime_error("Não foi possível abrir o SPR.");
				}
				std::array<uint8_t, 8> header {};
				file.read(reinterpret_cast<char*>(header.data()), options.extended ? 8 : 6);
				if (!file) {
					throw std::runtime_error("Cabeçalho SPR truncado.");
				}
				Cursor cursor { header };
				signature = cursor.u32();
				const auto count = options.extended ? cursor.u32() : cursor.u16();
				fileSize = std::filesystem::file_size(path);
				if (uint64_t(count) * 4 + 8 > fileSize + 2 || count > 16000000) {
					throw std::runtime_error("Índice SPR inválido.");
				}
				offsets.resize(static_cast<size_t>(count) + 1);
				std::vector<uint8_t> index(static_cast<size_t>(count) * 4);
				if (!file.read(reinterpret_cast<char*>(index.data()), index.size())) {
					throw std::runtime_error("Índice SPR truncado.");
				}
				Cursor values { index };
				for (uint32_t id = 1; id <= count; ++id) {
					offsets[id] = values.u32();
				}
			}
			std::shared_ptr<const std::vector<uint8_t>> pixels(uint32_t id) {
				if (auto found = cache.find(id); found != cache.end()) {
					return found->second;
				}
				auto result = std::make_shared<std::vector<uint8_t>>(32 * 32 * 4, uint8_t(0));
				if (id >= offsets.size()) {
					throw std::runtime_error("ID de sprite fora do SPR: " + std::to_string(id));
				}
				if (id && offsets[id]) {
					const uint64_t offset = offsets[id];
					if (offset + 5 > fileSize) {
						throw std::runtime_error("Offset SPR inválido.");
					}
					file.clear();
					file.seekg(static_cast<std::streamoff>(offset + 3));
					std::array<uint8_t, 2> sizeBytes {};
					file.read(reinterpret_cast<char*>(sizeBytes.data()), 2);
					Cursor sizeCursor { sizeBytes };
					const auto size = sizeCursor.u16();
					if (offset + 5 + size > fileSize) {
						throw std::runtime_error("Conteúdo SPR truncado.");
					}
					std::vector<uint8_t> compressed(size);
					if (!file.read(reinterpret_cast<char*>(compressed.data()), size)) {
						throw std::runtime_error("Falha ao ler sprite.");
					}
					Cursor rle { compressed };
					size_t position = 0;
					while (rle.remaining()) {
						const auto transparent = rle.u16(), coloured = rle.u16();
						position += transparent;
						if (position > 1024 || coloured > 1024 - position) {
							throw std::runtime_error("RLE SPR excede 32x32 pixels.");
						}
						for (unsigned i = 0; i < coloured; ++i, ++position) {
							auto* target = result->data() + position * 4;
							target[0] = rle.u8();
							target[1] = rle.u8();
							target[2] = rle.u8();
							target[3] = alpha ? rle.u8() : 255;
							if (!target[3]) {
								target[0] = target[1] = target[2] = 0;
							}
						}
					}
				}
				if (cache.size() >= 8192) {
					cache.clear(); // At most 32 MiB of decoded SPR tiles.
				}
				cache.emplace(id, result);
				return result;
			}
			uint32_t signature = 0;

		private:
			std::ifstream file;
			bool alpha;
			uint64_t fileSize = 0;
			std::vector<uint32_t> offsets;
			std::unordered_map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> cache;
		};
		std::vector<uint8_t> Compose(const Layout& layout, size_t variant, SpriteReader& sprites) {
			const size_t width = size_t(layout.width) * 32, height = size_t(layout.height) * 32;
			std::vector<uint8_t> rgba(width * height * 4, 0);
			for (unsigned layer = 0; layer < layout.layers; ++layer) {
				for (unsigned tileY = 0; tileY < layout.height; ++tileY) {
					for (unsigned tileX = 0; tileX < layout.width; ++tileX) {
						const size_t index = ((variant * layout.layers + layer) * layout.height + tileY) * layout.width + tileX;
						const auto pixels = sprites.pixels(layout.spriteIds.at(index));
						for (size_t y = 0; y < 32; ++y) {
							for (size_t x = 0; x < 32; ++x) {
								const auto* src = pixels->data() + (y * 32 + x) * 4;
								auto* dst = rgba.data() + (((layout.height - tileY - 1) * 32 + y) * width + (layout.width - tileX - 1) * 32 + x) * 4;
								const unsigned alpha = src[3], inverse = 255 - alpha, newAlpha = alpha + (dst[3] * inverse + 127) / 255;
								for (unsigned c = 0; c < 3; ++c) {
									dst[c] = newAlpha ? static_cast<uint8_t>((src[c] * alpha + (dst[c] * dst[3] * inverse + 127) / 255 + newAlpha / 2) / newAlpha) : 0;
								}
								dst[3] = static_cast<uint8_t>(newAlpha);
							}
						}
					}
				}
			}
			return rgba;
		}
		std::shared_ptr<const Visual> BuildVisual(const Layout& layout, SpriteReader& sprites, const Control& control) {
			auto result = std::make_shared<Visual>();
			result->layout = layout;
			try {
				const uint64_t variants = uint64_t(layout.patternX) * layout.patternY * layout.patternZ * layout.frames;
				if (variants * layout.width * layout.height * 1024 > 16ull * 1024 * 1024) {
					throw std::runtime_error("Item excede o orçamento de comparação de variações.");
				}
				IO::Digest complete;
				std::vector<uint8_t> first;
				for (uint64_t v = 0; v < variants; ++v) {
					control.check();
					auto rgba = Compose(layout, static_cast<size_t>(v), sprites);
					complete.add(rgba);
					result->variantPixels.push_back(Sha256(rgba));
					std::array<uint8_t, 256> feature {};
					const size_t w = size_t(layout.width) * 32, h = size_t(layout.height) * 32;
					for (size_t by = 0; by < 8; ++by) {
						for (size_t bx = 0; bx < 8; ++bx) {
							uint64_t alpha = 0, colour[3] = {};
							size_t samples = 0;
							for (size_t y = by * h / 8; y < (by + 1) * h / 8; ++y) {
								for (size_t x = bx * w / 8; x < (bx + 1) * w / 8; ++x) {
									const auto* p = rgba.data() + (y * w + x) * 4;
									alpha += p[3];
									++samples;
									for (size_t c = 0; c < 3; ++c) {
										colour[c] += uint64_t(p[c]) * p[3];
									}
								}
							}
							const size_t at = (by * 8 + bx) * 4;
							for (size_t c = 0; c < 3; ++c) {
								feature[at + c] = alpha ? static_cast<uint8_t>(colour[c] / alpha) : 0;
							}
							feature[at + 3] = samples ? static_cast<uint8_t>(alpha / samples) : 0;
						}
					}
					result->variantFeatures.push_back(feature);
					for (size_t a = 3; a < rgba.size(); a += 4) {
						if (rgba[a]) {
							result->visiblePixels = true;
							break;
						}
					}
					if (v == 0) {
						first = std::move(rgba);
					}
				}
				result->completePixels = complete.finish();
				result->firstPixels = Sha256(first);
				result->previewWidth = result->previewHeight = 32;
				result->preview.resize(32 * 32 * 4);
				const size_t width = size_t(layout.width) * 32, height = size_t(layout.height) * 32;
				double alphaWeight = 0;
				for (size_t y = 0; y < 32; ++y) {
					for (size_t x = 0; x < 32; ++x) {
						const auto* p = first.data() + ((y * height / 32) * width + x * width / 32) * 4;
						std::copy_n(p, 4, result->preview.data() + (y * 32 + x) * 4);
						if (p[3]) {
							const size_t bin = (p[0] >> 6) * 8 + (p[1] >> 6) * 2 + (p[2] >> 7);
							result->histogram[bin] += p[3];
							alphaWeight += p[3];
							result->shape |= uint64_t(1) << ((y / 4) * 8 + x / 4);
						}
					}
				}
				if (alphaWeight) {
					for (auto& value : result->histogram) {
						value /= alphaWeight;
					}
				}
				if (!result->variantFeatures.empty()) {
					const auto& f = result->variantFeatures.front();
					uint64_t total = 0;
					std::array<uint32_t, 64> luminance {};
					for (size_t p = 0; p < 64; ++p) {
						luminance[p] = (uint32_t(f[p * 4]) * 299 + uint32_t(f[p * 4 + 1]) * 587 + uint32_t(f[p * 4 + 2]) * 114) * f[p * 4 + 3];
						total += luminance[p];
					}
					for (size_t p = 0; p < 64; ++p) {
						if (uint64_t(luminance[p]) * 64 > total) {
							result->luminanceHash |= uint64_t(1) << p;
						}
					}
				}
				result->valid = true;
				result->allVariantsVerified = true;
			} catch (const std::exception& error) {
				control.check();
				result->error = error.what();
			}
			return result;
		}
		uint64_t GroupRoles(uint8_t group) {
			switch (group) {
				case 1:
					return Ground;
				case 2:
					return Container;
				case 7:
					return Teleport;
				case 8:
					return Field;
				case 10:
					return Key;
				case 11:
					return Splash;
				case 12:
					return Fluid;
				case 13:
					return Door;
				case 15:
					return Podium;
				default:
					return 0;
			}
		}
		void ReadXml(Catalog& catalog) {
			if (catalog.resources.xml.empty()) {
				catalog.warnings.push_back("items.xml ausente: nomes e classificações adicionais não foram usados.");
				return;
			}
			pugi::xml_document document;
			const auto bytes = IO::ReadFile(catalog.resources.xml, 32ull * 1024 * 1024);
			if (!document.load_buffer(bytes.data(), bytes.size()) || !document.child("items")) {
				throw std::runtime_error("items.xml inválido.");
			}
			for (auto node : document.child("items").children("item")) {
				const unsigned begin = node.attribute("id") ? node.attribute("id").as_uint() : node.attribute("fromid").as_uint();
				const unsigned end = node.attribute("id") ? begin : node.attribute("toid").as_uint();
				if (begin == 0 || end > 65535 || end < begin) {
					continue;
				}
				std::map<std::string, std::string> attributes;
				for (auto attr : node.children("attribute")) {
					attributes[attr.attribute("key").as_string()] = attr.attribute("value").as_string();
				}
				for (unsigned id = begin; id <= end; ++id) {
					auto found = catalog.items.find(static_cast<uint16_t>(id));
					if (found == catalog.items.end()) {
						continue;
					}
					auto& item = found->second;
					item.name = node.attribute("name").as_string();
					for (const auto& [key, value] : attributes) {
						item.xmlProperties += key + "=" + value + "\n";
					}
					if (auto type = attributes.find("type"); type != attributes.end()) {
						item.type = type->second;
						const std::map<std::string, uint64_t> roles = { { "container", Container }, { "door", Door }, { "teleport", Teleport }, { "depot", Depot | Container }, { "bed", Bed }, { "magicfield", Field }, { "key", Key }, { "podium", Podium }, { "mailbox", Mailbox } };
						if (auto role = roles.find(item.type); role != roles.end()) {
							item.roles |= role->second;
							item.knownRoles |= FunctionalRoles;
						}
					}
					for (const auto& [key, role] : std::array<std::pair<const char*, uint64_t>, 3> { { { "iswall", Wall }, { "isborder", Border }, { "iswindow", Window } } }) {
						if (auto attr = attributes.find(key); attr != attributes.end()) {
							item.knownRoles |= role;
							if (attr->second == "true" || attr->second == "1") {
								item.roles |= role;
							}
						}
					}
				}
			}
		}
	}

	Catalog LoadCatalog(const ResourceSet& resources, const Control& control) {
		Catalog catalog;
		catalog.resources = resources;
		if (resources.otb.empty()) {
			throw std::runtime_error("Selecione o items.otb: DAT/SPR isolados não identificam os IDs de servidor.");
		}
		if (resources.dat.empty() || resources.spr.empty()) {
			throw std::runtime_error("Selecione DAT e SPR para comparar os gráficos reais.");
		}
		auto options = ReadOptions(catalog.resources);
		for (const auto& path : { resources.otb, resources.xml, resources.dat, resources.spr, catalog.resources.otfi }) {
			if (!path.empty()) {
				catalog.files.push_back(FingerprintFile(path, control));
			}
		}
		IO::Digest resourcesHash;
		for (const auto& file : catalog.files) {
			resourcesHash.add(std::to_string(file.size) + ":" + file.sha256 + ";");
		}
		catalog.fingerprint = resourcesHash.finish();
		std::map<uint16_t, unsigned> clientUses;
		IO::Walk(
			resources.otb, "OTBI", [&](std::vector<uint8_t>& data, size_t depth, const auto&) {
				Cursor cursor { data };
				const auto group = cursor.u8();
				if (depth == 0) {
					cursor.skip(4);
					if (cursor.u8() != 1) {
						throw std::runtime_error("OTB sem cabeçalho de versão.");
					}
					const auto size = cursor.u16();
					if (size < 12) {
						throw std::runtime_error("Versão OTB truncada.");
					}
					catalog.majorVersion = cursor.u32();
					catalog.minorVersion = cursor.u32();
					if (catalog.majorVersion != 3) {
						throw std::runtime_error("Este conversor exige OTB versão 3; formatos legados precisam de carregador explícito.");
					}
					return;
				}
				if (depth != 1) {
					throw std::runtime_error("Árvore OTB possui subnós inesperados.");
				}
				if (group == 14) {
					return;
				}
				CatalogItem item;
				item.group = group;
				item.flags = cursor.u32();
				item.roles = GroupRoles(group);
				item.knownRoles = FunctionalRoles;
				item.otbRecordDigest = Sha256(data);
				while (cursor.remaining()) {
					const auto attr = cursor.u8();
					const auto length = cursor.u16();
					const auto value = cursor.take(length);
					Cursor field { value };
					if (attr == 0x10 || attr == 0x11) {
						if (length != 2) {
							throw std::runtime_error("ID OTB possui tamanho inválido.");
						}
						if (attr == 0x10) {
							item.serverId = field.u16();
						} else {
							item.clientId = field.u16();
						}
					} else if (attr == 0x12) {
						item.name = { reinterpret_cast<const char*>(value.data()), value.size() };
					} else if (attr == 0x2B && length == 1) {
						item.topOrder = field.u8();
					}
				}
				if (!item.serverId || !item.clientId) {
					throw std::runtime_error("Registro OTB sem ServerID/ClientID válido.");
				}
				if (!catalog.items.emplace(item.serverId, std::move(item)).second) {
					throw std::runtime_error("ServerID duplicado no OTB: " + std::to_string(item.serverId));
				}
				if (++clientUses[item.clientId] > 1) {
					++catalog.duplicateClientIds;
				}
			},
			control
		);
		ReadXml(catalog);
		const auto dat = IO::ReadFile(resources.dat, std::min<uint64_t>(control.memoryLimitBytes / 4, 64ull * 1024 * 1024));
		Cursor header { dat };
		catalog.datSignature = header.u32();
		std::map<uint16_t, Layout> layouts;
		if (resources.dialect != DatDialect::Automatic) {
			layouts = ParseDat(dat, options, resources.dialect, control);
		} else {
			std::string lastError;
			for (const auto dialect : { DatDialect::Classic86, DatDialect::Classic78, DatDialect::Modern1010, DatDialect::Classic755, DatDialect::Classic74 }) {
				try {
					layouts = ParseDat(dat, options, dialect, control);
					catalog.resources.dialect = dialect;
					break;
				} catch (const std::exception& error) {
					control.check();
					lastError = error.what();
				}
			}
			if (layouts.empty()) {
				throw std::runtime_error("Nenhum layout DAT suportado corresponde ao arquivo completo: " + lastError);
			}
		}
		resourcesHash.add("nexamap-classic-catalog-v1:dialect=" + std::to_string(static_cast<int>(catalog.resources.dialect)));
		catalog.fingerprint = resourcesHash.finish();
		SpriteReader sprites(resources.spr, options);
		catalog.sprSignature = sprites.signature;
		std::unordered_map<uint16_t, std::shared_ptr<const Visual>> visuals;
		size_t completed = 0;
		uint64_t visualMemory = 0;
		for (auto& [id, item] : catalog.items) {
			if (++completed % 32 == 0) {
				control.update(static_cast<int>(completed * 100 / catalog.items.size()), "Comparando variações gráficas dos recursos...");
			}
			if (auto found = visuals.find(item.clientId); found != visuals.end()) {
				item.visual = found->second;
			} else {
				std::shared_ptr<const Visual> visual;
				if (auto layout = layouts.find(item.clientId); layout != layouts.end()) {
					visual = BuildVisual(layout->second, sprites, control);
				} else {
					auto missing = std::make_shared<Visual>();
					missing->error = "ClientID inexistente no DAT.";
					visual = missing;
				}
				visuals.emplace(item.clientId, visual);
				visualMemory += sizeof(Visual) + visual->preview.capacity() + visual->layout.spriteIds.capacity() * sizeof(uint32_t) + visual->layout.durations.capacity() * sizeof(uint32_t) + visual->variantFeatures.capacity() * sizeof(std::array<uint8_t, 256>) + visual->variantPixels.capacity() * (sizeof(std::string) + 64);
				item.visual = std::move(visual);
			}
			if (item.visual && item.visual->valid) {
				item.knownRoles |= Border;
				const auto flags = item.visual->layout.datFlags;
				if (flags & 1) {
					item.roles |= Ground;
				}
				if (flags & (1ull << 4)) {
					item.roles |= Container;
				}
				if (flags & (1ull << 10)) {
					item.roles |= Fluid;
				}
				if (flags & (1ull << 11)) {
					item.roles |= Splash;
				}
				if (flags & (1ull << 1)) {
					item.roles |= Border;
					item.knownRoles |= Border;
				}
			}
			if (visualMemory > control.memoryLimitBytes / 3) {
				throw std::runtime_error("Catálogo excede o orçamento de previews; aumente o limite de memória.");
			}
		}
		control.update(100, "Catálogo concluído.");
		return catalog;
	}
}
