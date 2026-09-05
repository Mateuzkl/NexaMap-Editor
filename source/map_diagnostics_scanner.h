#ifndef RME_MAP_DIAGNOSTICS_SCANNER_H_
#define RME_MAP_DIAGNOSTICS_SCANNER_H_

#include "map_diagnostics.h"
#include "session_id.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Map;

class MapDiagnosticsScanner {
public:
	explicit MapDiagnosticsScanner(Map& map);
	~MapDiagnosticsScanner();

	MapDiagnosticsScanner(const MapDiagnosticsScanner&) = delete;
	MapDiagnosticsScanner& operator=(const MapDiagnosticsScanner&) = delete;

	void Start();
	void Cancel();
	void Step(size_t workUnits);

	bool IsRunning() const;
	bool IsComplete() const;
	bool WasCancelled() const;
	int GetProgressPercent() const;
	uint64_t GetProcessedTileCount() const;
	uint64_t GetTotalTileCount() const;
	SessionId GetMapSessionId() const;
	const std::vector<MapDiagnosticIssue>& GetIssues() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
