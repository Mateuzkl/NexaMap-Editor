//////////////////////////////////////////////////////////////////////
// Source-preserving codecs for structured monster editor sections.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_SECTION_CODEC_H_
#define NEXAMAP_MONSTER_SECTION_CODEC_H_

#include "monster_definition.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct MonsterTextPatch {
	std::size_t begin = 0;
	std::size_t end = 0;
	std::string replacement;
};

class MonsterSectionCodec {
public:
	struct Impl;

	static std::unique_ptr<MonsterSectionCodec> Parse(
		ServerContentFormat format,
		ServerType serverType,
		std::string_view source,
		MonsterDefinition& definition,
		std::string& error
	);

	~MonsterSectionCodec();
	MonsterSectionCodec(MonsterSectionCodec&&) noexcept;
	MonsterSectionCodec& operator=(MonsterSectionCodec&&) noexcept;
	MonsterSectionCodec(const MonsterSectionCodec&) = delete;
	MonsterSectionCodec& operator=(const MonsterSectionCodec&) = delete;

	[[nodiscard]] bool hasChanges(const MonsterDefinition& original, const MonsterDefinition& edited) const;
	bool buildPatches(
		const MonsterDefinition& original,
		const MonsterDefinition& edited,
		std::vector<MonsterTextPatch>& patches,
		std::string& error
	) const;

private:
	explicit MonsterSectionCodec(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> implementation;
};

#endif // NEXAMAP_MONSTER_SECTION_CODEC_H_
