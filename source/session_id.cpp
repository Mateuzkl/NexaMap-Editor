//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "session_id.h"

#include <atomic>
#include <cstdlib>

SessionId CreateSessionId() noexcept {
	static std::atomic<SessionId> nextId { 1 };
	const SessionId id = nextId.fetch_add(1, std::memory_order_relaxed);
	if (id == InvalidSessionId) {
		// Exhausting all 64-bit ids is not recoverable and must never allow the
		// invalid sentinel to masquerade as a real object identity.
		std::abort();
	}
	return id;
}
