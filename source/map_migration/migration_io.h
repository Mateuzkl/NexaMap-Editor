#ifndef NEXAMAP_MIGRATION_IO_H_
#define NEXAMAP_MIGRATION_IO_H_

#include "map_migration.h"
#include <fstream>
#include <span>
#include <stdexcept>

namespace MapMigration::IO {
	class Digest {
	public:
		Digest();
		void add(std::span<const uint8_t> bytes);
		void add(const std::string& value);
		std::string finish() const;

	private:
		void block(const uint8_t* data);
		std::array<uint32_t, 8> state;
		std::array<uint8_t, 64> buffer {};
		uint64_t bytes = 0;
		size_t buffered = 0;
	};
	struct Cursor {
		std::span<const uint8_t> data;
		size_t offset = 0;
		uint8_t u8();
		uint16_t u16();
		uint32_t u32();
		std::string text();
		std::span<const uint8_t> take(size_t);
		void skip(size_t size) {
			static_cast<void>(take(size));
		}
		[[nodiscard]] size_t remaining() const {
			return data.size() - offset;
		}
	};
	void Put16(std::vector<uint8_t>&, size_t offset, uint16_t value);
	void Put32(std::vector<uint8_t>&, size_t offset, uint32_t value);
	void AppendText(std::vector<uint8_t>&, const std::string&);
	[[nodiscard]] std::vector<uint8_t> ReadFile(const Path&, uint64_t maximumBytes);
	[[nodiscard]] std::string Utf8(const Path&);
	[[nodiscard]] Path FromUtf8(const std::string&);

	// Streaming visitor. Only one node payload per nesting level is retained.
	// Payloads include the node type and may be changed before serialization.
	using NodeVisitor = std::function<void(std::vector<uint8_t>&, size_t depth, const std::vector<uint8_t>& parent)>;
	void Walk(const Path&, const std::string& magic, const NodeVisitor&, const Control&, const Path& output = {});

} // namespace MapMigration::IO
#endif
