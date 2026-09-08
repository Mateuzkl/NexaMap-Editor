//////////////////////////////////////////////////////////////////////
// Per-workspace server content discovery and source metadata.
//////////////////////////////////////////////////////////////////////

#include "server_content_index.h"

#include "ext/pugixml.hpp"
#include "lua_source_scanner.h"
#include "source_text_utils.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {
	std::string PathUtf8(const std::filesystem::path& path) {
		return SourceText::PathUtf8(path);
	}

	std::filesystem::path PathFromUtf8(std::string_view value) {
		const auto* begin = reinterpret_cast<const char8_t*>(value.data());
		return std::filesystem::path(std::u8string(begin, begin + value.size()));
	}

	std::string LowerAscii(std::string value) {
		return SourceText::AsciiLower(value);
	}

	std::filesystem::path Normalize(const std::filesystem::path& value) {
		if (value.empty()) {
			return {};
		}
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(value, error);
		if (error) {
			return value.lexically_normal();
		}
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
		return error ? absolute.lexically_normal() : canonical;
	}

	bool IsRegularFile(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool IsDirectory(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_directory(path, error) && !error;
	}

	bool IsSafeRelativePath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute()) {
			return false;
		}
		return std::none_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
			return component == "..";
		});
	}

	std::string ComparablePath(const std::filesystem::path& path) {
		// Indexed paths are normalized when discovered. Comparisons must remain a
		// pure string operation; weakly_canonical here would touch the filesystem
		// for every sort comparison on servers with thousands of definitions.
		std::string value = PathUtf8(path.lexically_normal());
#ifdef _WIN32
		value = LowerAscii(std::move(value));
#endif
		return value;
	}

	std::size_t LineAtOffset(std::string_view bytes, std::ptrdiff_t offset) {
		if (offset <= 0) {
			return 1;
		}
		const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(offset), bytes.size());
		return 1 + static_cast<std::size_t>(std::count(bytes.begin(), bytes.begin() + end, '\n'));
	}

	std::string Diagnostic(const std::filesystem::path& path, const std::string& message) {
		return PathUtf8(path) + ": " + message;
	}

	struct CandidateFile {
		ServerContentKind kind = ServerContentKind::Monster;
		std::filesystem::path path;
	};

	struct MonsterRegistration {
		std::string name;
		std::filesystem::path declarationPath;
		std::filesystem::path registrationPath;
		ResourceFingerprint registrationFingerprint;
		std::size_t line = 0;
	};

	struct ParsedFile {
		ServerContentKind kind = ServerContentKind::Monster;
		ResourceFingerprint fingerprint;
		std::vector<ServerContentSource> sources;
		std::vector<MonsterRegistration> monsterRegistrations;
		std::vector<std::string> diagnostics;
		bool monsterRegistry = false;
		bool skipped = false;
	};

	struct QueueEntry {
		std::filesystem::path path;
		std::size_t depth = 0;
	};

	bool IsSkippedDirectory(const std::filesystem::path& path) {
		static const std::set<std::string> ignored {
			".git",
			".hg",
			".svn",
			".vs",
			"build",
			"builds",
			"node_modules",
			"vcpkg_installed",
		};
		const std::string name = LowerAscii(PathUtf8(path.filename()));
		return ignored.contains(name) || name.starts_with("build-") || name.starts_with("build_");
	}

	std::vector<std::filesystem::directory_entry> SortedEntries(const std::filesystem::path& directory) {
		std::vector<std::filesystem::directory_entry> entries;
		std::error_code error;
		for (std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
			 iterator != end && !error;
			 iterator.increment(error)) {
			entries.push_back(*iterator);
		}
		std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
			const std::string leftName = LowerAscii(PathUtf8(left.path().filename()));
			const std::string rightName = LowerAscii(PathUtf8(right.path().filename()));
			return leftName != rightName ? leftName < rightName : left.path() < right.path();
		});
		return entries;
	}

	std::string CandidateKey(ServerContentKind kind, const std::filesystem::path& path) {
		return std::to_string(static_cast<unsigned int>(kind)) + ":" + ComparablePath(path);
	}

	void DiscoverCandidates(
		const std::filesystem::path& root,
		ServerContentKind kind,
		const ServerContentScanOptions& options,
		std::vector<CandidateFile>& candidates,
		std::set<std::string>& seen,
		bool& limitReached
	) {
		if (root.empty() || !IsDirectory(root) || limitReached) {
			return;
		}

		std::deque<QueueEntry> queue;
		queue.push_back({ Normalize(root), 0 });
		while (!queue.empty() && !limitReached) {
			const QueueEntry current = queue.front();
			queue.pop_front();
			for (const std::filesystem::directory_entry& entry : SortedEntries(current.path)) {
				std::error_code error;
				if (entry.is_regular_file(error) && !error) {
					const std::string extension = LowerAscii(PathUtf8(entry.path().extension()));
					if (extension != ".xml" && extension != ".lua") {
						continue;
					}
					const std::filesystem::path path = Normalize(entry.path());
					const std::string key = CandidateKey(kind, path);
					if (!seen.insert(key).second) {
						continue;
					}
					if (candidates.size() >= options.maximumFiles) {
						limitReached = true;
						break;
					}
					candidates.push_back({ kind, path });
					continue;
				}
				if (current.depth >= options.maximumDepth || !entry.is_directory(error) || error || entry.is_symlink(error) || IsSkippedDirectory(entry.path())) {
					continue;
				}
				queue.push_back({ entry.path(), current.depth + 1 });
			}
		}
	}

	std::optional<std::string> ReadFile(const ResourceFingerprint& fingerprint, std::size_t maximumBytes, std::string& error) {
		if (!fingerprint.exists) {
			error = "source file no longer exists";
			return std::nullopt;
		}
		if (fingerprint.size > maximumBytes) {
			error = "source exceeds the configured scan size limit";
			return std::nullopt;
		}
		std::ifstream stream(fingerprint.path, std::ios::binary);
		if (!stream.is_open()) {
			error = "source could not be opened";
			return std::nullopt;
		}
		std::string bytes(static_cast<std::size_t>(fingerprint.size), '\0');
		if (!bytes.empty()) {
			stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (!stream) {
				error = "source could not be read completely";
				return std::nullopt;
			}
		}
		return bytes;
	}

	std::optional<std::filesystem::path> ResolveRelatedScript(
		const ServerWorkspace& workspace,
		ServerContentKind kind,
		const std::filesystem::path& declarationPath,
		const std::string& rawPath
	) {
		const std::filesystem::path relative = PathFromUtf8(rawPath);
		if (!IsSafeRelativePath(relative)) {
			return std::nullopt;
		}

		const std::filesystem::path contentRoot = kind == ServerContentKind::Npc ? workspace.npcsDirectory : workspace.spellsDirectory;
		std::vector<std::filesystem::path> candidates {
			declarationPath.parent_path() / relative,
		};
		if (!contentRoot.empty()) {
			candidates.push_back(contentRoot / "scripts" / relative);
			candidates.push_back(contentRoot / relative);
		}
		if (!workspace.rootPath.empty()) {
			candidates.push_back(workspace.rootPath / relative);
		}
		for (const std::filesystem::path& candidate : candidates) {
			if (IsRegularFile(candidate)) {
				return Normalize(candidate);
			}
		}
		return candidates.size() > 1 ? std::optional<std::filesystem::path>(Normalize(candidates[1])) : std::nullopt;
	}

	pugi::xml_node DocumentElement(pugi::xml_document& document) {
		for (pugi::xml_node node = document.first_child(); node; node = node.next_sibling()) {
			if (node.type() == pugi::node_element) {
				return node;
			}
		}
		return {};
	}

	void ParseMonsterXml(const CandidateFile& candidate, const ResourceFingerprint& fingerprint, std::string_view bytes, pugi::xml_node root, ParsedFile& parsed) {
		const std::string rootName = LowerAscii(root.name());
		if (rootName == "monster") {
			const std::string name = root.attribute("name").as_string();
			if (name.empty()) {
				parsed.diagnostics.push_back(Diagnostic(candidate.path, "monster definition has no name attribute"));
				return;
			}
			ServerContentSource source;
			source.kind = ServerContentKind::Monster;
			source.format = ServerContentFormat::Xml;
			source.name = name;
			source.declarationPath = candidate.path;
			source.declarationFingerprint = fingerprint;
			source.declarationLine = LineAtOffset(bytes, root.offset_debug());
			source.registered = true;
			source.declarationExists = true;
			parsed.sources.push_back(std::move(source));
			return;
		}
		if (rootName != "monsters") {
			return;
		}

		parsed.monsterRegistry = true;
		for (pugi::xml_node node = root.first_child(); node; node = node.next_sibling()) {
			if (node.type() != pugi::node_element || LowerAscii(node.name()) != "monster") {
				continue;
			}
			const std::string name = node.attribute("name").as_string();
			const std::string file = node.attribute("file").as_string();
			if (name.empty() || file.empty()) {
				parsed.diagnostics.push_back(Diagnostic(candidate.path, "monster registry entry is missing name or file"));
				continue;
			}
			const std::filesystem::path relative = PathFromUtf8(file);
			if (!IsSafeRelativePath(relative)) {
				parsed.diagnostics.push_back(Diagnostic(candidate.path, "monster registry entry contains an unsafe file path"));
				continue;
			}
			parsed.monsterRegistrations.push_back({
				name,
				Normalize(candidate.path.parent_path() / relative),
				candidate.path,
				fingerprint,
				LineAtOffset(bytes, node.offset_debug()),
			});
		}
	}

	void ParseNpcXml(
		const ServerWorkspace& workspace,
		const CandidateFile& candidate,
		const ResourceFingerprint& fingerprint,
		std::string_view bytes,
		pugi::xml_node root,
		ParsedFile& parsed
	) {
		if (LowerAscii(root.name()) != "npc") {
			return;
		}
		const std::string name = root.attribute("name").as_string();
		if (name.empty()) {
			parsed.diagnostics.push_back(Diagnostic(candidate.path, "NPC definition has no name attribute"));
			return;
		}

		ServerContentSource source;
		source.kind = ServerContentKind::Npc;
		source.format = ServerContentFormat::Xml;
		source.name = name;
		source.declarationPath = candidate.path;
		source.declarationFingerprint = fingerprint;
		source.declarationLine = LineAtOffset(bytes, root.offset_debug());
		source.registered = true;
		source.declarationExists = true;
		const std::string script = root.attribute("script").as_string();
		if (!script.empty()) {
			source.relatedScriptPath = ResolveRelatedScript(workspace, ServerContentKind::Npc, candidate.path, script);
			if (source.relatedScriptPath) {
				source.relatedScriptFingerprint = ResourceFingerprint::Read(*source.relatedScriptPath);
			}
		}
		parsed.sources.push_back(std::move(source));
	}

	void ParseSpellsXml(
		const ServerWorkspace& workspace,
		const CandidateFile& candidate,
		const ResourceFingerprint& fingerprint,
		std::string_view bytes,
		pugi::xml_node root,
		ParsedFile& parsed
	) {
		if (LowerAscii(root.name()) != "spells") {
			return;
		}
		for (pugi::xml_node node = root.first_child(); node; node = node.next_sibling()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			const std::string name = node.attribute("name").as_string();
			if (name.empty()) {
				continue;
			}
			ServerContentSource source;
			source.kind = ServerContentKind::Spell;
			source.format = ServerContentFormat::Xml;
			source.name = name;
			source.subtype = LowerAscii(node.name());
			source.declarationPath = candidate.path;
			source.registrationPath = candidate.path;
			source.declarationFingerprint = fingerprint;
			source.registrationFingerprint = fingerprint;
			source.declarationLine = LineAtOffset(bytes, node.offset_debug());
			source.registrationLine = source.declarationLine;
			source.registered = true;
			source.declarationExists = true;
			const std::string script = node.attribute("script").as_string();
			if (!script.empty()) {
				source.relatedScriptPath = ResolveRelatedScript(workspace, ServerContentKind::Spell, candidate.path, script);
				if (source.relatedScriptPath) {
					source.relatedScriptFingerprint = ResourceFingerprint::Read(*source.relatedScriptPath);
				}
			}
			parsed.sources.push_back(std::move(source));
		}
	}

	void ParseXml(const ServerWorkspace& workspace, const CandidateFile& candidate, const ResourceFingerprint& fingerprint, const std::string& bytes, ParsedFile& parsed) {
		pugi::xml_document document;
		const pugi::xml_parse_result result = document.load_buffer(bytes.data(), bytes.size(), pugi::parse_default | pugi::parse_comments, pugi::encoding_auto);
		if (!result) {
			parsed.diagnostics.push_back(Diagnostic(
				candidate.path,
				"XML parse error at byte " + std::to_string(result.offset) + ": " + result.description()
			));
			return;
		}
		pugi::xml_node root = DocumentElement(document);
		if (!root) {
			return;
		}
		switch (candidate.kind) {
			case ServerContentKind::Monster:
				ParseMonsterXml(candidate, fingerprint, bytes, root, parsed);
				break;
			case ServerContentKind::Npc:
				ParseNpcXml(workspace, candidate, fingerprint, bytes, root, parsed);
				break;
			case ServerContentKind::Spell:
				ParseSpellsXml(workspace, candidate, fingerprint, bytes, root, parsed);
				break;
		}
	}

	enum class LuaTokenKind : uint8_t {
		Identifier,
		String,
		Symbol,
	};

	struct LuaToken {
		LuaTokenKind kind = LuaTokenKind::Symbol;
		std::string value;
		std::size_t line = 1;
	};

	bool IsIdentifierStart(unsigned char character) {
		return character == '_' || std::isalpha(character) != 0;
	}

	bool IsIdentifierBody(unsigned char character) {
		return character == '_' || std::isalnum(character) != 0;
	}

	std::optional<std::pair<std::size_t, std::size_t>> LongBracket(std::string_view source, std::size_t offset) {
		const auto bracket = LuaSource::OpenLongBracket(source, offset);
		if (!bracket) {
			return std::nullopt;
		}
		return std::pair<std::size_t, std::size_t> { bracket->equals, bracket->contentBegin };
	}

	std::size_t FindLongBracketEnd(std::string_view source, std::size_t contentStart, std::size_t equals) {
		return LuaSource::LongBracketEnd(source, contentStart, equals);
	}

	std::vector<LuaToken> TokenizeLua(std::string_view source) {
		std::vector<LuaToken> tokens;
		std::size_t line = 1;
		std::size_t cursor = 0;
		while (cursor < source.size()) {
			const unsigned char character = static_cast<unsigned char>(source[cursor]);
			if (std::isspace(character) != 0) {
				line += source[cursor] == '\n' ? 1 : 0;
				++cursor;
				continue;
			}
			if (source[cursor] == '-' && cursor + 1 < source.size() && source[cursor + 1] == '-') {
				cursor += 2;
				if (const auto bracket = LongBracket(source, cursor)) {
					const std::size_t end = FindLongBracketEnd(source, bracket->second, bracket->first);
					line += static_cast<std::size_t>(std::count(source.begin() + cursor, source.begin() + end, '\n'));
					cursor = end;
				} else {
					while (cursor < source.size() && source[cursor] != '\n') {
						++cursor;
					}
				}
				continue;
			}
			if (source[cursor] == '\'' || source[cursor] == '"') {
				const char quote = source[cursor++];
				const std::size_t tokenLine = line;
				std::string value;
				while (cursor < source.size()) {
					char current = source[cursor++];
					if (current == quote) {
						break;
					}
					if (current == '\\' && cursor < source.size()) {
						const char escaped = source[cursor++];
						switch (escaped) {
							case 'n':
								value.push_back('\n');
								break;
							case 'r':
								value.push_back('\r');
								break;
							case 't':
								value.push_back('\t');
								break;
							default:
								value.push_back(escaped);
								break;
						}
						continue;
					}
					line += current == '\n' ? 1 : 0;
					value.push_back(current);
				}
				tokens.push_back({ LuaTokenKind::String, std::move(value), tokenLine });
				continue;
			}
			if (const auto bracket = LongBracket(source, cursor)) {
				const std::size_t tokenLine = line;
				const std::size_t end = FindLongBracketEnd(source, bracket->second, bracket->first);
				const std::size_t closingSize = bracket->first + 2;
				const std::size_t contentEnd = end >= closingSize ? end - closingSize : bracket->second;
				std::string value(source.substr(bracket->second, contentEnd - bracket->second));
				line += static_cast<std::size_t>(std::count(source.begin() + cursor, source.begin() + end, '\n'));
				cursor = end;
				tokens.push_back({ LuaTokenKind::String, std::move(value), tokenLine });
				continue;
			}
			if (IsIdentifierStart(character)) {
				const std::size_t start = cursor++;
				while (cursor < source.size() && IsIdentifierBody(static_cast<unsigned char>(source[cursor]))) {
					++cursor;
				}
				tokens.push_back({ LuaTokenKind::Identifier, std::string(source.substr(start, cursor - start)), line });
				continue;
			}
			tokens.push_back({ LuaTokenKind::Symbol, std::string(1, source[cursor]), line });
			++cursor;
		}
		return tokens;
	}

	bool IsToken(const std::vector<LuaToken>& tokens, std::size_t index, std::string_view value) {
		return index < tokens.size() && tokens[index].value == value;
	}

	std::optional<std::string> LiteralValue(const LuaToken& token, const std::unordered_map<std::string, std::string>& stringVariables) {
		if (token.kind == LuaTokenKind::String) {
			return token.value;
		}
		if (token.kind == LuaTokenKind::Identifier) {
			const auto found = stringVariables.find(token.value);
			if (found != stringVariables.end()) {
				return found->second;
			}
		}
		return std::nullopt;
	}

	std::unordered_map<std::string, std::string> CollectStringVariables(const std::vector<LuaToken>& tokens) {
		std::unordered_map<std::string, std::string> variables;
		for (std::size_t index = 0; index + 2 < tokens.size(); ++index) {
			std::size_t nameIndex = index;
			if (IsToken(tokens, nameIndex, "local")) {
				++nameIndex;
			}
			if (nameIndex + 2 >= tokens.size() || tokens[nameIndex].kind != LuaTokenKind::Identifier || !IsToken(tokens, nameIndex + 1, "=") || tokens[nameIndex + 2].kind != LuaTokenKind::String) {
				continue;
			}
			variables[tokens[nameIndex].value] = tokens[nameIndex + 2].value;
			index = nameIndex + 2;
		}
		return variables;
	}

	struct LuaCreation {
		std::string variable;
		std::string name;
		std::string subtype;
		std::size_t tokenIndex = 0;
		std::size_t line = 0;
	};

	std::optional<std::size_t> AssignmentStart(const std::vector<LuaToken>& tokens, std::size_t index) {
		if (IsToken(tokens, index, "local")) {
			++index;
		}
		if (index + 2 >= tokens.size() || tokens[index].kind != LuaTokenKind::Identifier || !IsToken(tokens, index + 1, "=")) {
			return std::nullopt;
		}
		return index;
	}

	std::vector<LuaCreation> FindCreatureCreations(
		const std::vector<LuaToken>& tokens,
		std::string_view factory,
		const std::unordered_map<std::string, std::string>& variables
	) {
		std::vector<LuaCreation> creations;
		for (std::size_t index = 0; index < tokens.size(); ++index) {
			const auto assignment = AssignmentStart(tokens, index);
			if (!assignment || *assignment + 6 >= tokens.size()) {
				continue;
			}
			const std::size_t start = *assignment;
			if (!IsToken(tokens, start + 2, "Game") || !IsToken(tokens, start + 3, ".") || !IsToken(tokens, start + 4, factory) || !IsToken(tokens, start + 5, "(")) {
				continue;
			}
			const std::optional<std::string> name = LiteralValue(tokens[start + 6], variables);
			if (!name || name->empty()) {
				continue;
			}
			creations.push_back({ tokens[start].value, *name, {}, start, tokens[start].line });
			index = start + 6;
		}
		return creations;
	}

	std::vector<LuaCreation> FindSpellCreations(
		const std::vector<LuaToken>& tokens,
		const std::unordered_map<std::string, std::string>& variables
	) {
		std::vector<LuaCreation> creations;
		for (std::size_t index = 0; index < tokens.size(); ++index) {
			const auto assignment = AssignmentStart(tokens, index);
			if (!assignment || *assignment + 4 >= tokens.size()) {
				continue;
			}
			const std::size_t start = *assignment;
			if (!IsToken(tokens, start + 2, "Spell") || !IsToken(tokens, start + 3, "(")) {
				continue;
			}
			std::string subtype;
			if (const std::optional<std::string> literal = LiteralValue(tokens[start + 4], variables)) {
				subtype = *literal;
			} else if (tokens[start + 4].kind == LuaTokenKind::Identifier) {
				subtype = tokens[start + 4].value;
			}
			creations.push_back({ tokens[start].value, {}, std::move(subtype), start, tokens[start].line });
			index = start + 4;
		}
		return creations;
	}

	std::size_t CreationEnd(const std::vector<LuaCreation>& creations, std::size_t creationIndex, std::size_t tokenCount) {
		const LuaCreation& creation = creations[creationIndex];
		for (std::size_t index = creationIndex + 1; index < creations.size(); ++index) {
			if (creations[index].variable == creation.variable) {
				return creations[index].tokenIndex;
			}
		}
		return tokenCount;
	}

	std::optional<std::size_t> FindMethodCall(const std::vector<LuaToken>& tokens, const LuaCreation& creation, std::size_t end, std::string_view method) {
		for (std::size_t index = creation.tokenIndex; index + 3 < end; ++index) {
			if (IsToken(tokens, index, creation.variable) && IsToken(tokens, index + 1, ":") && IsToken(tokens, index + 2, method) && IsToken(tokens, index + 3, "(")) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<std::string> MethodLiteral(
		const std::vector<LuaToken>& tokens,
		const LuaCreation& creation,
		std::size_t end,
		std::string_view method,
		const std::unordered_map<std::string, std::string>& variables
	) {
		for (std::size_t index = creation.tokenIndex; index + 4 < end; ++index) {
			if (!IsToken(tokens, index, creation.variable) || !IsToken(tokens, index + 1, ":") || !IsToken(tokens, index + 2, method) || !IsToken(tokens, index + 3, "(")) {
				continue;
			}
			return LiteralValue(tokens[index + 4], variables);
		}
		return std::nullopt;
	}

	void ParseLua(const CandidateFile& candidate, const ResourceFingerprint& fingerprint, const std::string& bytes, ParsedFile& parsed) {
		const std::vector<LuaToken> tokens = TokenizeLua(bytes);
		const auto variables = CollectStringVariables(tokens);
		if (candidate.kind == ServerContentKind::Monster || candidate.kind == ServerContentKind::Npc) {
			const std::string_view factory = candidate.kind == ServerContentKind::Monster ? "createMonsterType" : "createNpcType";
			const std::vector<LuaCreation> creations = FindCreatureCreations(tokens, factory, variables);
			for (std::size_t index = 0; index < creations.size(); ++index) {
				const LuaCreation& creation = creations[index];
				const std::optional<std::size_t> registration = candidate.kind == ServerContentKind::Npc
					? std::optional<std::size_t>(creation.tokenIndex)
					: FindMethodCall(tokens, creation, CreationEnd(creations, index, tokens.size()), "register");
				ServerContentSource source;
				source.kind = candidate.kind;
				source.format = ServerContentFormat::Lua;
				source.name = creation.name;
				source.declarationPath = candidate.path;
				source.declarationFingerprint = fingerprint;
				source.declarationLine = creation.line;
				source.registered = registration.has_value();
				source.declarationExists = true;
				if (registration) {
					source.registrationPath = candidate.path;
					source.registrationFingerprint = fingerprint;
					source.registrationLine = tokens[*registration].line;
				}
				parsed.sources.push_back(std::move(source));
			}
			return;
		}

		const std::vector<LuaCreation> creations = FindSpellCreations(tokens, variables);
		for (std::size_t index = 0; index < creations.size(); ++index) {
			const LuaCreation& creation = creations[index];
			const std::size_t end = CreationEnd(creations, index, tokens.size());
			const std::optional<std::string> name = MethodLiteral(tokens, creation, end, "name", variables);
			if (!name || name->empty()) {
				continue;
			}
			ServerContentSource source;
			source.kind = ServerContentKind::Spell;
			source.format = ServerContentFormat::Lua;
			source.name = *name;
			source.subtype = creation.subtype;
			source.declarationPath = candidate.path;
			source.declarationFingerprint = fingerprint;
			source.declarationLine = creation.line;
			const std::optional<std::size_t> registration = FindMethodCall(tokens, creation, end, "register");
			source.registered = registration.has_value();
			source.declarationExists = true;
			if (registration) {
				source.registrationPath = candidate.path;
				source.registrationFingerprint = fingerprint;
				source.registrationLine = tokens[*registration].line;
			}
			parsed.sources.push_back(std::move(source));
		}
	}

	ParsedFile ParseCandidate(
		const ServerWorkspace& workspace,
		const CandidateFile& candidate,
		const ResourceFingerprint& fingerprint,
		const ServerContentScanOptions& options
	) {
		ParsedFile parsed;
		parsed.kind = candidate.kind;
		parsed.fingerprint = fingerprint;
		std::string error;
		const std::optional<std::string> bytes = ReadFile(parsed.fingerprint, options.maximumFileBytes, error);
		if (!bytes) {
			parsed.skipped = true;
			parsed.diagnostics.push_back(Diagnostic(candidate.path, error));
			return parsed;
		}

		const std::string extension = LowerAscii(PathUtf8(candidate.path.extension()));
		if (extension == ".xml") {
			ParseXml(workspace, candidate, parsed.fingerprint, *bytes, parsed);
		} else if (extension == ".lua") {
			ParseLua(candidate, parsed.fingerprint, *bytes, parsed);
		}
		return parsed;
	}

	bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root) {
		if (path.empty() || root.empty()) {
			return false;
		}
		const std::filesystem::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
		return !relative.empty() && !relative.is_absolute()
			&& std::none_of(relative.begin(), relative.end(), [](const std::filesystem::path& component) { return component == ".."; });
	}

	std::vector<ServerContentKind> KindsForPath(const ServerWorkspace& workspace, const std::filesystem::path& path) {
		std::vector<ServerContentKind> result;
		if (IsWithin(path, workspace.monstersDirectory)) {
			result.push_back(ServerContentKind::Monster);
		}
		if (IsWithin(path, workspace.npcsDirectory)) {
			result.push_back(ServerContentKind::Npc);
		}
		if (IsWithin(path, workspace.spellsDirectory)) {
			result.push_back(ServerContentKind::Spell);
		}
		return result;
	}

	std::string LookupKey(ServerContentKind kind, std::string_view name) {
		std::string key;
		key.reserve(name.size() + 2);
		key.push_back(static_cast<char>('0' + static_cast<unsigned int>(kind)));
		key.push_back(':');
		key.append(name);
		return key;
	}

	void AddAlias(ServerContentSource& source, const std::string& alias) {
		if (alias.empty() || alias == source.name || std::find(source.aliases.begin(), source.aliases.end(), alias) != source.aliases.end()) {
			return;
		}
		source.aliases.push_back(alias);
	}

	ServerContentCategoryCapabilities& CapabilitiesFor(ServerContentCapabilities& capabilities, ServerContentKind kind) {
		switch (kind) {
			case ServerContentKind::Monster:
				return capabilities.monsters;
			case ServerContentKind::Npc:
				return capabilities.npcs;
			case ServerContentKind::Spell:
				return capabilities.spells;
		}
		return capabilities.monsters;
	}
}

struct ServerContentIndex::CacheState {
	std::vector<std::shared_ptr<const ParsedFile>> files;
};

bool ServerContentCategoryCapabilities::hasDefinitions() const {
	return xmlDefinitions || luaDefinitions;
}

bool ServerContentCategoryCapabilities::supports(ServerContentFormat format) const {
	if (format == ServerContentFormat::Xml) {
		return xmlDefinitions;
	}
	if (format == ServerContentFormat::Lua) {
		return luaDefinitions || relatedLua;
	}
	return false;
}

bool ServerContentCategoryCapabilities::isMixed() const {
	return xmlDefinitions && (luaDefinitions || relatedLua);
}

const ServerContentCategoryCapabilities& ServerContentCapabilities::forKind(ServerContentKind kind) const {
	switch (kind) {
		case ServerContentKind::Monster:
			return monsters;
		case ServerContentKind::Npc:
			return npcs;
		case ServerContentKind::Spell:
			return spells;
	}
	return monsters;
}

bool ServerContentCapabilities::isMixed() const {
	const bool anyXml = monsters.xmlDefinitions || npcs.xmlDefinitions || spells.xmlDefinitions;
	const bool anyLua = monsters.luaDefinitions || npcs.luaDefinitions || spells.luaDefinitions || monsters.relatedLua || npcs.relatedLua || spells.relatedLua;
	return anyXml && anyLua;
}

bool ServerContentLookupResult::empty() const {
	return matches.empty();
}

bool ServerContentLookupResult::unique() const {
	return matches.size() == 1;
}

bool ServerContentLookupResult::ambiguous() const {
	return matches.size() > 1;
}

const ServerContentSource* ServerContentLookupResult::value() const {
	return unique() ? matches.front() : nullptr;
}

const ServerContentSource* ServerContentLookupResult::uniqueRegisteredValue() const {
	const ServerContentSource* registered = nullptr;
	for (const ServerContentSource* match : matches) {
		if (!match->registered) {
			continue;
		}
		if (registered) {
			return nullptr;
		}
		registered = match;
	}
	return registered;
}

ServerContentIndex ServerContentIndex::Assemble(
	const ServerWorkspace& workspace,
	std::shared_ptr<CacheState> cacheState,
	ServerContentScanStats stats,
	std::vector<std::string> diagnostics
) {
	ServerContentIndex index;
	index.built = true;
	index.workspaceRoot = workspace.rootPath.lexically_normal();
	index.monstersRoot = workspace.monstersDirectory.lexically_normal();
	index.npcsRoot = workspace.npcsDirectory.lexically_normal();
	index.spellsRoot = workspace.spellsDirectory.lexically_normal();
	index.cache = cacheState;
	index.scanStats = stats;
	index.scanDiagnostics = std::move(diagnostics);

	std::vector<ServerContentSource> assembledSources;
	std::vector<MonsterRegistration> registrations;
	bool hasMonsterRegistry = false;
	for (const auto& cached : cacheState->files) {
		const ParsedFile& file = *cached;
		assembledSources.insert(assembledSources.end(), file.sources.begin(), file.sources.end());
		registrations.insert(registrations.end(), file.monsterRegistrations.begin(), file.monsterRegistrations.end());
		index.scanDiagnostics.insert(index.scanDiagnostics.end(), file.diagnostics.begin(), file.diagnostics.end());
		hasMonsterRegistry = hasMonsterRegistry || file.monsterRegistry;
		index.scanStats.filesSkipped += file.skipped ? 1 : 0;
	}

	if (hasMonsterRegistry) {
		for (ServerContentSource& source : assembledSources) {
			if (source.kind == ServerContentKind::Monster && source.format == ServerContentFormat::Xml) {
				source.registered = false;
			}
		}
	}
	std::unordered_map<std::string, std::vector<std::size_t>> monsterSourcesByPath;
	for (std::size_t position = 0; position < assembledSources.size(); ++position) {
		if (assembledSources[position].kind == ServerContentKind::Monster) {
			monsterSourcesByPath[ComparablePath(assembledSources[position].declarationPath)].push_back(position);
		}
	}
	for (const MonsterRegistration& registration : registrations) {
		auto& matchedSources = monsterSourcesByPath[ComparablePath(registration.declarationPath)];
		for (const std::size_t sourceIndex : matchedSources) {
			ServerContentSource& source = assembledSources[sourceIndex];
			source.registered = true;
			source.registrationPath = registration.registrationPath;
			source.registrationFingerprint = registration.registrationFingerprint;
			source.registrationLine = registration.line;
			AddAlias(source, registration.name);
		}
		if (matchedSources.empty()) {
			ServerContentSource source;
			source.kind = ServerContentKind::Monster;
			const std::string extension = LowerAscii(PathUtf8(registration.declarationPath.extension()));
			source.format = extension == ".lua" ? ServerContentFormat::Lua : (extension == ".xml" ? ServerContentFormat::Xml : ServerContentFormat::Unknown);
			source.name = registration.name;
			source.declarationPath = registration.declarationPath;
			source.registrationPath = registration.registrationPath;
			source.declarationFingerprint = ResourceFingerprint::Read(registration.declarationPath);
			source.registrationFingerprint = registration.registrationFingerprint;
			source.registrationLine = registration.line;
			source.registered = true;
			source.declarationExists = source.declarationFingerprint.exists;
			assembledSources.push_back(std::move(source));
			matchedSources.push_back(assembledSources.size() - 1);
		}
	}

	std::unordered_map<std::string, ResourceFingerprint> currentFingerprints;
	currentFingerprints.reserve(cacheState->files.size());
	for (const auto& cached : cacheState->files) {
		currentFingerprints.emplace(ComparablePath(cached->fingerprint.path), cached->fingerprint);
	}
	const auto currentFingerprint = [&](const std::filesystem::path& path) -> ResourceFingerprint {
		const std::string key = ComparablePath(path);
		if (const auto found = currentFingerprints.find(key); found != currentFingerprints.end()) {
			return found->second;
		}
		ResourceFingerprint fingerprint = ResourceFingerprint::Read(path);
		currentFingerprints.emplace(key, fingerprint);
		return fingerprint;
	};
	for (ServerContentSource& source : assembledSources) {
		source.serverType = workspace.serverType;
		source.declarationFingerprint = currentFingerprint(source.declarationPath);
		source.declarationExists = source.declarationFingerprint.exists;
		if (source.registrationPath) {
			source.registrationFingerprint = currentFingerprint(*source.registrationPath);
		}
		if (source.relatedScriptPath) {
			source.relatedScriptFingerprint = currentFingerprint(*source.relatedScriptPath);
		}

		ServerContentCategoryCapabilities& capabilities = CapabilitiesFor(index.detectedCapabilities, source.kind);
		if (source.declarationExists && source.format == ServerContentFormat::Xml) {
			capabilities.xmlDefinitions = true;
		} else if (source.declarationExists && source.format == ServerContentFormat::Lua) {
			capabilities.luaDefinitions = true;
		}
		if (source.relatedScriptFingerprint && source.relatedScriptFingerprint->exists) {
			capabilities.relatedLua = true;
		}
	}

	struct SortableSource {
		ServerContentSource source;
		std::string foldedName;
		std::string comparablePath;
	};
	std::vector<SortableSource> sortable;
	sortable.reserve(assembledSources.size());
	for (ServerContentSource& source : assembledSources) {
		std::string foldedName = LowerAscii(source.name);
		std::string comparablePath = ComparablePath(source.declarationPath);
		sortable.push_back({ std::move(source), std::move(foldedName), std::move(comparablePath) });
	}
	std::sort(sortable.begin(), sortable.end(), [](const SortableSource& left, const SortableSource& right) {
		if (left.source.kind != right.source.kind) {
			return left.source.kind < right.source.kind;
		}
		if (left.foldedName != right.foldedName) {
			return left.foldedName < right.foldedName;
		}
		if (left.source.name != right.source.name) {
			return left.source.name < right.source.name;
		}
		return left.comparablePath != right.comparablePath ? left.comparablePath < right.comparablePath : left.source.declarationLine < right.source.declarationLine;
	});
	assembledSources.clear();
	assembledSources.reserve(sortable.size());
	for (SortableSource& value : sortable) {
		assembledSources.push_back(std::move(value.source));
	}
	index.sources = std::make_shared<const std::vector<ServerContentSource>>(std::move(assembledSources));
	std::sort(index.scanDiagnostics.begin(), index.scanDiagnostics.end());
	index.rebuildLookups();
	return index;
}

ServerContentIndex ServerContentIndex::Build(const ServerWorkspace& workspace, const ServerContentIndex* previous, const ServerContentScanOptions& options) {
	auto cacheState = std::make_shared<CacheState>();
	ServerContentScanStats stats;
	stats.fullScans = 1;

	std::vector<CandidateFile> candidates;
	std::set<std::string> seen;
	bool limitReached = false;
	DiscoverCandidates(workspace.monstersDirectory, ServerContentKind::Monster, options, candidates, seen, limitReached);
	DiscoverCandidates(workspace.npcsDirectory, ServerContentKind::Npc, options, candidates, seen, limitReached);
	DiscoverCandidates(workspace.spellsDirectory, ServerContentKind::Spell, options, candidates, seen, limitReached);
	std::sort(candidates.begin(), candidates.end(), [](const CandidateFile& left, const CandidateFile& right) {
		return left.kind != right.kind ? left.kind < right.kind : ComparablePath(left.path) < ComparablePath(right.path);
	});

	stats.filesDiscovered = candidates.size();
	stats.fileLimitReached = limitReached;
	std::vector<std::string> diagnostics;
	if (limitReached) {
		diagnostics.push_back("The server content scan reached its file limit; the partial index is marked incomplete.");
	}

	std::unordered_map<std::string, std::shared_ptr<const ParsedFile>> previousFiles;
	if (previous != nullptr && previous->cache && previous->matchesWorkspace(workspace)) {
		previousFiles.reserve(previous->cache->files.size());
		for (const auto& file : previous->cache->files) {
			previousFiles.emplace(CandidateKey(file->kind, file->fingerprint.path), file);
		}
	}

	cacheState->files.reserve(candidates.size());
	for (const CandidateFile& candidate : candidates) {
		const ResourceFingerprint fingerprint = ResourceFingerprint::Read(candidate.path);
		++stats.filesFingerprinted;
		const auto found = previousFiles.find(CandidateKey(candidate.kind, candidate.path));
		if (found != previousFiles.end() && found->second->fingerprint == fingerprint) {
			cacheState->files.push_back(found->second);
			++stats.filesReused;
			++stats.cacheRecordsShared;
			continue;
		}
		auto parsed = std::make_shared<ParsedFile>(ParseCandidate(workspace, candidate, fingerprint, options));
		stats.filesRead += fingerprint.exists && fingerprint.size <= options.maximumFileBytes ? 1 : 0;
		++stats.filesParsed;
		cacheState->files.push_back(std::move(parsed));
	}
	if (previous != nullptr && previous->matchesWorkspace(workspace)
		&& previous->cache && previous->cache->files.size() == cacheState->files.size()
		&& stats.filesReused == cacheState->files.size()) {
		ServerContentIndex reused = *previous;
		reused.cache = std::move(cacheState);
		reused.scanStats = stats;
		return reused;
	}
	return Assemble(workspace, std::move(cacheState), stats, std::move(diagnostics));
}

ServerContentIndex ServerContentIndex::RefreshPaths(
	const ServerWorkspace& workspace,
	const ServerContentIndex& previous,
	const std::vector<std::filesystem::path>& changedPaths,
	const ServerContentScanOptions& options
) {
	if (!previous.cache || !previous.matchesWorkspace(workspace) || changedPaths.empty()) {
		return Build(workspace, previous.cache ? &previous : nullptr, options);
	}

	std::unordered_map<std::string, std::shared_ptr<const ParsedFile>> existing;
	existing.reserve(previous.cache->files.size());
	for (const auto& file : previous.cache->files) {
		existing.emplace(CandidateKey(file->kind, file->fingerprint.path), file);
	}

	std::vector<CandidateFile> affected;
	std::set<std::string> affectedKeys;
	for (const std::filesystem::path& supplied : changedPaths) {
		if (supplied.empty()) {
			continue;
		}
		const std::filesystem::path path = Normalize(supplied);
		const std::string comparablePath = ComparablePath(path);
		std::vector<ServerContentKind> kinds = KindsForPath(workspace, path);
		for (const auto& [key, file] : existing) {
			if (ComparablePath(file->fingerprint.path) == comparablePath
				&& std::find(kinds.begin(), kinds.end(), file->kind) == kinds.end()) {
				kinds.push_back(file->kind);
			}
		}
		for (const auto& file : previous.cache->files) {
			const bool referencesChangedPath = std::any_of(file->sources.begin(), file->sources.end(), [&](const ServerContentSource& source) {
				return (source.registrationPath && ComparablePath(*source.registrationPath) == comparablePath)
					|| (source.relatedScriptPath && ComparablePath(*source.relatedScriptPath) == comparablePath);
			});
			if (referencesChangedPath) {
				const std::string key = CandidateKey(file->kind, file->fingerprint.path);
				if (affectedKeys.insert(key).second) {
					affected.push_back({ file->kind, file->fingerprint.path });
				}
			}
		}
		if (kinds.empty()) {
			const bool related = std::any_of(previous.entries().begin(), previous.entries().end(), [&](const ServerContentSource& source) {
				return (source.registrationPath && ComparablePath(*source.registrationPath) == comparablePath)
					|| (source.relatedScriptPath && ComparablePath(*source.relatedScriptPath) == comparablePath);
			});
			if (!related) {
				return Build(workspace, &previous, options);
			}
			continue;
		}
		for (const ServerContentKind kind : kinds) {
			const std::string key = CandidateKey(kind, path);
			if (affectedKeys.insert(key).second) {
				affected.push_back({ kind, path });
			}
		}
	}

	auto cacheState = std::make_shared<CacheState>();
	cacheState->files.reserve(previous.cache->files.size() + affected.size());
	ServerContentScanStats stats;
	stats.targetedRefreshes = 1;
	for (const auto& file : previous.cache->files) {
		if (!affectedKeys.contains(CandidateKey(file->kind, file->fingerprint.path))) {
			cacheState->files.push_back(file);
			++stats.filesReused;
			++stats.cacheRecordsShared;
		}
	}
	for (const CandidateFile& candidate : affected) {
		const std::string extension = LowerAscii(PathUtf8(candidate.path.extension()));
		const ResourceFingerprint fingerprint = ResourceFingerprint::Read(candidate.path);
		++stats.filesFingerprinted;
		const auto old = existing.find(CandidateKey(candidate.kind, candidate.path));
		if (old != existing.end() && old->second->fingerprint == fingerprint) {
			cacheState->files.push_back(old->second);
			++stats.filesReused;
			++stats.cacheRecordsShared;
		} else if (fingerprint.exists && (extension == ".xml" || extension == ".lua")) {
			cacheState->files.push_back(std::make_shared<ParsedFile>(ParseCandidate(workspace, candidate, fingerprint, options)));
			stats.filesRead += fingerprint.size <= options.maximumFileBytes ? 1 : 0;
			++stats.filesParsed;
		}
	}
	std::sort(cacheState->files.begin(), cacheState->files.end(), [](const auto& left, const auto& right) {
		return left->kind != right->kind ? left->kind < right->kind : ComparablePath(left->fingerprint.path) < ComparablePath(right->fingerprint.path);
	});
	stats.filesDiscovered = cacheState->files.size();
	return Assemble(workspace, std::move(cacheState), stats);
}

const std::vector<ServerContentSource>& ServerContentIndex::entries() const {
	return *sources;
}

const ServerContentCapabilities& ServerContentIndex::capabilities() const {
	return detectedCapabilities;
}

const std::vector<std::string>& ServerContentIndex::diagnostics() const {
	return scanDiagnostics;
}

const ServerContentScanStats& ServerContentIndex::stats() const {
	return scanStats;
}

bool ServerContentIndex::initialized() const {
	return built;
}

bool ServerContentIndex::matchesWorkspace(const ServerWorkspace& workspace) const {
	return built
		&& workspaceRoot == workspace.rootPath.lexically_normal()
		&& monstersRoot == workspace.monstersDirectory.lexically_normal()
		&& npcsRoot == workspace.npcsDirectory.lexically_normal()
		&& spellsRoot == workspace.spellsDirectory.lexically_normal();
}

std::shared_ptr<const std::vector<ServerContentSource>> ServerContentIndex::snapshot() const {
	return sources;
}

std::vector<std::size_t> ServerContentIndex::indicesForKind(ServerContentKind kind, bool existingOnly) const {
	std::vector<std::size_t> result;
	result.reserve(sources->size());
	for (std::size_t index = 0; index < sources->size(); ++index) {
		const ServerContentSource& source = (*sources)[index];
		if (source.kind == kind && (!existingOnly || source.declarationExists)) {
			result.push_back(index);
		}
	}
	return result;
}

void ServerContentIndex::rebuildLookups() {
	exactLookup.clear();
	caseFoldedLookup.clear();
	const auto add = [](LookupMap& lookup, const std::string& key, std::size_t index) {
		auto& matches = lookup[key];
		if (matches.empty() || matches.back() != index) {
			matches.push_back(index);
		}
	};
	for (std::size_t index = 0; index < sources->size(); ++index) {
		const ServerContentSource& source = (*sources)[index];
		const auto addName = [&](const std::string& name) {
			add(exactLookup, LookupKey(source.kind, name), index);
			add(caseFoldedLookup, LookupKey(source.kind, LowerAscii(name)), index);
		};
		addName(source.name);
		for (const std::string& alias : source.aliases) {
			addName(alias);
		}
	}
}

ServerContentLookupResult ServerContentIndex::findExact(ServerContentKind kind, const std::string& name) const {
	ServerContentLookupResult result;
	result.owner = sources;
	if (const auto found = exactLookup.find(LookupKey(kind, name)); found != exactLookup.end()) {
		result.matches.reserve(found->second.size());
		for (const std::size_t index : found->second) {
			result.matches.push_back(&(*sources)[index]);
		}
	}
	return result;
}

ServerContentLookupResult ServerContentIndex::findCaseInsensitive(ServerContentKind kind, const std::string& name) const {
	ServerContentLookupResult result;
	result.owner = sources;
	if (const auto found = caseFoldedLookup.find(LookupKey(kind, LowerAscii(name))); found != caseFoldedLookup.end()) {
		result.matches.reserve(found->second.size());
		for (const std::size_t index : found->second) {
			result.matches.push_back(&(*sources)[index]);
		}
	}
	return result;
}

bool ServerContentIndex::trackedSourcesChanged() const {
	if (cache && std::any_of(cache->files.begin(), cache->files.end(), [](const auto& file) {
			return !file->fingerprint.MatchesCurrentFile();
		})) {
		return true;
	}
	return std::any_of(sources->begin(), sources->end(), [](const ServerContentSource& source) {
		if (!source.declarationFingerprint.MatchesCurrentFile()) {
			return true;
		}
		if (source.registrationFingerprint && !source.registrationFingerprint->MatchesCurrentFile()) {
			return true;
		}
		return source.relatedScriptFingerprint && !source.relatedScriptFingerprint->MatchesCurrentFile();
	});
}

bool ServerContentIndex::sameContentAs(const ServerContentIndex& other) const {
	return (sources == other.sources || *sources == *other.sources)
		&& detectedCapabilities == other.detectedCapabilities
		&& scanDiagnostics == other.scanDiagnostics;
}

const char* ServerContentKindName(ServerContentKind kind) {
	switch (kind) {
		case ServerContentKind::Monster:
			return "Monster";
		case ServerContentKind::Npc:
			return "NPC";
		case ServerContentKind::Spell:
			return "Spell";
	}
	return "Unknown";
}

const char* ServerContentFormatName(ServerContentFormat format) {
	switch (format) {
		case ServerContentFormat::Xml:
			return "XML";
		case ServerContentFormat::Lua:
			return "Lua";
		default:
			return "Unknown";
	}
}
