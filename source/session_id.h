//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_SESSION_ID_H_
#define RME_SESSION_ID_H_

#include <cstdint>

using SessionId = uint64_t;

constexpr SessionId InvalidSessionId = 0;

// Returns a process-local identity that is never based on an object's address.
// Session ids are intentionally not serialized; they distinguish live editor
// objects and remain safe to retain after the original object is destroyed.
SessionId CreateSessionId() noexcept;

#endif
