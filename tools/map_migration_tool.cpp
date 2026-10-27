#include "map_migration/map_migration.h"
#include <iostream>
#include <map>

int main(int argc, char** argv) {
	using namespace MapMigration;
	try {
		std::map<std::string, std::string> args;
		bool convert = false, visualDraft = false;
		for (int i = 1; i < argc; ++i) {
			const std::string option = argv[i];
			if (option == "--visual-draft") {
				visualDraft = true;
				continue;
			}
			if (option == "--convert") {
				convert = true;
				continue;
			}
			if (i + 1 >= argc) {
				throw std::runtime_error("Missing value: " + option);
			}
			args[option] = argv[++i];
		}
		const auto path = [&](const char* name, bool required = true) {
			if (!args.contains(name)) {
				if (required) {
					throw std::runtime_error(std::string("Missing ") + name);
				}
				return Path {};
			}
			const auto& text = args.at(name);
			return Path(std::u8string(text.begin(), text.end()));
		};
		Options options;
		options.sourceMap = path("--map");
		options.outputMap = path("--output");
		options.source = { path("--source-otb"), path("--source-xml", false), path("--source-dat"), path("--source-spr"), path("--source-otfi", false) };
		options.target = { path("--target-otb"), path("--target-xml", false), path("--target-dat"), path("--target-spr"), path("--target-otfi", false) };
		Control control;
		int last = -1;
		control.progress = [&](int p, const std::string& text) {if(p!=last){std::cerr<<p<<"% "<<text<<'\n';last=p;} };
		auto plan = Analyze(options, control);
		if (args.contains("--mappings")) {
			LoadReviewedMappings(plan, path("--mappings"));
		}
		if (visualDraft && plan.error.empty()) {
			ResolveAutomatically(plan, AutomaticPolicy::VisualDraft, control);
		}
		std::cout << "tiles=" << plan.inventory.tiles << " items=" << plan.inventory.items << " distinct_ids=" << plan.rows.size() << " unresolved_ids=" << plan.unresolvedIds() << " unresolved_occurrences=" << plan.unresolvedOccurrences() << '\n';
		std::map<std::string, uint64_t> decisions;
		for (const auto& row : plan.rows) {
			++decisions[DecisionName(row.decision)];
		}
		for (const auto& [name, count] : decisions) {
			std::cout << name << ": " << count << '\n';
		}
		Path json = options.outputMap;
		json.replace_extension(".analysis.json");
		Path csv = options.outputMap;
		csv.replace_extension(".analysis.csv");
		std::filesystem::create_directories(std::filesystem::absolute(json).parent_path());
		WriteReports(plan, json, csv);
		if (!plan.error.empty()) {
			std::cerr << plan.error << '\n';
			return 1;
		}
		if (convert) {
			const auto result = Convert(plan, control);
			if (!result.success) {
				std::cerr << result.error << '\n';
				return 3;
			}
			std::cout << "Converted, reopened and validated. Changed items: " << result.changedItems << '\n';
		}
		return plan.unresolvedIds() ? 2 : 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
