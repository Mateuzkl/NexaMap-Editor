// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_protocol.h"
#include "multiplayer_crypto.h"
#include <iostream>
#include <functional>
#include <cstdlib>
using namespace Multiplayer;
namespace {
	void check(bool ok, const char* reason) {
		if (!ok) {
			std::cerr << reason << '\n';
			std::exit(1);
		}
	}
	void rejects(const std::function<void()>& fn) {
		bool rejected = false;
		try {
			fn();
		} catch (const Error&) {
			rejected = true;
		}
		check(rejected, "Malformed input was accepted");
	}
}
int main() {
	Writer fields;
	fields.u8(255);
	fields.u16(0xabcd);
	fields.u32(0x12345678);
	fields.u64(0x1122334455667788);
	fields.string("Olá");
	Reader in(fields.data);
	check(in.u8() == 255 && in.u16() == 0xabcd && in.u32() == 0x12345678 && in.u64() == 0x1122334455667788 && in.string() == "Olá", "Wire roundtrip");
	in.finish();
	rejects([&] { in.u8(); });
	for (uint32_t bad : { 0u, uint32_t(MaxPacket + 1), UINT32_MAX }) {
		Writer header;
		header.u32(bad);
		rejects([&] { FrameReader fr; fr.append(header.data); });
	}
	auto packet = frame(Packet::Chat, fields.data);
	for (size_t step : { 1u, 3u, 7u, 64u }) {
		FrameReader fr;
		size_t offset = 0;
		while (offset < packet.size()) {
			auto n = std::min({ step, fr.needed(), packet.size() - offset });
			fr.append(std::span(packet).subspan(offset, n));
			offset += n;
		}
		check(fr.ready(), "Fragmented frame did not complete");
		auto value = fr.take();
		check(value[0] == uint8_t(Packet::Chat) && value.size() == fields.data.size() + 1, "Frame payload");
	}
	check(validText("Mateus", MaxName) && validText("João", MaxName) && !validText("bad\nname", MaxName) && !validText(std::string("\xc0\x80", 2), MaxName), "Text validation");
	check(!validTileKey(uint64_t(16) << 32) && validTileKey(tileKey(65535, 65535, 15)), "Floor validation");
	Identity session = randomIdentity();
	check(session != randomIdentity(), "Session identities");
	check(hex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 vector");
	Digest vectorKey;
	for (size_t i = 0; i < vectorKey.size(); ++i) {
		vectorKey[i] = static_cast<uint8_t>(i);
	}
	const std::string vectorText = "NexaMap";
	check(hex(hmacSha256(vectorKey, { reinterpret_cast<const uint8_t*>(vectorText.data()), vectorText.size() })) == "11c6438ed067c4e21705d153269edbdaf8a12201e97f8269e815113a01016258", "HMAC-SHA256 vector");
	Identity vectorSalt;
	for (size_t i = 0; i < vectorSalt.size(); ++i) {
		vectorSalt[i] = static_cast<uint8_t>(i);
	}
	check(hex(deriveSessionKey("secret", vectorSalt)) == "9fb9dd3c8e2944366b788c7e795a5a6350d58845b2a3d21ad65f07b55181f837", "PBKDF2 vector");
	auto challenge = randomIdentity();
	auto hello = randomIdentity();
	auto sessionKey = deriveSessionKey("secret", session);
	auto keys = deriveHandshake(sessionKey, session, challenge, hello);
	auto sameKeys = deriveHandshake(deriveSessionKey("secret", session), session, challenge, hello);
	check(secureEqual(keys.clientProof, sameKeys.clientProof) && secureEqual(keys.hostProof, sameKeys.hostProof) && secureEqual(keys.clientToHost, sameKeys.clientToHost) && secureEqual(keys.hostToClient, sameKeys.hostToClient), "Handshake keys must be reproducible");
	check(!secureEqual(keys.clientProof, keys.hostProof) && !secureEqual(keys.clientToHost, keys.hostToClient) && !secureEqual(keys.clientProof, keys.clientToHost), "Proofs and directions must be separated");
	check(!secureEqual(keys.clientProof, deriveHandshake(deriveSessionKey("wrong", session), session, challenge, hello).clientProof), "Password binding");
	check(!secureEqual(keys.hostProof, deriveHandshake(sessionKey, session, randomIdentity(), hello).hostProof), "Host nonce binding");
	check(!secureEqual(keys.hostProof, deriveHandshake(sessionKey, session, challenge, randomIdentity()).hostProof), "Client hello binding");
	check(!secureEqual(keys.hostProof, deriveHandshake(sessionKey, randomIdentity(), challenge, hello).hostProof), "Session binding");

	auto sealedFrame = [&](const Digest& key, uint64_t sequence, std::span<const uint8_t> payload) {
		auto sealed = sealFrame(key, sequence, Packet::Chat, payload);
		FrameReader reader;
		reader.append(std::span(sealed).first(4));
		reader.append(std::span(sealed).subspan(4));
		return reader.take();
	};
	const auto original = sealedFrame(keys.clientToHost, 7, fields.data);
	auto opened = original;
	check(openFrame(keys.clientToHost, 7, opened) && opened.size() == fields.data.size() + 1 && opened[0] == uint8_t(Packet::Chat) && std::equal(fields.data.begin(), fields.data.end(), opened.begin() + 1), "Sealed frame roundtrip");
	for (size_t n = 0; n < original.size(); ++n) {
		auto tampered = original;
		tampered[n] ^= 1;
		check(!openFrame(keys.clientToHost, 7, tampered) && tampered == [&] { auto copy = original; copy[n] ^= 1; return copy; }(), "Tampered frame accepted or modified");
	}
	auto replayed = original;
	check(!openFrame(keys.clientToHost, 8, replayed) && !openFrame(keys.clientToHost, 6, replayed), "Frame accepted under another sequence number");
	check(!openFrame(keys.hostToClient, 7, replayed), "Frame accepted in the other direction");
	Bytes tooShort(MacSize, 0);
	check(!openFrame(keys.clientToHost, 0, tooShort), "Frame without a body accepted");
	check(sealedFrame(keys.clientToHost, 0, {}).size() == MacSize + 1, "Empty payload frame size");
	Bytes largest(MaxPacket - MacSize - 1, 0);
	check(sealFrame(keys.clientToHost, 0, Packet::Chat, largest).size() == MaxPacket + 4, "Largest sealed frame must still pass FrameReader");
	largest.push_back(0);
	rejects([&] { sealFrame(keys.clientToHost, 0, Packet::Chat, largest); });

	check(!validHostPassword("", false) && !validHostPassword("1234567", false) && validHostPassword("12345678", false), "Host password length");
	check(validHostPassword("", true) && validHostPassword("short", true), "Loopback session password");
	check(!validHostPassword("żółć12", false) && validHostPassword("żółć1234", false), "Host password length counts characters, not bytes");
	check(printableText("plain text") == "plain text" && printableText("João") == "João" && printableText("a\nb") == "a?b" && printableText(std::string("\xff", 1)) == "?", "Log text cleaning");
	Revisions revisions;
	Locks locks;
	Transaction a;
	a.session = session;
	a.id = 1;
	a.author = 1;
	a.tiles.push_back({ tileKey(10, 20, 7), {}, { 1, 2, 3 } });
	auto b = a;
	b.author = 2;
	check(revisions.validate(a, session, Role::Editor, 1, locks, 0).empty(), "Initial edit rejected");
	revisions.commit(a);
	check(!revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Overlapping concurrent edit accepted");
	b.tiles[0].key = tileKey(11, 20, 7);
	check(revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Disjoint concurrent edit rejected");
	revisions.commit(b);
	check(!revisions.validate(a, session, Role::Editor, 1, locks, 0).empty(), "Duplicate transaction accepted");
	b.id = 2;
	b.base = revisions.current();
	check(!revisions.validate(b, session, Role::Viewer, 2, locks, 0).empty(), "Viewer can write");
	check(!revisions.validate(b, randomIdentity(), Role::Editor, 2, locks, 0).empty(), "Wrong session accepted");
	check(locks.acquire(1, 1, { 10, 20, 30, 40, 7 }, 0), "Lock acquisition");
	check(!locks.acquire(2, 1, { 25, 25, 35, 35, 7 }, 0), "Overlapping area lock accepted");
	check(!revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Host ignored another client's lock");
	locks.release(2, 1);
	check(!locks.permits(2, b.tiles[0].key, 0), "Nonowner unlocked a region");
	locks.releaseOwner(1);
	check(locks.permits(2, b.tiles[0].key, 0), "Disconnect didn't release locks");
	locks.acquire(1, 1, { 10, 20, 30, 40, 7 }, 0);
	check(locks.expire(30000) && locks.all().empty(), "Lease expiry");
	Locks capped;
	check(capped.acquire(1, 1, { 0, 0, 511, 511, 7 }, 0), "Largest allowed lock rejected");
	check(!capped.acquire(1, 2, { 0, 0, 512, 511, 7 }, 0), "Oversized lock accepted");
	check(!capped.acquire(1, 3, { 0, 0, 65535, 65535, 7 }, 0), "Whole-floor lock accepted");
	check(!capped.acquire(1, 1, { 0, 0, 1023, 1023, 7 }, 0), "Renewal grew a lock past the area cap");
	for (uint16_t z = 0; z < 16; ++z) {
		check(capped.acquire(2, z + 1, { 2000, 2000, 2100, 2100, static_cast<uint8_t>(z) }, 0), "Multi-floor Lock Selection must fit the owner cap");
	}
	for (uint64_t id = 2; id <= MaxLocksPerOwner; ++id) {
		check(capped.acquire(1, id, { static_cast<uint16_t>(id * 10), 1000, static_cast<uint16_t>(id * 10), 1000, 7 }, 0), "Lock under the owner cap rejected");
	}
	check(!capped.acquire(1, 999, { 9000, 9000, 9000, 9000, 7 }, 0), "Owner exceeded the lease cap");
	check(capped.acquire(1, 2, { 20, 1000, 20, 1000, 7 }, 25000), "Renewal at the owner cap rejected");
	check(capped.acquire(3, 1, { 9000, 9000, 9000, 9000, 7 }, 0), "One owner's cap blocked another owner");
	Revisions hostEdits;
	Transaction hostEdit;
	hostEdit.session = session;
	hostEdit.id = 1;
	hostEdit.tiles.push_back({ tileKey(100, 100, 7), {}, { 1 } });
	Locks hostLocked;
	check(hostLocked.acquire(1, 1, { 0, 0, 511, 511, 7 }, 0), "Host exemption lock");
	check(hostEdits.validate(hostEdit, session, Role::Host, 0, hostLocked, 0).empty(), "A client lock blocked the host");
	hostEdit.author = 2;
	check(!hostEdits.validate(hostEdit, session, Role::Editor, 2, hostLocked, 0).empty(), "A client lock stopped protecting other editors");
	hostLocked.releaseOverlapping({ 100, 100, 100, 100, 7 });
	check(hostLocked.all().empty(), "Host could not break an overlapping lock");
	auto encoded = encodeTransaction(b);
	check(encodeTransaction(decodeTransaction(encoded)) == encoded, "Transaction roundtrip");
	for (size_t n = 0; n < encoded.size(); ++n) {
		rejects([&] { decodeTransaction(std::span(encoded).first(n)); });
	}
	auto duplicate = b;
	duplicate.tiles.push_back(b.tiles[0]);
	rejects([&] { decodeTransaction(encodeTransaction(duplicate)); });
	Journal journal;
	journal.push(1, { 1 });
	journal.push(2, { 2 });
	check(journal.covers(0, 2) && journal.covers(2, 2) && !journal.covers(3, 2), "Journal range");
	for (uint64_t i = 3; i <= 6000; ++i) {
		journal.push(i, { 0 });
	}
	check(!journal.covers(0, 6000) && journal.covers(5999, 6000) && journal.entries().size() == 5000, "Journal retention");
	// 100k interleaved operations, multiple clients; revisions remain contiguous.
	Revisions stress;
	for (uint64_t i = 1; i <= 100000; ++i) {
		a.id = i;
		a.author = uint32_t(i % 3 + 1);
		a.base = stress.current();
		a.tiles[0].key = tileKey(uint16_t(i % 65536), uint16_t(i / 65536), 7);
		check(stress.validate(a, session, Role::Editor, a.author, locks, 30000).empty(), "Stress validation");
		check(stress.commit(a) == i, "Stress revision sequence");
	}
	std::cout << "Multiplayer protocol tests passed (100000 revisions).\n";
}
