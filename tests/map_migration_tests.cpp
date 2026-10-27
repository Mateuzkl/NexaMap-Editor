#include "map_migration/map_migration.h"
#include "map_migration/migration_io.h"
#include "file_transaction.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

namespace {
	using namespace MapMigration;
	using Bytes = std::vector<uint8_t>;
	struct Node {
		Bytes data;
		std::vector<Node> children;
	};
	void Check(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	void U16(Bytes& bytes, uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}
	void U32(Bytes& bytes, uint32_t value) {
		U16(bytes, static_cast<uint16_t>(value));
		U16(bytes, static_cast<uint16_t>(value >> 16));
	}
	void Write(const Path& path, const Bytes& bytes) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		Check(bool(file), "write fixture");
	}
	void TextFile(const Path& path, const std::string& text) {
		Write(path, Bytes(text.begin(), text.end()));
	}
	void Tree(const Path& path, const char* magic, const Node& root) {
		Bytes bytes(magic, magic + 4);
		std::function<void(const Node&)> node = [&](const Node& n) {bytes.push_back(0xFE);for(auto c:n.data){if(c>=0xFD){bytes.push_back(0xFD);
}bytes.push_back(c);}for(const auto& child:n.children){node(child);
}bytes.push_back(0xFF); };
		node(root);
		Write(path, bytes);
	}
	void Field(Bytes& data, uint8_t key, const Bytes& bytes) {
		data.push_back(key);
		U16(data, static_cast<uint16_t>(bytes.size()));
		data.insert(data.end(), bytes.begin(), bytes.end());
	}
	Bytes Word(uint16_t value) {
		Bytes data;
		U16(data, value);
		return data;
	}
	struct Definition {
		uint16_t sid, cid;
		uint8_t group;
		uint32_t flags = 0;
	};
	struct Set {
		Path root;
		ResourceSet paths;
		Set(Path path, const std::vector<Definition>& definitions, bool alpha = false, bool secondLayer = false, bool secondFrameDifferent = false, uint8_t groundPatterns = 1) :
			root(std::move(path)) {
			paths = { root / "items.otb", {}, root / "Tibia.dat", root / "Tibia.spr", root / "Tibia.otfi" };
			Node otb { { 0, 0, 0, 0, 0 }, {} };
			Bytes version;
			U32(version, 3);
			U32(version, 20);
			U32(version, 1);
			version.resize(140);
			Field(otb.data, 1, version);
			for (const auto& def : definitions) {
				Node item { { def.group }, {} };
				U32(item.data, def.flags);
				Field(item.data, 0x10, Word(def.sid));
				Field(item.data, 0x11, Word(def.cid));
				otb.children.push_back(item);
			}
			Tree(paths.otb, "OTBI", otb);
			Bytes dat;
			U32(dat, 0x4C2C7993);
			U16(dat, 105);
			U16(dat, 0);
			U16(dat, 0);
			U16(dat, 0);
			for (unsigned cid = 100; cid <= 105; ++cid) {
				if (cid == 100 || cid == 105) {
					dat.push_back(0);
					U16(dat, 150);
				}
				if (cid == 101) {
					dat.push_back(4);
				}
				if (cid == 104) {
					dat.push_back(5);
				}
				dat.push_back(255);
				dat.insert(dat.end(), { 1, 1, static_cast<uint8_t>(secondLayer ? 2 : 1), static_cast<uint8_t>(cid == 100 ? groundPatterns : 1), 1, 1, static_cast<uint8_t>(secondFrameDifferent ? 2 : 1) });
				for (unsigned frame = 0; frame < (secondFrameDifferent ? 2u : 1u); ++frame) {
					for (unsigned pattern = 0; pattern < (cid == 100 ? groundPatterns : 1u); ++pattern) {
						U32(dat, frame == 1 || pattern ? 6 : cid - 99);
						if (secondLayer) {
							U32(dat, 0);
						}
					}
				}
			}
			Write(paths.dat, dat);
			Bytes spr;
			U32(spr, 0x4C220594);
			U32(spr, 6);
			spr.resize(8 + 6 * 4);
			constexpr std::array<std::array<uint8_t, 3>, 6> colours = { { { 20, 180, 35 }, { 220, 180, 10 }, { 20, 30, 220 }, { 230, 10, 20 }, { 150, 80, 20 }, { 190, 30, 190 } } };
			for (size_t sprite = 0; sprite < colours.size(); ++sprite) {
				IO::Put32(spr, 8 + sprite * 4, static_cast<uint32_t>(spr.size()));
				spr.insert(spr.end(), { 255, 0, 255 });
				Bytes pixels;
				U16(pixels, 0);
				U16(pixels, 1024);
				for (unsigned pixel = 0; pixel < 1024; ++pixel) {
					pixels.insert(pixels.end(), colours[sprite].begin(), colours[sprite].end());
					if (alpha) {
						pixels.push_back(255);
					}
				}
				U16(spr, static_cast<uint16_t>(pixels.size()));
				spr.insert(spr.end(), pixels.begin(), pixels.end());
			}
			Write(paths.spr, spr);
			TextFile(paths.otfi, std::string("DatSpr\n  extended: true\n  transparency: ") + (alpha ? "true" : "false") + "\n  frame-durations: false\n  frame-groups: false\n");
		}
	};
	Path MapFile(const Path& root, bool nested = true, bool sidecars = true, uint16_t ground = 100, uint16_t container = 101) {
		Node map { { 0 }, {} };
		U32(map.data, 3);
		U16(map.data, 512);
		U16(map.data, 512);
		U32(map.data, 3);
		U32(map.data, 20);
		Node data { { 2 }, {} };
		data.data.push_back(1);
		IO::AppendText(data.data, "Mapa São João: geometry and attributes");
		if (sidecars) {
			for (const auto& [attribute, name] : std::array<std::pair<uint8_t, const char*>, 4> { { { 11, "world-spawn.xml" }, { 13, "world-house.xml" }, { 23, "world-npc.xml" }, { 24, "world-zones.xml" } } }) {
				data.data.push_back(attribute);
				IO::AppendText(data.data, name);
				TextFile(root / name, std::string("<fixture name=\"") + name + "\"/>\n");
			}
		}
		Node area { { 4 }, {} };
		U16(area.data, 256);
		U16(area.data, 256);
		area.data.push_back(7);
		Node tile { { 5, 1, 2, 9 }, {} };
		U16(tile.data, ground);
		if (nested) {
			Node bag { { 6 }, {} };
			U16(bag.data, container);
			bag.data.push_back(4);
			U16(bag.data, 1200);
			bag.data.push_back(5);
			U16(bag.data, 4500);
			bag.data.push_back(6);
			IO::AppendText(bag.data, "Texto especial: çã");
			Node inside { { 6 }, {} };
			U16(inside.data, 104);
			inside.data.insert(inside.data.end(), { 15, 17 });
			Node nestedBag { { 6 }, {} };
			U16(nestedBag.data, container);
			nestedBag.children.push_back(inside);
			bag.children.push_back(nestedBag);
			tile.children.push_back(bag);
			Node door { { 6 }, {} };
			U16(door.data, 102);
			door.data.insert(door.data.end(), { 14, 23 });
			tile.children.push_back(door);
			Node teleport { { 6 }, {} };
			U16(teleport.data, 103);
			teleport.data.push_back(8);
			U16(teleport.data, 300);
			U16(teleport.data, 310);
			teleport.data.push_back(6);
			tile.children.push_back(teleport);
		}
		area.children.push_back(tile);
		data.children.push_back(area);
		Node upstairs = area;
		upstairs.data.back() = 6;
		upstairs.children.front().data[1] = 3;
		data.children.push_back(upstairs);
		Node towns { { 12 }, { { Bytes { 13, 1, 0, 0, 0, 0, 0, 10, 0, 20, 0, 7 }, {} } } };
		data.children.push_back(towns);
		map.children.push_back(data);
		auto path = root / "world.otbm";
		Tree(path, "OTBM", map);
		return path;
	}
	const std::vector<Definition> Source = { { 100, 100, 1 }, { 101, 101, 2 }, { 102, 102, 13 }, { 103, 103, 7 }, { 104, 104, 0, 1u << 7 } };
	const std::vector<Definition> Target = { { 500, 100, 1 }, { 501, 101, 2 }, { 502, 102, 13 }, { 503, 103, 7 }, { 504, 104, 0, 1u << 7 }, { 100, 105, 1 } };
	Plan FixturePlan(const Path& directory, const Set& source, const Set& target, bool nested = true, bool sidecars = true) {
		Options options { MapFile(directory, nested, sidecars), directory / "output" / "convertido.otbm", source.paths, target.paths };
		return Analyze(options);
	}
	void TestWholeMap(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target, true, true);
		auto plan = FixturePlan(root / "map", source, target);
		Check(plan.error.empty(), plan.error.c_str());
		Check(plan.ready(), "whole map exact plan ready");
		Check(plan.inventory.tiles == 2 && plan.inventory.nestedItems == 4, "inventory includes floors and nested containers");
		Check(plan.rows.front().targetId == 500, "equal numerical server ID is not accepted without matching graphics");
		const auto sourceDigest = FingerprintFile(plan.options.sourceMap).sha256;
		const auto originalSource = source.paths.otb;
		const auto assetDigest = FingerprintFile(originalSource).sha256;
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		Check(result.validated && result.changedItems == plan.inventory.items, "complete stream validated and rewritten once");
		auto converted = InspectMap(result.outputMap, *plan.target);
		Check(converted.tiles == plan.inventory.tiles && converted.items == plan.inventory.items && converted.nestedItems == 4, "geometry and nested inventory preserved");
		Check(converted.usedIds.contains(500) && !converted.usedIds.contains(100), "actual target server IDs written");
		Check(converted.structureDigest == plan.inventory.structureDigest, "all attributes, order, towns and positions preserved byte for byte");
		Check(converted.sidecars.size() == 4, "spawn, NPC, house and zone references copied and rebased");
		for (size_t i = 0; i < 4; ++i) {
			Check(converted.sidecars[i].file.sha256 == plan.inventory.sidecars[i].file.sha256, "sidecar bytes unchanged");
		}
		Check(FingerprintFile(plan.options.sourceMap).sha256 == sourceDigest && FingerprintFile(originalSource).sha256 == assetDigest, "source map and assets untouched");
		Check(std::filesystem::exists(result.jsonReport) && std::filesystem::exists(result.csvReport), "JSON and CSV reports published");
		std::cout << "PASS whole map: two floors, nested containers, action/unique IDs, text, door, teleport, sidecars, different DAT layers and RGB/RGBA\n";
	}
	void TestAmbiguityAndRoles(const Path& root) {
		Set source(root / "source", Source);
		auto definitions = Target;
		definitions.push_back({ 505, 100, 1 });
		Set target(root / "target", definitions);
		auto plan = FixturePlan(root / "map", source, target, false, false);
		Check(plan.error.empty() && plan.rows.front().decision == Decision::Ambiguous && !plan.ready(), "equal exact candidates are ambiguous");
		Check(!Convert(plan).success && !std::filesystem::exists(plan.options.outputMap), "unresolved map cannot publish output");
		std::string error;
		Check(!Resolve(plan, 100, 501, error), "ground cannot become container");
		Check(!Resolve(plan, 100, 502, error), "ground cannot become door");
		Check(Resolve(plan, 100, 500, error) && plan.ready(), "one reviewed choice resolves every occurrence");
		const auto mapping = root / "reviewed.json";
		SaveReviewedMappings(plan, mapping);
		auto reloaded = FixturePlan(root / "other-map", source, target, false, false);
		LoadReviewedMappings(reloaded, mapping);
		Check(reloaded.ready() && reloaded.rows.front().decision == Decision::Reviewed, "reviewed mapping reused for same resource fingerprints");
		auto changed = std::make_shared<Catalog>(*reloaded.target);
		changed->fingerprint = "different resources";
		reloaded.target = changed;
		bool rejected = false;
		try {
			LoadReviewedMappings(reloaded, mapping);
		} catch (...) {
			rejected = true;
		}
		Check(rejected, "mappings from another catalog blocked");
		Check(!Compatible(plan.source->items.at(101), plan.target->items.at(500), Container), "container contents cannot be promoted to tile");
		std::cout << "PASS ambiguous IDs, strict roles, reviewed mappings and fingerprint isolation\n";
	}
	void TestAnimationAndMissing(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target, false, false, true);
		auto plan = FixturePlan(root / "map", source, target, false, false);
		Check(plan.error.empty() && !plan.ready(), "same first frame does not prove other animation frames");
		Check(plan.rows.front().decision != Decision::Exact, "animation mismatch not auto-applied");
		auto dat = IO::ReadFile(source.paths.dat, 100000);
		dat.resize(14);
		Write(source.paths.dat, dat);
		auto bad = Analyze(plan.options);
		Check(!bad.error.empty() && !bad.ready(), "truncated DAT blocks migration");
		Set missing(root / "missing", { { 600, 65000, 1 } });
		auto catalog = LoadCatalog(missing.paths);
		Check(!catalog.items.at(600).visual->valid, "missing DAT client ID is diagnosed");
		Set duplicate(root / "duplicates", { { 100, 100, 1 }, { 100, 101, 2 } });
		bool rejected = false;
		try {
			static_cast<void>(LoadCatalog(duplicate.paths));
		} catch (...) {
			rejected = true;
		}
		Check(rejected, "duplicate server ID cannot silently override catalog");
		std::cout << "PASS animation variation verification, missing sprites, duplicate IDs and malformed DAT\n";
	}
	void TestCancellationAndStale(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		auto plan = FixturePlan(root / "map", source, target);
		std::atomic<bool> cancel = false;
		Control control;
		control.cancelled = &cancel;
		control.progress = [&](int percent, const auto&) {if(percent>=90){cancel=true;
} };
		const auto result = Convert(plan, control);
		Check(result.cancelled && !result.success && !std::filesystem::exists(plan.options.outputMap), "cancel before publication leaves no map or sidecar");
		for (const auto& path : std::filesystem::recursive_directory_iterator(root / "map")) {
			Check(path.path().filename().string().find(".rme-stage") == std::string::npos, "cancelled staging cleaned up");
		}
		TextFile(target.paths.otfi, "changed resource file");
		const auto stale = Convert(plan);
		Check(!stale.success && !std::filesystem::exists(plan.options.outputMap), "changed resources invalidate analyzed mappings");
		cancel = true;
		auto stopped = Analyze(plan.options, control);
		Check(stopped.cancelled && !stopped.ready(), "analysis cancellation respected");
		std::cout << "PASS cancellation, cleanup and resource-staleness checks\n";
	}
	void TestBoundaries(const Path& root) {
		Set source(root / "source", { { 65535, 100, 1 } }), target(root / "target", { { 101, 100, 1 } });
		Options options { MapFile(root / "map", false, false, 65535), root / "out.otbm", source.paths, target.paths };
		auto plan = Analyze(options);
		Check(plan.ready(), "max 16-bit source server ID accepted");
		auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		Check(InspectMap(result.outputMap, *plan.target).usedIds.contains(101), "escaped 65535 ID rewritten correctly");
		auto second = Convert(plan);
		Check(!second.success, "existing output protected");
		auto malformed = IO::ReadFile(options.sourceMap, 100000);
		malformed.pop_back();
		Write(options.sourceMap, malformed);
		auto invalid = Analyze(options);
		Check(!invalid.error.empty(), "truncated OTBM rejected");
		std::cout << "PASS 16-bit limits, escaped bytes, output protection and truncated OTBM\n";
	}
	void TestAutomaticBatch(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		// A genuine retexture: preserve the SPR encoding and change the green
		// channel of every pixel in the ground's only frame.
		auto spr = IO::ReadFile(target.paths.spr, 100000);
		IO::Cursor index { spr };
		index.skip(8);
		const auto first = index.u32();
		for (size_t p = 0; p < 1024; ++p) {
			spr.at(first + 5 + 4 + p * 3 + 1) = 190;
		}
		Write(target.paths.spr, spr);
		auto plan = FixturePlan(root / "map", source, target, false, false);
		Check(plan.error.empty() && !plan.ready(), "retexture is not exact or safe by default");
		ResolveAutomatically(plan, AutomaticPolicy::Safe);
		Check(!plan.ready(), "safe automatic policy does not silently accept approximations");
		ResolveAutomatically(plan, AutomaticPolicy::CompatibleApproximation);
		Check(plan.ready() && plan.rows.front().decision == Decision::Approximate, "one automatic operation resolves every compatible occurrence without per-ID clicks");
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		const auto bytes = IO::ReadFile(result.jsonReport, 1000000);
		const std::string json(bytes.begin(), bytes.end());
		Check(json.find("\"completeCompatible\": false") != std::string::npos && json.find("\"requiresVisualTesting\": true") != std::string::npos, "approximate output is never reported as fully compatible");
		Set ambiguous(root / "ambiguous", { { 100, 100, 1 }, { 505, 100, 1 } });
		auto tied = FixturePlan(root / "tied-map", source, ambiguous, false, false);
		ResolveAutomatically(tied, AutomaticPolicy::CompatibleApproximation);
		Check(!tied.ready() && tied.rows.front().decision == Decision::Ambiguous, "batch automation does not choose an equal-ID tie from different catalogs");
		std::string error;
		Check(Resolve(tied, 100, 505, error), "reviewed selection accepted");
		ResolveAutomatically(tied, AutomaticPolicy::Safe);
		Check(tied.rows.front().targetId == 505 && tied.rows.front().decision == Decision::Reviewed, "automatic reassessment retains fingerprint-bound user decisions");
		for (size_t p = 0; p < 1024; ++p) {
			spr.at(first + 5 + 4 + p * 3 + 1) = 40;
		}
		Write(target.paths.spr, spr);
		auto poor = FixturePlan(root / "poor-map", source, target, false, false);
		ResolveAutomatically(poor, AutomaticPolicy::CompatibleApproximation);
		Check(!poor.ready(), "visually unrelated highest score cannot auto-convert the map");
		std::cout << "PASS automatic batch, opt-in approximations, tie rejection, retained review and honest reports\n";
	}
	void TestNoCascadeAndProtection(const Path& root) {
		Set source(root / "source", Source), target(root / "target", { { 101, 100, 1 }, { 500, 101, 2 }, { 502, 102, 13 }, { 503, 103, 7 }, { 504, 104, 0, 1u << 7 } });
		auto plan = FixturePlan(root / "map", source, target);
		Check(plan.ready(), "cascade fixture maps every source ID");
		const auto before = FingerprintFile(source.paths.otb).sha256;
		bool rejected = false;
		try {
			WriteReports(plan, source.paths.otb, root / "bad.csv");
		} catch (...) {
			rejected = true;
		}
		Check(rejected && FingerprintFile(source.paths.otb).sha256 == before, "report cannot replace original assets");
		rejected = false;
		try {
			SaveReviewedMappings(plan, plan.options.sourceMap);
		} catch (...) {
			rejected = true;
		}
		Check(rejected, "decision cache cannot replace original map");
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		const auto inventory = InspectMap(result.outputMap, *plan.target);
		Check(inventory.usedIds.at(101).occurrences == 2 && inventory.usedIds.at(500).occurrences == 4, "A to B and B to C uses original IDs exactly once");
		std::cout << "PASS no cascading substitutions and protection against report/cache overwrites\n";
	}
}
namespace {
	void TestContextualPatterns(const Path& root) {
		Set source(root / "source", Source, false, false, false, 2), target(root / "target", Target);
		const auto original = MapFile(root / "map", false, false);
		const auto positioned = root / "map" / "positioned.otbm";
		unsigned tile = 0;
		IO::Walk(
			original, "OTBM", [&](auto& data, size_t, const auto&) {if(data[0]==5){data[1]=static_cast<uint8_t>(tile++);
} }, {}, positioned
		);
		Options options { positioned, root / "map" / "output.otbm", source.paths, target.paths };
		auto plan = Analyze(options);
		Check(plan.error.empty(), plan.error.c_str());
		Check(plan.ready() && plan.rows.front().decision == Decision::ContextualExact, "one source ID resolves to multiple exact target IDs by original DAT pattern");
		Check(TargetIdAt(plan.rows.front(), { 256, 258, 7 }) == 500 && TargetIdAt(plan.rows.front(), { 257, 258, 6 }) == 100, "source X modulo pattern selects the exact target graphics");
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		Check(result.changedItems == 1, "contextual mapping counts only changed occurrences");
		const auto inventory = InspectMap(result.outputMap, *plan.target);
		Check(inventory.usedIds.at(500).occurrences == 1 && inventory.usedIds.at(100).occurrences == 1, "every output position uses its pattern's target ID");
		Check(inventory.structureDigest == plan.inventory.structureDigest, "contextual conversion preserves geometry and attributes");
		std::string error;
		Check(Resolve(plan, 100, 500, error) && plan.rows.front().patternTargets.empty(), "explicit review clears a former contextual rule");
		std::cout << "PASS contextual DAT patterns, all floors, non-cascading IDs and structure validation\n";
	}
}
namespace {
	void TestVisualDraftOutput(const Path& root) {
		Set source(root / "source", { { 100, 100, 1, 3 } }), target(root / "target", { { 500, 100, 0 } });
		auto dat = IO::ReadFile(target.paths.dat, 100000);
		dat.erase(dat.begin() + 12, dat.begin() + 15);
		Write(target.paths.dat, dat);
		auto plan = FixturePlan(root / "map", source, target, false, false);
		Check(!plan.ready(), "safe mode blocks a ground-to-object classification change");
		ResolveAutomatically(plan, AutomaticPolicy::VisualDraft);
		Check(plan.visualDraft && plan.ready() && plan.rows.front().targetId == 500, "explicit appearance priority resolves identical pixels across destination metadata changes");
		Check(!CompatibleForVisualDraft(plan.target->items.at(500), plan.source->items.at(100)), "an ordinary source object cannot overwrite an existing ground");
		plan.options.outputMap.replace_filename("visual_teste.otbm");
		const auto rejected = Convert(plan);
		Check(!rejected.success && rejected.error.find("ground") != std::string::npos, "visual policy alone never authorizes ground loss");
		Check(!std::filesystem::exists(plan.options.outputMap), "rejected ground loss publishes no map");
		plan.options.allowGroundLoss = true;
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		Check(result.structurallyValidated && !result.validated && !result.semanticallyCompatible, "experimental structure is never labeled functionally validated");
		Check(!result.outputGroundlessPositions.empty(), "ground loss positions are available for review");
		Check(result.outputTilesWithoutGround == 2 && InspectMap(result.outputMap, *plan.target).items == 2, "visual trial preserves items and reports the functional change");
		const auto data = IO::ReadFile(result.jsonReport, 1000000);
		const std::string text(data.begin(), data.end());
		Check(text.find("\"mode\": \"visual-test\"") != std::string::npos && text.find("\"completeCompatible\": false") != std::string::npos, "visual draft is never claimed fully server-compatible");
		std::cout << "PASS appearance-first OTBM export, changed blocking, preserved items and explicit trial report\n";
	}
}
namespace {
	void TestTransparentContainerTrial(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		auto sprites = IO::ReadFile(source.paths.spr, 100000);
		IO::Put32(sprites, 12, 0);
		Write(source.paths.spr, sprites);
		auto plan = FixturePlan(root / "map", source, target);
		Check(!plan.ready(), "transparent source container has no invisible exact target");
		ResolveAutomatically(plan, AutomaticPolicy::VisualDraft);
		Check(plan.ready(), "visual trial selects a valid visible container when no invisible one exists");
		plan.options.outputMap.replace_filename("transparent_teste.otbm");
		const auto row = std::find_if(plan.rows.begin(), plan.rows.end(), [](const auto& r) { return r.sourceId == 101; });
		Check(row != plan.rows.end() && row->targetId == 501 && row->reason.find("Container transparente") != std::string::npos, "visibility change is explicit and container type retained");
		const auto result = Convert(plan);
		Check(result.success, result.error.c_str());
		Check(InspectMap(result.outputMap, *plan.target).nestedItems == 4, "container fallback preserves all nested contents");
		std::cout << "PASS transparent-container trial fallback with nested contents preserved\n";
	}
	void TestCreateOnlyPublication(const Path& root) {
		std::filesystem::create_directories(root);
		const auto output = root / IO::FromUtf8("saída-com-espaço.otbm");
		const auto sidecar = root / "sidecar.xml";
		{
			FileSaveTransaction transaction(FileSaveTransaction::PublicationMode::CreateOnly);
			TextFile(transaction.Stage(sidecar), "new sidecar");
			TextFile(transaction.Stage(output), "new map");
			TextFile(output, "other writer");
			std::string error;
			Check(!transaction.Commit(error), "late name collision must fail");
			Check(IO::ReadFile(output, 100) == Bytes({ 'o', 't', 'h', 'e', 'r', ' ', 'w', 'r', 'i', 't', 'e', 'r' }), "late writer was replaced");
			Check(!std::filesystem::exists(sidecar), "create-only rollback retained an earlier sidecar");
		}
		const auto dangling = root / "dangling.otbm";
		std::error_code ec;
		std::filesystem::create_symlink(root / "absent", dangling, ec);
		if (!ec) {
			FileSaveTransaction transaction(FileSaveTransaction::PublicationMode::CreateOnly);
			TextFile(transaction.Stage(dangling), "new map");
			std::string error;
			Check(!transaction.Commit(error) && std::filesystem::is_symlink(dangling), "dangling symlink was replaced");
		}
		Set source(root / "source", Source), target(root / "target", Target);
		auto plan = FixturePlan(root / "map", source, target);
		bool injected = false;
		Control control;
		control.progress = [&](int p, const auto&) {
			if (p == 98 && !injected) {
				injected = true;
				TextFile(plan.options.outputMap, "other writer");
			}
		};
		const auto result = Convert(plan, control);
		Check(injected && !result.success, "race after preflight must abort actual conversion");
		Check(!std::filesystem::exists(result.jsonReport) && !std::filesystem::exists(result.csvReport), "failed publication left reports");
		Check(IO::ReadFile(plan.options.outputMap, 100) == IO::ReadFile(output, 100), "conversion race overwrote another writer");
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			Check(entry.path().filename().string().find(".rme-stage") == std::string::npos, "failed publication leaked staging");
		}
		std::cout << "PASS exclusive publication: late writer, Unicode, dangling symlink, sidecar/report rollback\n";
	}
	void TestIncrementalAndBudget(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		Options options { MapFile(root / "map", false, false), root / "new.otbm", source.paths, target.paths };
		Control control;
		auto plan = Analyze(options, control);
		Check(plan.ready() && control.metrics->fullMatchPasses == 1, "initial analysis must match once");
		ResolveAutomatically(plan, AutomaticPolicy::Safe, control);
		ResolveAutomatically(plan, AutomaticPolicy::VisualDraft, control);
		Check(control.metrics->fullMatchPasses == 1, "automatic refinement repeated the full match");
		ResolveAutomatically(plan, AutomaticPolicy::Safe, control);
		Check(control.metrics->fullMatchPasses == 2 && !plan.visualDraft, "policy downgrade must restore safe evidence");
		auto changed = std::make_shared<Catalog>(*plan.target);
		changed->fingerprint += "-reloaded";
		plan.target = changed;
		ResolveAutomatically(plan, AutomaticPolicy::Safe, control);
		Check(control.metrics->fullMatchPasses == 3, "catalog fingerprint change must invalidate reused evidence");
		Control limited;
		limited.memoryLimitBytes = 1;
		auto rejected = Analyze(options, limited);
		Check(!rejected.error.empty() && !rejected.ready(), "memory budget failure must abort cleanly");
		Check(!std::filesystem::exists(options.outputMap), "budget failure published output");
		Check(control.metrics->hashedBytes > 0 && control.metrics->retainedBytes > 0 && !control.metrics->stageMilliseconds.empty(), "job measurements missing");
		std::cout << "PASS single full match, safe policy downgrade, aggregate memory budget and stage metrics\n";
	}
	void TestRepeatedJobs(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		for (unsigned i = 0; i < 30; ++i) {
			auto plan = FixturePlan(root / ("map" + std::to_string(i)), source, target);
			Check(plan.ready(), "repeated analysis failed");
			std::weak_ptr<const Catalog> sourceLife = plan.source, targetLife = plan.target;
			{
				const auto result = Convert(plan);
				Check(result.success, result.error.c_str());
			}
			plan = {};
			Check(sourceLife.expired() && targetLife.expired(), "completed job retained catalogs");
		}
		std::cout << "PASS 30 complete conversion/destruction cycles release both catalogs\n";
	}
	void TestCancellationPhases(const Path& root) {
		Set source(root / "source", Source), target(root / "target", Target);
		Options options { MapFile(root / "map"), root / "new.otbm", source.paths, target.paths };
		for (const auto& phase : { std::string("Lendo DAT"), std::string("Lendo SPR"), std::string("Comparando IDs") }) {
			std::atomic<bool> cancel = false;
			Control control;
			control.cancelled = &cancel;
			control.progress = [&](int, const auto& message) {if(message.find(phase)!=std::string::npos){ cancel = true;
} };
			auto plan = Analyze(options, control);
			Check(cancel && plan.cancelled && !plan.ready(), "analysis stage ignored cancellation");
		}
		auto plan = Analyze(options);
		Check(plan.ready(), "cancellation fixture plan");
		for (const auto& phase : { std::string("Copiando arquivos"), std::string("Gravando mapa"), std::string("Revalidando"), std::string("Publicando") }) {
			std::atomic<bool> cancel = false;
			Control control;
			control.cancelled = &cancel;
			control.progress = [&](int, const auto& message) {if(message.find(phase)!=std::string::npos){ cancel = true;
} };
			const auto result = Convert(plan, control);
			Check(cancel && result.cancelled && !result.success && !std::filesystem::exists(options.outputMap), "conversion stage ignored cancellation or published output");
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
			Check(entry.path().filename().string().find(".rme-stage") == std::string::npos, "cancelled phase leaked a staged file");
		}
		std::cout << "PASS cancellation during DAT, SPR, matching, sidecars, map writing, revalidation and publication\n";
	}
	void StressMapSizes(const Path& root) {
		if (!std::getenv("NEXAMAP_MIGRATION_STRESS")) {
			return;
		}
		Set source(root / "source", { { 100, 100, 1 } }), target(root / "target", { { 500, 100, 1 } });
		std::filesystem::create_directories(root / "maps");
		for (const uint64_t count : { 10000ull, 100000ull, 1000000ull }) {
			// Stream the large fixture rather than retaining one million heap nodes.
			std::ofstream file(root / "maps" / "large.otbm", std::ios::binary);
			const auto emit = [&](const Bytes& data) {
				file.put(char(0xFE));
				for (auto byte : data) {
					if (byte >= 0xFD) {
						file.put(char(0xFD));
					}
					file.put(char(byte));
				}
			};
			Bytes header { 0 };
			U32(header, 2);
			U16(header, 65535);
			U16(header, 65535);
			U32(header, 3);
			U32(header, 0);
			file.write("OTBM", 4);
			emit(header);
			emit({ 2 });
			uint64_t remaining = count;
			for (unsigned areaY = 0; remaining; areaY += 256) {
				for (unsigned areaX = 0; areaX < 65536 && remaining; areaX += 256) {
					Bytes area { 4 };
					U16(area, uint16_t(areaX));
					U16(area, uint16_t(areaY));
					area.push_back(7);
					emit(area);
					for (unsigned y = 0; y < 256 && remaining; ++y) {
						for (unsigned x = 0; x < 256 && remaining; ++x) {
							Bytes tile { 5, uint8_t(x), uint8_t(y), 9 };
							U16(tile, 100);
							emit(tile);
							file.put(char(0xFF));
							--remaining;
						}
					}
					file.put(char(0xFF));
				}
			}
			file.put(char(0xFF));
			file.put(char(0xFF));
			file.close();
			Control control;
			auto started = std::chrono::steady_clock::now();
			auto plan = Analyze({ root / "maps" / "large.otbm", root / ("out" + std::to_string(count) + ".otbm"), source.paths, target.paths }, control);
			Check(plan.ready() && plan.inventory.tiles == count, "stress inventory count");
			const auto result = Convert(plan, control);
			Check(result.success, result.error.c_str());
			std::cout << "BENCH tiles=" << count << " ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
					  << " peakRSS=" << control.metrics->peakResidentBytes << " hashedBytes=" << control.metrics->hashedBytes << '\n';
		}
	}
	void BenchmarkMatching() {
		if (!std::getenv("NEXAMAP_MIGRATION_STRESS")) {
			return;
		}
		for (const unsigned targetCount : { 10000u, 50000u }) {
			Catalog target, source;
			auto a = std::make_shared<Visual>(), b = std::make_shared<Visual>();
			a->valid = b->valid = a->visiblePixels = b->visiblePixels = true;
			a->allVariantsVerified = b->allVariantsVerified = true;
			a->completePixels = "source";
			b->completePixels = "target";
			a->preview = b->preview = { 80, 90, 100, 255 };
			a->histogram[0] = b->histogram[0] = 1;
			a->variantFeatures.resize(1);
			b->variantFeatures.resize(1);
			a->variantFeatures[0].fill(100);
			b->variantFeatures[0].fill(100);
			for (unsigned id = 1; id <= targetCount; ++id) {
				auto& item = target.items[uint16_t(id)];
				item.serverId = item.clientId = uint16_t(id);
				item.roles = id % 4096;
				item.visual = b;
			}
			for (const unsigned sourceCount : { 100u, 1000u, 5000u }) {
				Inventory inventory;
				source.items.clear();
				for (unsigned id = 1; id <= sourceCount; ++id) {
					auto& item = source.items[uint16_t(id)];
					item.serverId = item.clientId = uint16_t(id);
					item.roles = id % 4096;
					item.visual = a;
					inventory.usedIds[uint16_t(id)].occurrences = 1;
				}
				Control control;
				const auto start = std::chrono::steady_clock::now();
				const auto rows = Match(inventory, source, target, control);
				Check(rows.size() == sourceCount && std::all_of(rows.begin(), rows.end(), [](const auto& row) { return !row.resolved(); }), "indexed benchmark unexpectedly resolved ties");
				Check(control.metrics->candidateComparisons < uint64_t(sourceCount) * targetCount / 100, "functional index did not reduce candidate work");
				std::cout << "BENCH sourceIDs=" << sourceCount << " targetIDs=" << targetCount << " ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
						  << " compared=" << control.metrics->candidateComparisons << " fullScanPairs=" << uint64_t(sourceCount) * targetCount << '\n';
			}
		}
	}
}
int main() {
	const auto root = std::filesystem::temp_directory_path() / IO::FromUtf8("nexamap-migration-São-João-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try {
		Check(Sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty known vector");
		Check(Sha256({ 'a', 'b', 'c' }) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc known vector");
		TestWholeMap(root / "whole");
		TestAmbiguityAndRoles(root / "ambiguity");
		TestAnimationAndMissing(root / "animation");
		TestCancellationAndStale(root / "cancellation");
		TestBoundaries(root / "boundaries");
		TestAutomaticBatch(root / "automatic");
		TestNoCascadeAndProtection(root / "cascade");
		TestContextualPatterns(root / "patterns");
		TestVisualDraftOutput(root / "visual-draft");
		TestTransparentContainerTrial(root / "transparent-container");
		TestCreateOnlyPublication(root / "publication");
		TestIncrementalAndBudget(root / "incremental");
		TestRepeatedJobs(root / "repeated");
		TestCancellationPhases(root / "cancel-phases");
		StressMapSizes(root / "stress");
		BenchmarkMatching();
		std::filesystem::remove_all(root);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "FAIL " << error.what() << " fixture=" << root << '\n';
		return 1;
	}
}
