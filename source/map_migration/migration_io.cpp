#include "migration_io.h"
#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <psapi.h>
#elif defined(__linux__)
	#include <unistd.h>
#endif

namespace MapMigration {
	namespace {
		uint64_t ResidentBytes() {
#ifdef _WIN32
			PROCESS_MEMORY_COUNTERS counters {};
			return K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) ? counters.WorkingSetSize : 0;
#elif defined(__linux__)
			uint64_t total = 0, resident = 0;
			std::ifstream status("/proc/self/statm");
			return status >> total >> resident ? resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) : 0;
#else
			return 0;
#endif
		}
	}
	void Control::check() const {
		if (cancelled && cancelled->load(std::memory_order_relaxed)) {
			throw std::runtime_error("Operação cancelada.");
		}
		if (metrics) {
			std::lock_guard lock(metrics->mutex);
			const auto now = std::chrono::steady_clock::now();
			if (now - metrics->lastProbe >= std::chrono::milliseconds(25)) {
				const auto resident = ResidentBytes();
				if (!metrics->baselineResidentBytes) {
					metrics->baselineResidentBytes = resident;
				}
				metrics->lastProbe = now;
				metrics->peakResidentBytes = std::max(metrics->peakResidentBytes, resident);
				if (resident > metrics->baselineResidentBytes && resident - metrics->baselineResidentBytes > memoryLimitBytes) {
					throw std::runtime_error("Job excede o orçamento agregado de memória (RSS/Working Set). Nenhuma saída foi publicada.");
				}
			}
		}
	}
	void Control::retain(uint64_t bytes) const {
		check();
		if (metrics) {
			std::lock_guard lock(metrics->mutex);
			if (bytes > memoryLimitBytes || metrics->retainedBytes > memoryLimitBytes - bytes) {
				throw std::runtime_error("Catálogos excedem o orçamento agregado de memória.");
			}
			metrics->retainedBytes += bytes;
		}
	}
	void Control::update(int percent, const std::string& message) const {
		check();
		if (progress) {
			progress(std::clamp(percent, 0, 100), message);
		}
		check();
	}
	std::string Sha256(const std::vector<uint8_t>& data) {
		IO::Digest digest;
		digest.add(data);
		return digest.finish();
	}
	FileDigest FingerprintFile(const Path& path, const Control& control) {
		IO::StageTimer timer(control, "resourceHash");
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			throw std::runtime_error("Não foi possível ler: " + IO::Utf8(path));
		}
		IO::Digest hash;
		std::array<uint8_t, 65536> buffer;
		uint64_t size = 0;
		while (file) {
			control.check();
			file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
			const auto count = static_cast<size_t>(file.gcount());
			hash.add(std::span(buffer.data(), count));
			size += count;
		}
		if (!file.eof()) {
			throw std::runtime_error("Falha de leitura: " + IO::Utf8(path));
		}
		if (control.metrics) {
			std::lock_guard lock(control.metrics->mutex);
			control.metrics->hashedBytes += size;
		}
		return { std::filesystem::absolute(path).lexically_normal(), size, hash.finish() };
	}
}

namespace MapMigration::IO {
	namespace {
		constexpr std::array<uint32_t, 64> K = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};
	}
	Digest::Digest() :
		state { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 } { }
	void Digest::block(const uint8_t* data) {
		std::array<uint32_t, 64> w {};
		for (size_t i = 0; i < 16; ++i) {
			w[i] = (uint32_t(data[i * 4]) << 24) | (uint32_t(data[i * 4 + 1]) << 16) | (uint32_t(data[i * 4 + 2]) << 8) | data[i * 4 + 3];
		}
		for (size_t i = 16; i < 64; ++i) {
			const auto a = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const auto b = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + a + w[i - 7] + b;
		}
		auto [a, b, c, d, e, f, g, h] = state;
		for (size_t i = 0; i < 64; ++i) {
			const auto t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
			const auto t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}
	void Digest::add(std::span<const uint8_t> input) {
		bytes += input.size();
		while (!input.empty()) {
			const size_t size = std::min(input.size(), buffer.size() - buffered);
			std::copy_n(input.begin(), size, buffer.begin() + buffered);
			buffered += size;
			input = input.subspan(size);
			if (buffered == 64) {
				block(buffer.data());
				buffered = 0;
			}
		}
	}
	void Digest::add(const std::string& text) {
		add(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
	}
	std::string Digest::finish() const {
		Digest copy = *this;
		const uint64_t bits = bytes * 8;
		const uint8_t one = 0x80, zero = 0;
		copy.add(std::span(&one, 1));
		while (copy.buffered != 56) {
			copy.add(std::span(&zero, 1));
		}
		std::array<uint8_t, 8> length {};
		for (size_t i = 0; i < 8; ++i) {
			length[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
		}
		copy.add(length);
		std::ostringstream out;
		for (const auto value : copy.state) {
			out << std::hex << std::setfill('0') << std::setw(8) << value;
		}
		return out.str();
	}
	std::span<const uint8_t> Cursor::take(size_t size) {
		if (size > remaining()) {
			throw std::runtime_error("Arquivo truncado ou comprimento de atributo inválido.");
		}
		auto result = data.subspan(offset, size);
		offset += size;
		return result;
	}
	uint8_t Cursor::u8() {
		return take(1)[0];
	}
	uint16_t Cursor::u16() {
		auto p = take(2);
		return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
	}
	uint32_t Cursor::u32() {
		auto p = take(4);
		return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
	}
	std::string Cursor::text() {
		const auto size = u16();
		auto p = take(size);
		return { reinterpret_cast<const char*>(p.data()), p.size() };
	}
	void Put16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
		if (offset > data.size() || data.size() - offset < 2) {
			throw std::runtime_error("Campo de ID truncado.");
		}
		data[offset] = static_cast<uint8_t>(value);
		data[offset + 1] = static_cast<uint8_t>(value >> 8);
	}
	void Put32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
		Put16(data, offset, static_cast<uint16_t>(value));
		Put16(data, offset + 2, static_cast<uint16_t>(value >> 16));
	}
	void AppendText(std::vector<uint8_t>& data, const std::string& text) {
		if (text.size() > 65535) {
			throw std::runtime_error("Referência externa muito longa.");
		}
		data.push_back(static_cast<uint8_t>(text.size()));
		data.push_back(static_cast<uint8_t>(text.size() >> 8));
		data.insert(data.end(), text.begin(), text.end());
	}
	std::string Utf8(const Path& path) {
		auto text = path.u8string();
		return { text.begin(), text.end() };
	}
	Path FromUtf8(const std::string& text) {
		return Path(std::u8string(text.begin(), text.end()));
	}
	std::vector<uint8_t> ReadFile(const Path& path, uint64_t maximum) {
		const auto size = std::filesystem::file_size(path);
		if (size > maximum) {
			throw std::runtime_error("Arquivo excede o limite de memória: " + Utf8(path));
		}
		std::vector<uint8_t> data(static_cast<size_t>(size));
		std::ifstream file(path, std::ios::binary);
		if (!file || !file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
			throw std::runtime_error("Falha ao ler: " + Utf8(path));
		}
		return data;
	}
	void Walk(const Path& path, const std::string& magic, const NodeVisitor& visit, const Control& control, const Path& output) {
		StageTimer timer(control, output.empty() ? "parseMap" : "writeMap");
		std::ifstream input(path, std::ios::binary);
		std::array<char, 4> header {};
		if (!input.read(header.data(), 4)) {
			throw std::runtime_error("Cabeçalho binário truncado: " + Utf8(path));
		}
		if (std::string(header.data(), 4) != magic && !std::all_of(header.begin(), header.end(), [](char c) { return c == 0; })) {
			throw std::runtime_error("Identificador inválido: " + Utf8(path));
		}
		std::ofstream out;
		if (!output.empty()) {
			out.open(output, std::ios::binary | std::ios::trunc);
			if (!out) {
				throw std::runtime_error("Não foi possível criar a saída temporária.");
			}
			out.write(header.data(), 4);
		}
		uint64_t nodes = 0;
		const uint64_t size = std::filesystem::file_size(path);
		uint64_t retained = 0;
		std::function<void(size_t, const std::vector<uint8_t>&)> node;
		node = [&](size_t depth, const std::vector<uint8_t>& parent) {
			if (depth > 256 || input.get() != 0xFE) {
				throw std::runtime_error("Estrutura binária inválida ou profundidade excessiva.");
			}
			std::vector<uint8_t> payload;
			for (;;) {
				if (payload.size() % 65536 == 0) {
					control.check();
				}
				int byte = input.peek();
				if (byte == 0xFE || byte == 0xFF) {
					break;
				}
				byte = input.get();
				if (byte == 0xFD) {
					byte = input.get();
				}
				if (byte < 0) {
					throw std::runtime_error("Nó binário truncado.");
				}
				payload.push_back(static_cast<uint8_t>(byte));
				if (payload.size() + retained > control.memoryLimitBytes / 4) {
					throw std::runtime_error("Nó excede o orçamento de memória.");
				}
			}
			if (payload.empty()) {
				throw std::runtime_error("Nó binário sem tipo.");
			}
			if (++nodes % 4096 == 0) {
				control.update(size ? static_cast<int>(100.0 * static_cast<double>(input.tellg()) / size) : 0, "Lendo estrutura do mapa...");
			}
			control.check();
			visit(payload, depth, parent);
			if (out.is_open()) {
				out.put(static_cast<char>(0xFE));
				for (auto byte : payload) {
					if (byte >= 0xFD) {
						out.put(static_cast<char>(0xFD));
					}
					out.put(static_cast<char>(byte));
				}
			}
			retained += payload.size();
			while (input.peek() == 0xFE) {
				node(depth + 1, payload);
			}
			retained -= payload.size();
			if (input.get() != 0xFF) {
				throw std::runtime_error("Fim de nó inválido.");
			}
			if (out.is_open()) {
				out.put(static_cast<char>(0xFF));
			}
		};
		node(0, {});
		if (input.peek() != std::char_traits<char>::eof()) {
			throw std::runtime_error("Bytes inesperados após o nó raiz.");
		}
		if (out.is_open()) {
			out.flush();
			if (!out) {
				throw std::runtime_error("Falha ao gravar o mapa temporário.");
			}
			out.close();
		}
	}
}
