// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_protocol.h"
#include "multiplayer_crypto.h"
#include <iostream>
#include <functional>
#include <cstdlib>
using namespace Multiplayer;
namespace {
void check(bool ok, const char* reason) { if (!ok) { std::cerr << reason << '\n'; std::exit(1); } }
void rejects(const std::function<void()>& fn) { bool rejected = false; try { fn(); } catch (const Error&) { rejected = true; } check(rejected, "Malformed input was accepted"); }
}
int main() {
    Writer fields; fields.u8(255); fields.u16(0xabcd); fields.u32(0x12345678); fields.u64(0x1122334455667788); fields.string("Olá");
    Reader in(fields.data); check(in.u8() == 255 && in.u16() == 0xabcd && in.u32() == 0x12345678 && in.u64() == 0x1122334455667788 && in.string() == "Olá", "Wire roundtrip"); in.finish();
    rejects([&] { in.u8(); });
    for (uint32_t bad : {0u, uint32_t(MaxPacket + 1), UINT32_MAX}) {
        Writer header; header.u32(bad); rejects([&] { FrameReader fr; fr.append(header.data); });
    }
    auto packet = frame(Packet::Chat, fields.data);
    for (size_t step : {1u, 3u, 7u, 64u}) {
        FrameReader fr; size_t offset = 0;
        while (offset < packet.size()) { auto n = std::min({step, fr.needed(), packet.size() - offset}); fr.append(std::span(packet).subspan(offset, n)); offset += n; }
        check(fr.ready(), "Fragmented frame did not complete"); auto value = fr.take(); check(value[0] == uint8_t(Packet::Chat) && value.size() == fields.data.size() + 1, "Frame payload");
    }
    check(validText("Mateus", MaxName) && validText("João", MaxName) && !validText("bad\nname", MaxName) && !validText(std::string("\xc0\x80", 2), MaxName), "Text validation");
    check(!validTileKey(uint64_t(16) << 32) && validTileKey(tileKey(65535, 65535, 15)), "Floor validation");
    Identity session = randomIdentity(); check(session != randomIdentity(), "Session identities");
    check(hex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 vector");
    auto challenge = randomIdentity(); auto proof = authenticate("secret", session, challenge);
    check(secureEqual(proof, authenticate("secret", session, challenge)) && !secureEqual(proof, authenticate("wrong", session, challenge)) && !secureEqual(proof, authenticate("secret", session, randomIdentity())), "Password/challenge binding");
    Revisions revisions; Locks locks;
    Transaction a; a.session = session; a.id = 1; a.author = 1; a.tiles.push_back({tileKey(10, 20, 7), {}, {1, 2, 3}});
    auto b = a; b.author = 2;
    check(revisions.validate(a, session, Role::Editor, 1, locks, 0).empty(), "Initial edit rejected"); revisions.commit(a);
    check(!revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Overlapping concurrent edit accepted");
    b.tiles[0].key = tileKey(11, 20, 7);
    check(revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Disjoint concurrent edit rejected"); revisions.commit(b);
    check(!revisions.validate(a, session, Role::Editor, 1, locks, 0).empty(), "Duplicate transaction accepted");
    b.id = 2; b.base = revisions.current();
    check(!revisions.validate(b, session, Role::Viewer, 2, locks, 0).empty(), "Viewer can write");
    check(!revisions.validate(b, randomIdentity(), Role::Editor, 2, locks, 0).empty(), "Wrong session accepted");
    check(locks.acquire(1, 1, {10, 20, 30, 40, 7}, 0), "Lock acquisition");
    check(!locks.acquire(2, 1, {25, 25, 35, 35, 7}, 0), "Overlapping area lock accepted");
    check(!revisions.validate(b, session, Role::Editor, 2, locks, 0).empty(), "Host ignored another client's lock");
    locks.release(2, 1); check(!locks.permits(2, b.tiles[0].key, 0), "Nonowner unlocked a region");
    locks.releaseOwner(1); check(locks.permits(2, b.tiles[0].key, 0), "Disconnect didn't release locks");
    locks.acquire(1, 1, {10, 20, 30, 40, 7}, 0); check(locks.expire(30000) && locks.all().empty(), "Lease expiry");
    auto encoded = encodeTransaction(b); check(encodeTransaction(decodeTransaction(encoded)) == encoded, "Transaction roundtrip");
    for (size_t n = 0; n < encoded.size(); ++n) rejects([&] { decodeTransaction(std::span(encoded).first(n)); });
    auto duplicate = b; duplicate.tiles.push_back(b.tiles[0]); rejects([&] { decodeTransaction(encodeTransaction(duplicate)); });
    Journal journal; journal.push(1, {1}); journal.push(2, {2}); check(journal.covers(0, 2) && journal.covers(2, 2) && !journal.covers(3, 2), "Journal range");
    for (uint64_t i = 3; i <= 6000; ++i) journal.push(i, {0});
    check(!journal.covers(0, 6000) && journal.covers(5999, 6000) && journal.entries().size() == 5000, "Journal retention");
    // 100k interleaved operations, multiple clients; revisions remain contiguous.
    Revisions stress;
    for (uint64_t i = 1; i <= 100000; ++i) {
        a.id = i; a.author = uint32_t(i % 3 + 1); a.base = stress.current(); a.tiles[0].key = tileKey(uint16_t(i % 65536), uint16_t(i / 65536), 7);
        check(stress.validate(a, session, Role::Editor, a.author, locks, 30000).empty(), "Stress validation"); check(stress.commit(a) == i, "Stress revision sequence");
    }
    std::cout << "Multiplayer protocol tests passed (100000 revisions).\n";
}
