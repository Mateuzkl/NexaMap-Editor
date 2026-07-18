#include "procedural_map_generator.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ProceduralMap {
namespace {

constexpr uint64_t kHashOffset = 1469598103934665603ULL;
constexpr uint64_t kHashPrime = 1099511628211ULL;
constexpr uint16_t kStructureFeatures = FeatureWall | FeatureEntrance | FeatureExit | FeatureTransition | FeaturePointOfInterest;

struct LocalPoint {
	int x = 0;
	int y = 0;
};

struct RoomRect {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	LocalPoint center() const noexcept {
		return { x + width / 2, y + height / 2 };
	}
};

uint64_t Mix64(uint64_t value) noexcept {
	value += 0x9E3779B97F4A7C15ULL;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
	return value ^ (value >> 31);
}

uint64_t CellHash(uint64_t seed, int x, int y, int z, uint64_t salt = 0) noexcept {
	uint64_t value = seed ^ Mix64(salt);
	value ^= Mix64(static_cast<uint32_t>(x));
	value ^= Mix64(static_cast<uint32_t>(y) + 0x9E3779B9U);
	value ^= Mix64(static_cast<uint32_t>(z) + 0x85EBCA6BU);
	return Mix64(value);
}

double UnitHash(uint64_t seed, int x, int y, int z, uint64_t salt = 0) noexcept {
	return static_cast<double>(CellHash(seed, x, y, z, salt) >> 11) * (1.0 / 9007199254740992.0);
}

double Smooth(double t) noexcept {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double Lerp(double a, double b, double t) noexcept {
	return a + (b - a) * t;
}

double ValueNoise(uint64_t seed, double x, double y, int z, uint64_t salt) noexcept {
	const int x0 = static_cast<int>(std::floor(x));
	const int y0 = static_cast<int>(std::floor(y));
	const double tx = Smooth(x - x0);
	const double ty = Smooth(y - y0);
	const auto sample = [&](int sx, int sy) {
		return UnitHash(seed, sx, sy, z, salt) * 2.0 - 1.0;
	};
	return Lerp(Lerp(sample(x0, y0), sample(x0 + 1, y0), tx), Lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), tx), ty);
}

double FractalNoise(const GenerationRequest& request, int x, int y, int z, uint64_t salt = 0) noexcept {
	const auto& p = request.parameters;
	const double scale = std::max(1.0, p.noiseScale);
	const double baseX = static_cast<double>(x) / scale;
	const double baseY = static_cast<double>(y) / scale;
	const double warpX = ValueNoise(request.seed, baseX * 0.55, baseY * 0.55, z, salt + 101) * p.irregularity * 1.8;
	const double warpY = ValueNoise(request.seed, baseX * 0.55 + 37.0, baseY * 0.55 - 19.0, z, salt + 211) * p.irregularity * 1.8;

	double amplitude = 1.0;
	double frequency = 1.0;
	double total = 0.0;
	double normalizer = 0.0;
	const int octaveCount = std::clamp(p.octaves, 1, 10);
	for (int octave = 0; octave < octaveCount; ++octave) {
		total += ValueNoise(request.seed, (baseX + warpX) * frequency, (baseY + warpY) * frequency, z, salt + static_cast<uint64_t>(octave) * 17) * amplitude;
		normalizer += amplitude;
		amplitude *= std::clamp(p.persistence, 0.05, 0.95);
		frequency *= std::clamp(p.lacunarity, 1.1, 4.0);
	}
	return normalizer > 0.0 ? total / normalizer : 0.0;
}

size_t RequestIndex(const GenerationRequest& request, int x, int y, int z) noexcept {
	return (static_cast<size_t>(z - request.minZ) * static_cast<size_t>(request.height()) + static_cast<size_t>(y - request.minY)) * static_cast<size_t>(request.width()) + static_cast<size_t>(x - request.minX);
}

size_t PlanIndex(const GenerationRequest& request, int x, int y, int z) noexcept {
	return RequestIndex(request, x, y, z);
}

void Report(const ProgressCallback& callback, int percent, GenerationStage stage) {
	if (callback) {
		callback(percent, stage);
	}
}

void CheckCancelled(const std::stop_token& token) {
	if (token.stop_requested()) {
		throw std::runtime_error("Generation cancelled.");
	}
}

GeneratedCell& CellAt(GenerationPlan& plan, int x, int y, int z) {
	return plan.cells[PlanIndex(plan.request, x, y, z)];
}

const GeneratedCell& CellAt(const GenerationPlan& plan, int x, int y, int z) {
	return plan.cells[PlanIndex(plan.request, x, y, z)];
}

bool IsPlanned(const GenerationPlan& plan, int x, int y, int z) noexcept {
	return plan.request.isAllowed(x, y, z) && CellAt(plan, x, y, z).material != MaterialRole::None;
}

bool IsWalkable(const GenerationPlan& plan, int x, int y, int z) noexcept {
	return IsPlanned(plan, x, y, z) && CellAt(plan, x, y, z).walkable;
}

void SetCell(GenerationPlan& plan, int x, int y, int z, MaterialRole material, bool walkable, uint16_t features = FeatureNone) {
	if (!plan.request.isAllowed(x, y, z)) {
		return;
	}
	auto& cell = CellAt(plan, x, y, z);
	cell.material = material;
	cell.walkable = walkable;
	cell.features = features;
}

void CarveDisk(GenerationPlan& plan, int cx, int cy, int z, int radius, MaterialRole role = MaterialRole::Path) {
	for (int y = cy - radius; y <= cy + radius; ++y) {
		for (int x = cx - radius; x <= cx + radius; ++x) {
			if (std::abs(x - cx) + std::abs(y - cy) <= radius + radius / 2) {
				SetCell(plan, x, y, z, role, true);
			}
		}
	}
}

void CarveLine(GenerationPlan& plan, LocalPoint from, LocalPoint to, int z, int width, MaterialRole role = MaterialRole::Path, uint64_t salt = 0) {
	// Even requested widths round up by one tile in the diamond raster, ensuring
	// the generated route is never narrower than the configured minimum.
	const int radius = std::max(0, width) / 2;
	int x = from.x;
	int y = from.y;
	const int dx = std::abs(to.x - x);
	const int sx = x < to.x ? 1 : -1;
	const int dy = -std::abs(to.y - y);
	const int sy = y < to.y ? 1 : -1;
	int error = dx + dy;
	while (true) {
		CarveDisk(plan, x, y, z, radius, role);
		if (x == to.x && y == to.y) {
			break;
		}
		const int twice = error * 2;
		if (twice >= dy) {
			error += dy;
			x += sx;
		}
		if (twice <= dx) {
			error += dx;
			y += sy;
		}
		if (salt != 0 && CellHash(plan.request.seed, x, y, z, salt) % 11 == 0) {
			CarveDisk(plan, x + static_cast<int>(CellHash(plan.request.seed, x, y, z, salt + 1) % 3) - 1, y, z, radius, role);
		}
	}
}

void CarveOrthogonalPath(GenerationPlan& plan, LocalPoint from, LocalPoint to, int z, int width, uint64_t salt) {
	const bool horizontalFirst = (CellHash(plan.request.seed, from.x ^ to.x, from.y ^ to.y, z, salt) & 1U) == 0;
	const LocalPoint corner = horizontalFirst ? LocalPoint { to.x, from.y } : LocalPoint { from.x, to.y };
	CarveLine(plan, from, corner, z, width, MaterialRole::Path, salt);
	CarveLine(plan, corner, to, z, width, MaterialRole::Path, salt + 1);
}

bool RoomsOverlap(const RoomRect& lhs, const RoomRect& rhs, int padding) noexcept {
	return lhs.x - padding < rhs.x + rhs.width && lhs.x + lhs.width + padding > rhs.x &&
		lhs.y - padding < rhs.y + rhs.height && lhs.y + lhs.height + padding > rhs.y;
}

bool RoomFitsMask(const GenerationRequest& request, const RoomRect& room, int z) noexcept {
	for (int y = room.y; y < room.y + room.height; ++y) {
		for (int x = room.x; x < room.x + room.width; ++x) {
			if (!request.isAllowed(x, y, z)) return false;
		}
	}
	return true;
}

std::vector<LocalPoint> WalkablePoints(const GenerationPlan& plan, int z) {
	std::vector<LocalPoint> points;
	for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
		for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
			if (IsWalkable(plan, x, y, z)) {
				points.push_back({ x, y });
			}
		}
	}
	return points;
}

std::vector<std::vector<LocalPoint>> ConnectedComponents(const GenerationPlan& plan, int z) {
	std::vector<std::vector<LocalPoint>> components;
	std::vector<uint8_t> visited(static_cast<size_t>(plan.request.width()) * static_cast<size_t>(plan.request.height()), uint8_t { 0 });
	const auto visitedIndex = [&](int x, int y) {
		return static_cast<size_t>(y - plan.request.minY) * static_cast<size_t>(plan.request.width()) + static_cast<size_t>(x - plan.request.minX);
	};
	const std::array<LocalPoint, 4> directions { LocalPoint { 1, 0 }, LocalPoint { -1, 0 }, LocalPoint { 0, 1 }, LocalPoint { 0, -1 } };
	for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
		for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
			if (!IsWalkable(plan, x, y, z)) {
				continue;
			}
			const size_t originIndex = visitedIndex(x, y);
			if (visited[originIndex] != 0) {
				continue;
			}
			components.emplace_back();
			std::queue<LocalPoint> pending;
			pending.push({ x, y });
			visited[originIndex] = 1;
			while (!pending.empty()) {
				const auto current = pending.front();
				pending.pop();
				components.back().push_back(current);
				for (const auto& direction : directions) {
					const int nx = current.x + direction.x;
					const int ny = current.y + direction.y;
					if (!IsWalkable(plan, nx, ny, z)) {
						continue;
					}
					const size_t index = visitedIndex(nx, ny);
					if (visited[index] == 0) {
						visited[index] = 1;
						pending.push({ nx, ny });
					}
				}
			}
		}
	}
	std::sort(components.begin(), components.end(), [](const auto& lhs, const auto& rhs) { return lhs.size() > rhs.size(); });
	return components;
}

bool CarveRouteAroundObstacles(GenerationPlan& plan, LocalPoint from, LocalPoint to, int z, int pathWidth, const std::stop_token& stopToken, uint64_t salt) {
	if (!plan.request.isAllowed(from.x, from.y, z) || !plan.request.isAllowed(to.x, to.y, z)) return false;
	const size_t width = static_cast<size_t>(plan.request.width());
	const size_t height = static_cast<size_t>(plan.request.height());
	const size_t cellCount = width * height;
	const auto index = [&](int x, int y) {
		return static_cast<size_t>(y - plan.request.minY) * width + static_cast<size_t>(x - plan.request.minX);
	};
	const size_t start = index(from.x, from.y);
	const size_t target = index(to.x, to.y);
	std::vector<int32_t> parent(cellCount, -1);
	std::queue<size_t> pending;
	parent[start] = static_cast<int32_t>(start);
	pending.push(start);
	static constexpr std::array<LocalPoint, 4> directions { LocalPoint { 1, 0 }, LocalPoint { 0, 1 }, LocalPoint { -1, 0 }, LocalPoint { 0, -1 } };
	size_t visited = 0;
	while (!pending.empty() && parent[target] < 0) {
		const size_t current = pending.front();
		pending.pop();
		if ((++visited & 4095U) == 0) CheckCancelled(stopToken);
		const int x = plan.request.minX + static_cast<int>(current % width);
		const int y = plan.request.minY + static_cast<int>(current / width);
		const size_t rotation = static_cast<size_t>(CellHash(plan.request.seed, x, y, z, salt) % directions.size());
		for (size_t directionIndex = 0; directionIndex < directions.size(); ++directionIndex) {
			const LocalPoint& direction = directions[(directionIndex + rotation) % directions.size()];
			const int nextX = x + direction.x;
			const int nextY = y + direction.y;
			if (!plan.request.isAllowed(nextX, nextY, z)) continue;
			const size_t next = index(nextX, nextY);
			if (parent[next] >= 0) continue;
			parent[next] = static_cast<int32_t>(current);
			pending.push(next);
		}
	}
	if (parent[target] < 0) return false;
	for (size_t current = target;; current = static_cast<size_t>(parent[current])) {
		const int x = plan.request.minX + static_cast<int>(current % width);
		const int y = plan.request.minY + static_cast<int>(current / width);
		CarveDisk(plan, x, y, z, std::max(0, pathWidth) / 2, MaterialRole::Path);
		if (current == start) break;
	}
	return true;
}

void RepairConnectivity(GenerationPlan& plan, int z, const std::stop_token& stopToken) {
	auto components = ConnectedComponents(plan, z);
	// Set final count, not accumulate
	plan.statistics.connectedRegions = components.size();
	if (components.size() <= 1) {
		return;
	}
	if (!plan.request.parameters.autoFixConnectivity) {
		plan.issues.push_back({ IssueSeverity::Warning, "The generated floor contains disconnected walkable regions.", -1, -1, z });
		return;
	}
	while (components.size() > 1) {
		CheckCancelled(stopToken);
		const size_t componentCountBeforeRepair = components.size();
		const auto& main = components.front();
		const auto& other = components.back();
		LocalPoint bestMain = main.front();
		LocalPoint bestOther = other.front();
		int bestDistance = std::numeric_limits<int>::max();
		const size_t mainStep = std::max<size_t>(1, main.size() / 256);
		const size_t otherStep = std::max<size_t>(1, other.size() / 128);
		for (size_t i = 0; i < main.size(); i += mainStep) {
			for (size_t j = 0; j < other.size(); j += otherStep) {
				const int distance = std::abs(main[i].x - other[j].x) + std::abs(main[i].y - other[j].y);
				if (distance < bestDistance) {
					bestDistance = distance;
					bestMain = main[i];
					bestOther = other[j];
				}
			}
		}
		if (!CarveRouteAroundObstacles(plan, bestMain, bestOther, z, std::max(1, plan.request.parameters.pathWidth), stopToken, 7001 + plan.statistics.repairedConnections)) {
			plan.issues.push_back({ IssueSeverity::Warning, "Disconnected selection islands cannot be joined without crossing protected or unselected tiles.", -1, -1, z });
			break;
		}
		++plan.statistics.repairedConnections;
		components = ConnectedComponents(plan, z);
		if (components.size() >= componentCountBeforeRepair) {
			plan.issues.push_back({ IssueSeverity::Warning, "Disconnected selection islands cannot be joined without modifying tiles outside the allowed mask.", -1, -1, z });
			break;
		}
	}
	// Update final count after all repairs
	plan.statistics.connectedRegions = components.size();
}

void AssignEntrances(GenerationPlan& plan, int z) {
	for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
		for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
			if (plan.request.isAllowed(x, y, z)) {
				CellAt(plan, x, y, z).features &= static_cast<uint16_t>(~(FeatureEntrance | FeatureExit));
			}
		}
	}
	auto points = WalkablePoints(plan, z);
	if (points.empty()) {
		plan.issues.push_back({ IssueSeverity::Error, "No walkable tile was generated on this floor.", -1, -1, z });
		return;
	}
	const auto entrance = *std::min_element(points.begin(), points.end(), [](const auto& a, const auto& b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); });
	const auto exit = *std::max_element(points.begin(), points.end(), [](const auto& a, const auto& b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); });
	CellAt(plan, entrance.x, entrance.y, z).features |= FeatureEntrance;
	CellAt(plan, exit.x, exit.y, z).features |= FeatureExit;
}

void PlanAlignedFloorTransitions(GenerationPlan& plan) {
	if (plan.request.floorCount() <= 1) return;
	const int centerX = (plan.request.minX + plan.request.maxX) / 2;
	const int centerY = (plan.request.minY + plan.request.maxY) / 2;
	for (int lowerZ = plan.request.maxZ; lowerZ > plan.request.minZ; --lowerZ) {
		const int upperZ = lowerZ - 1;
		bool alreadyAligned = false;
		bool found = false;
		LocalPoint best {};
		int bestDistance = std::numeric_limits<int>::max();
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!IsWalkable(plan, x, y, lowerZ) || !IsWalkable(plan, x, y, upperZ)) continue;
				const auto& lower = CellAt(plan, x, y, lowerZ);
				const auto& upper = CellAt(plan, x, y, upperZ);
				if ((lower.features & FeatureTransition) != 0 && (upper.features & FeatureTransition) != 0) {
					alreadyAligned = true;
					break;
				}
				const int distance = std::abs(x - centerX) + std::abs(y - centerY);
				if (distance < bestDistance || (distance == bestDistance && std::tie(x, y) < std::tie(best.x, best.y))) {
					bestDistance = distance;
					best = { x, y };
					found = true;
				}
			}
			if (alreadyAligned) break;
		}
		if (alreadyAligned) continue;
		if (!found) {
			plan.issues.push_back({ IssueSeverity::Warning, "No shared walkable position could align this pair of floors.", centerX, centerY, lowerZ });
			continue;
		}
		CellAt(plan, best.x, best.y, lowerZ).features |= FeatureTransition;
		CellAt(plan, best.x, best.y, upperZ).features |= FeatureTransition;
	}
}

void MarkRockBoundaries(GenerationPlan& plan, int z) {
	static constexpr std::array<LocalPoint, 4> directions { LocalPoint { 1, 0 }, LocalPoint { -1, 0 }, LocalPoint { 0, 1 }, LocalPoint { 0, -1 } };
	for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
		for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
			if (!plan.request.isAllowed(x, y, z)) continue;
			auto& cell = CellAt(plan, x, y, z);
			if (cell.material != MaterialRole::Rock) continue;
			cell.features &= ~FeatureWall;
			if (std::any_of(directions.begin(), directions.end(), [&](const auto& direction) { return IsWalkable(plan, x + direction.x, y + direction.y, z); })) {
				cell.features |= FeatureWall;
			}
		}
	}
}

void SmoothNaturalTerrain(GenerationPlan& plan, int z, const std::stop_token& stopToken) {
	const int passes = std::clamp(plan.request.parameters.smoothingPasses, 0, 8);
	if (passes == 0) return;
	const int width = plan.request.width();
	const int height = plan.request.height();
	std::vector<MaterialRole> next(static_cast<size_t>(width) * static_cast<size_t>(height), MaterialRole::None);
	const auto index = [&](int x, int y) {
		return static_cast<size_t>(y - plan.request.minY) * static_cast<size_t>(width) + static_cast<size_t>(x - plan.request.minX);
	};
	for (int pass = 0; pass < passes; ++pass) {
		CheckCancelled(stopToken);
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!plan.request.isAllowed(x, y, z)) {
					next[index(x, y)] = MaterialRole::None;
					continue;
				}
				const MaterialRole current = CellAt(plan, x, y, z).material;
				std::array<uint8_t, static_cast<size_t>(MaterialRole::Count)> counts {};
				for (int oy = -1; oy <= 1; ++oy) {
					for (int ox = -1; ox <= 1; ++ox) {
						if ((ox == 0 && oy == 0) || !plan.request.isAllowed(x + ox, y + oy, z)) continue;
						const MaterialRole neighbour = CellAt(plan, x + ox, y + oy, z).material;
						if (neighbour != MaterialRole::None) ++counts[static_cast<size_t>(neighbour)];
					}
				}
				const auto majority = std::max_element(counts.begin(), counts.end());
				const MaterialRole candidate = static_cast<MaterialRole>(std::distance(counts.begin(), majority));
				const uint8_t currentCount = counts[static_cast<size_t>(current)];
				next[index(x, y)] = *majority >= 5 && currentCount <= 2 ? candidate : current;
			}
		}
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!plan.request.isAllowed(x, y, z)) continue;
				auto& cell = CellAt(plan, x, y, z);
				cell.material = next[index(x, y)];
				cell.walkable = cell.material != MaterialRole::Water && cell.material != MaterialRole::Rock && cell.material != MaterialRole::None;
			}
		}
	}
}

void GenerateNatural(GenerationPlan& plan, const std::stop_token& stopToken) {
	const auto preset = plan.request.preset;
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			CheckCancelled(stopToken);
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!plan.request.isAllowed(x, y, z)) {
					continue;
				}
				const double n = FractalNoise(plan.request, x, y, z, 1000 + static_cast<uint8_t>(preset));
				const double detail = FractalNoise(plan.request, x * 2, y * 2, z, 1200) * 0.18;
				MaterialRole role = MaterialRole::Base;

				if (preset == Preset::Island) {
					const double nx = (2.0 * (x - plan.request.minX) / std::max(1, plan.request.width() - 1)) - 1.0;
					const double ny = (2.0 * (y - plan.request.minY) / std::max(1, plan.request.height() - 1)) - 1.0;
					const double island = 0.72 - std::sqrt(nx * nx + ny * ny) + n * 0.38;
					if (island < -0.05) {
						role = MaterialRole::Water;
					} else if (island < 0.12) {
						role = MaterialRole::Secondary;
					} else if (island > 0.65) {
						role = MaterialRole::Rock;
					}
				} else if (preset == Preset::Coastline) {
					const double shore = static_cast<double>(x - plan.request.minX) / std::max(1, plan.request.width() - 1) + n * 0.20;
					if (shore < 0.36) {
						role = MaterialRole::Water;
					} else if (shore < 0.47) {
						role = MaterialRole::Secondary;
					}
				} else if (preset == Preset::Desert) {
					role = n + detail < -0.38 ? MaterialRole::Water : (n > 0.48 ? MaterialRole::Rock : (n > 0.12 ? MaterialRole::Accent : MaterialRole::Secondary));
				} else if (preset == Preset::SnowArea) {
					role = n < -0.40 ? MaterialRole::Water : (n > 0.35 ? MaterialRole::Rock : MaterialRole::Accent);
				} else if (preset == Preset::Swamp) {
					role = n + detail < 0.05 ? MaterialRole::Water : (n > 0.43 ? MaterialRole::Base : MaterialRole::Accent);
				} else {
					const double value = n + detail;
					if (value < plan.request.parameters.waterLevel) {
						role = MaterialRole::Water;
					} else if (value < plan.request.parameters.waterLevel + 0.14) {
						role = MaterialRole::Secondary;
					} else if (value > 0.50) {
						role = MaterialRole::Rock;
					} else if (preset == Preset::Forest && value > 0.08) {
						role = MaterialRole::Accent;
					}
				}
				const bool walkable = role != MaterialRole::Water && role != MaterialRole::Rock;
				SetCell(plan, x, y, z, role, walkable);
			}
		}
		SmoothNaturalTerrain(plan, z, stopToken);
		MarkRockBoundaries(plan, z);
		RepairConnectivity(plan, z, stopToken);
		AssignEntrances(plan, z);
	}
}

void GenerateRiver(GenerationPlan& plan, const std::stop_token& stopToken) {
	GenerateNatural(plan, stopToken);
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		LocalPoint previous { plan.request.minX, (plan.request.minY + plan.request.maxY) / 2 };
		const int segment = std::max(4, plan.request.width() / 10);
		for (int x = plan.request.minX; x <= plan.request.maxX; x += segment) {
			const double wave = FractalNoise(plan.request, x, z * 17, z, 8111);
			const int y = std::clamp((plan.request.minY + plan.request.maxY) / 2 + static_cast<int>(wave * plan.request.height() * 0.32), plan.request.minY, plan.request.maxY);
			CarveLine(plan, previous, { std::min(x, plan.request.maxX), y }, z, std::max(1, plan.request.parameters.pathWidth + 1), MaterialRole::Water, 8123);
			previous = { std::min(x, plan.request.maxX), y };
		}
		CarveLine(plan, previous, { plan.request.maxX, (plan.request.minY + plan.request.maxY) / 2 }, z, std::max(1, plan.request.parameters.pathWidth + 1), MaterialRole::Water, 8127);
		for (auto& cell : plan.cells) {
			if (cell.z == z && cell.material == MaterialRole::Water) {
				cell.walkable = false;
			}
		}
		MarkRockBoundaries(plan, z);
		RepairConnectivity(plan, z, stopToken);
		AssignEntrances(plan, z);
	}
}

void GenerateCave(GenerationPlan& plan, const std::stop_token& stopToken) {
	const int width = plan.request.width();
	const int height = plan.request.height();
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		std::vector<uint8_t> open(static_cast<size_t>(width) * height, 0);
		const auto index = [&](int x, int y) { return static_cast<size_t>(y - plan.request.minY) * width + static_cast<size_t>(x - plan.request.minX); };
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (plan.request.isAllowed(x, y, z)) {
					const bool edge = x == plan.request.minX || x == plan.request.maxX || y == plan.request.minY || y == plan.request.maxY;
					open[index(x, y)] = !edge && UnitHash(plan.request.seed, x, y, z, 2201) > 0.44 ? 1 : 0;
				}
			}
		}
		for (int pass = 0; pass < std::clamp(plan.request.parameters.smoothingPasses, 0, 8); ++pass) {
			CheckCancelled(stopToken);
			auto next = open;
			for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
				for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
					if (!plan.request.isAllowed(x, y, z)) {
						continue;
					}
					int neighbours = 0;
					for (int oy = -1; oy <= 1; ++oy) {
						for (int ox = -1; ox <= 1; ++ox) {
							if ((ox != 0 || oy != 0) && plan.request.isAllowed(x + ox, y + oy, z) && open[index(x + ox, y + oy)] != 0) {
								++neighbours;
							}
						}
					}
					next[index(x, y)] = neighbours >= 5 || (open[index(x, y)] && neighbours >= 4) ? 1 : 0;
				}
			}
			open.swap(next);
		}
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (plan.request.isAllowed(x, y, z)) {
					SetCell(plan, x, y, z, open[index(x, y)] ? MaterialRole::Base : MaterialRole::Rock, open[index(x, y)] != 0, open[index(x, y)] ? FeatureNone : FeatureWall);
				}
			}
		}
		RepairConnectivity(plan, z, stopToken);
		MarkRockBoundaries(plan, z);
		AssignEntrances(plan, z);
	}
}

void GenerateDungeon(GenerationPlan& plan, const std::stop_token& stopToken, bool ruins = false) {
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (plan.request.isAllowed(x, y, z)) {
					SetCell(plan, x, y, z, MaterialRole::Rock, false, FeatureWall);
				}
			}
		}

		std::vector<RoomRect> rooms;
		const int requestedRooms = std::clamp(plan.request.parameters.roomCount, 2, 128);
		rooms.reserve(static_cast<size_t>(requestedRooms));
		const int maximumRoomWidth = std::min(plan.request.parameters.roomMaxSize, plan.request.width() - 2);
		const int maximumRoomHeight = std::min(plan.request.parameters.roomMaxSize, plan.request.height() - 2);
		const int minimumRoom = std::max(3, std::min({ plan.request.parameters.roomMinSize, maximumRoomWidth, maximumRoomHeight }));
		const int maximumAttempts = requestedRooms * 16;
		for (int attempt = 0; attempt < maximumAttempts && static_cast<int>(rooms.size()) < requestedRooms; ++attempt) {
			CheckCancelled(stopToken);
			if (maximumRoomWidth < 3 || maximumRoomHeight < 3) break;
			const int rw = minimumRoom + static_cast<int>(CellHash(plan.request.seed, attempt, z, 0, 3101) % static_cast<uint64_t>(std::max(1, maximumRoomWidth - minimumRoom + 1)));
			const int rh = minimumRoom + static_cast<int>(CellHash(plan.request.seed, attempt, z, 0, 3103) % static_cast<uint64_t>(std::max(1, maximumRoomHeight - minimumRoom + 1)));
			const int rangeX = std::max(1, plan.request.width() - rw - 1);
			const int rangeY = std::max(1, plan.request.height() - rh - 1);
			RoomRect candidate {
				plan.request.minX + 1 + static_cast<int>(CellHash(plan.request.seed, attempt, z, 0, 3109) % static_cast<uint64_t>(rangeX)),
				plan.request.minY + 1 + static_cast<int>(CellHash(plan.request.seed, attempt, z, 0, 3119) % static_cast<uint64_t>(rangeY)),
				rw,
				rh,
			};
			if (!RoomFitsMask(plan.request, candidate, z)) continue;
			if (std::any_of(rooms.begin(), rooms.end(), [&](const RoomRect& room) { return RoomsOverlap(candidate, room, 1); })) continue;
			rooms.push_back(candidate);
		}

		std::vector<LocalPoint> centers;
		centers.reserve(rooms.size());
		for (size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
			const RoomRect& room = rooms[roomIndex];
			const LocalPoint center = room.center();
			for (int y = room.y; y < room.y + room.height; ++y) {
				for (int x = room.x; x < room.x + room.width; ++x) {
					const bool eroded = ruins && (x != center.x || y != center.y) && UnitHash(plan.request.seed, x, y, z, 3137) < 0.10;
					if (!eroded) SetCell(plan, x, y, z, roomIndex % 4 == 0 ? MaterialRole::Secondary : MaterialRole::Base, true);
				}
			}
			centers.push_back(center);
		}
		plan.statistics.roomsPlaced += rooms.size();

		if (centers.empty()) {
			bool found = false;
			LocalPoint center {};
			for (int y = plan.request.minY; y <= plan.request.maxY && !found; ++y) {
				for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
					if (plan.request.isAllowed(x, y, z)) {
						center = { x, y };
						found = true;
						break;
					}
				}
			}
			if (!found) continue;
			CarveDisk(plan, center.x, center.y, z, 2, MaterialRole::Base);
			centers.push_back(center);
		}
		std::vector<bool> connected(centers.size(), false);
		std::set<std::pair<size_t, size_t>> connections;
		connected[0] = true;
		for (size_t connectedCount = 1; connectedCount < centers.size(); ++connectedCount) {
			int bestDistance = std::numeric_limits<int>::max();
			size_t from = 0;
			size_t to = 0;
			for (size_t i = 0; i < centers.size(); ++i) {
				if (!connected[i]) continue;
				for (size_t j = 0; j < centers.size(); ++j) {
					if (connected[j]) continue;
					const int distance = std::abs(centers[i].x - centers[j].x) + std::abs(centers[i].y - centers[j].y);
					if (distance < bestDistance) {
						bestDistance = distance;
						from = i;
						to = j;
					}
				}
			}
			CarveOrthogonalPath(plan, centers[from], centers[to], z, std::max(1, plan.request.parameters.pathWidth), 3201 + to);
			connections.emplace(std::min(from, to), std::max(from, to));
			connected[to] = true;
		}
		for (int loop = 0, attempts = 0; loop < plan.request.parameters.loops && centers.size() > 2 && attempts < plan.request.parameters.loops * 8 + 8; ++attempts) {
			const size_t from = CellHash(plan.request.seed, attempts, z, 0, 3221) % centers.size();
			const size_t to = CellHash(plan.request.seed, attempts, z, 0, 3229) % centers.size();
			if (from == to || connections.contains({ std::min(from, to), std::max(from, to) })) continue;
			CarveOrthogonalPath(plan, centers[from], centers[to], z, std::max(1, plan.request.parameters.pathWidth), 3233 + loop * 2);
			connections.emplace(std::min(from, to), std::max(from, to));
			++loop;
		}
		RepairConnectivity(plan, z, stopToken);
		MarkRockBoundaries(plan, z);
		for (const LocalPoint& center : centers) {
			if (IsWalkable(plan, center.x, center.y, z)) CellAt(plan, center.x, center.y, z).features |= FeaturePointOfInterest;
		}
		AssignEntrances(plan, z);
	}
}

void GenerateHunt(GenerationPlan& plan, const std::stop_token& stopToken) {
	GenerateCave(plan, stopToken);
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		const LocalPoint center { (plan.request.minX + plan.request.maxX) / 2, (plan.request.minY + plan.request.maxY) / 2 };
		const int radiusX = std::max(3, plan.request.width() / 3);
		const int radiusY = std::max(3, plan.request.height() / 3);
		const int chamberCount = std::clamp(plan.request.parameters.roomCount, 4, 20);
		std::vector<LocalPoint> chambers;
		chambers.reserve(static_cast<size_t>(chamberCount));
		CarveDisk(plan, center.x, center.y, z, std::max(2, std::min(plan.request.width(), plan.request.height()) / 12), MaterialRole::Secondary);
		for (int chamber = 0; chamber < chamberCount; ++chamber) {
			CheckCancelled(stopToken);
			const double angle = chamber * 2.0 * 3.14159265358979323846 / chamberCount;
			LocalPoint current {
				std::clamp(center.x + static_cast<int>(std::cos(angle) * radiusX), plan.request.minX, plan.request.maxX),
				std::clamp(center.y + static_cast<int>(std::sin(angle) * radiusY), plan.request.minY, plan.request.maxY),
			};
			if (!plan.request.isAllowed(current.x, current.y, z)) continue;
			const int chamberRadius = 2 + static_cast<int>(CellHash(plan.request.seed, chamber, z, 0, 4099) % 4U);
			CarveDisk(plan, current.x, current.y, z, chamberRadius, chamber % 3 == 0 ? MaterialRole::Secondary : MaterialRole::Base);
			chambers.push_back(current);
			CarveOrthogonalPath(plan, center, current, z, std::max(2, plan.request.parameters.pathWidth), 4101 + chamber * 3);
		}
		for (size_t chamber = 1; chamber < chambers.size(); ++chamber) {
			CarveLine(plan, chambers[chamber - 1], chambers[chamber], z, std::max(2, plan.request.parameters.pathWidth), MaterialRole::Path, 4201 + chamber);
		}
		if (chambers.size() > 2) CarveLine(plan, chambers.back(), chambers.front(), z, std::max(2, plan.request.parameters.pathWidth), MaterialRole::Path, 4301);
		plan.statistics.roomsPlaced += chambers.size() + 1;
		RepairConnectivity(plan, z, stopToken);
		MarkRockBoundaries(plan, z);
		if (IsWalkable(plan, center.x, center.y, z)) CellAt(plan, center.x, center.y, z).features |= FeaturePointOfInterest;
		for (const LocalPoint& chamber : chambers) {
			if (IsWalkable(plan, chamber.x, chamber.y, z)) CellAt(plan, chamber.x, chamber.y, z).features |= FeaturePointOfInterest;
		}
		AssignEntrances(plan, z);
	}
}

void GenerateCity(GenerationPlan& plan, const std::stop_token& stopToken, bool village) {
	const int spacing = std::max(6, plan.request.parameters.secondaryRoadSpacing + (village ? 4 : 0));
	const int roadWidth = std::max(1, plan.request.parameters.pathWidth);
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			CheckCancelled(stopToken);
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!plan.request.isAllowed(x, y, z)) continue;
				const bool mainHorizontal = std::abs(y - (plan.request.minY + plan.request.maxY) / 2) < roadWidth;
				const bool mainVertical = std::abs(x - (plan.request.minX + plan.request.maxX) / 2) < roadWidth;
				const int localX = x - plan.request.minX;
				const int localY = y - plan.request.minY;
				const int gridX = localX % spacing;
				const int gridY = localY % spacing;
				const int blockX = localX / spacing;
				const int blockY = localY / spacing;
				const bool sideRoad = gridX < roadWidth || gridY < roadWidth;
				const bool road = mainHorizontal || mainVertical || (!village && sideRoad) || (village && sideRoad && UnitHash(plan.request.seed, blockX, blockY, z, 5101) > 0.30);
				if (road) {
					SetCell(plan, x, y, z, MaterialRole::Path, true);
				} else {
					const bool green = UnitHash(plan.request.seed, blockX, blockY, z, 5113) < (village ? 0.46 : 0.16);
					if (green) {
						SetCell(plan, x, y, z, MaterialRole::Base, true);
					} else {
						const bool verticalBoundary = gridX == roadWidth || gridX == spacing - 1;
						const bool horizontalBoundary = gridY == roadWidth || gridY == spacing - 1;
						const bool doorway = (verticalBoundary && gridY == spacing / 2) || (horizontalBoundary && gridX == spacing / 2);
						const bool wall = (verticalBoundary || horizontalBoundary) && !doorway;
						SetCell(plan, x, y, z, MaterialRole::Secondary, !wall, wall ? FeatureWall : (doorway ? FeatureEntrance : FeatureNone));
					}
				}
			}
		}
		const int plazaRadius = std::max(2, std::min(plan.request.width(), plan.request.height()) / 12);
		const LocalPoint plaza { (plan.request.minX + plan.request.maxX) / 2, (plan.request.minY + plan.request.maxY) / 2 };
		CarveDisk(plan, plaza.x, plaza.y, z, plazaRadius, MaterialRole::Accent);
		if (plan.request.isAllowed(plaza.x, plaza.y, z)) CellAt(plan, plaza.x, plaza.y, z).features |= FeaturePointOfInterest;
		RepairConnectivity(plan, z, stopToken);
		AssignEntrances(plan, z);
	}
}

void GenerateMountain(GenerationPlan& plan, const std::stop_token& stopToken) {
	const int floors = plan.request.floorCount();
	for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) {
		const int level = plan.request.maxZ - z;
		const double threshold = 0.02 + static_cast<double>(level) / std::max(1, floors) * 0.48;
		for (int y = plan.request.minY; y <= plan.request.maxY; ++y) {
			CheckCancelled(stopToken);
			for (int x = plan.request.minX; x <= plan.request.maxX; ++x) {
				if (!plan.request.isAllowed(x, y, z)) continue;
				const double n = FractalNoise(plan.request, x, y, plan.request.maxZ, 6101);
				if (n < threshold && level > 0) {
					SetCell(plan, x, y, z, MaterialRole::None, false);
				} else if (n > threshold + 0.28) {
					SetCell(plan, x, y, z, MaterialRole::Rock, false, FeatureWall);
				} else {
					SetCell(plan, x, y, z, level == floors - 1 ? MaterialRole::Accent : MaterialRole::Base, true);
				}
			}
		}
		const int pathY = std::clamp((plan.request.minY + plan.request.maxY) / 2 + level * 2, plan.request.minY, plan.request.maxY);
		CarveLine(plan, { plan.request.minX, pathY }, { plan.request.maxX, pathY }, z, std::max(1, plan.request.parameters.pathWidth), MaterialRole::Path, 6121 + level);
		RepairConnectivity(plan, z, stopToken);
		MarkRockBoundaries(plan, z);
		AssignEntrances(plan, z);
	}
}

void PlanDecorations(GenerationPlan& plan) {
	const auto density = std::clamp(plan.request.parameters.decorationDensity, 0, 100);
	for (auto& cell : plan.cells) {
		if (cell.material == MaterialRole::None || !cell.walkable || cell.material == MaterialRole::Path || cell.material == MaterialRole::Water || (cell.features & kStructureFeatures) != 0) {
			continue;
		}
		int adjustedDensity = density;
		if (plan.request.preset == Preset::Forest) adjustedDensity = std::min(100, density + 25);
		if (plan.request.preset == Preset::City) adjustedDensity /= 3;
		if (UnitHash(plan.request.seed, cell.x, cell.y, cell.z, 9001) * 100.0 < adjustedDensity) {
			cell.features |= FeatureDecoration;
		}
	}
}

void ComputeBlendWeights(GenerationPlan& plan) {
	const int margin = plan.request.edgeBlending == EdgeBlending::None ? 0 : std::max(1, plan.request.parameters.edgeMargin);
	for (auto& cell : plan.cells) {
		if (cell.material == MaterialRole::None || margin == 0) {
			cell.blendWeight = 255;
			continue;
		}
		int distance = margin;
		for (int radius = 1; radius <= margin; ++radius) {
			if (!plan.request.isAllowed(cell.x - radius, cell.y, cell.z) || !plan.request.isAllowed(cell.x + radius, cell.y, cell.z) || !plan.request.isAllowed(cell.x, cell.y - radius, cell.z) || !plan.request.isAllowed(cell.x, cell.y + radius, cell.z)) {
				distance = radius - 1;
				break;
			}
		}
		const double normalized = static_cast<double>(distance + 1) / static_cast<double>(margin + 1);
		const double shaped = plan.request.edgeBlending == EdgeBlending::Strong ? normalized * normalized : normalized;
		cell.blendWeight = static_cast<uint8_t>(std::clamp(shaped * 255.0, 1.0, 255.0));
	}
}

void CollectStatistics(GenerationPlan& plan) {
	plan.statistics.allowedTiles = static_cast<size_t>(std::count(plan.request.allowedCells.begin(), plan.request.allowedCells.end(), uint8_t { 1 }));
	plan.statistics.protectedTiles = static_cast<size_t>(std::count(plan.request.protectedCells.begin(), plan.request.protectedCells.end(), uint8_t { 1 }));
	plan.statistics.plannedTiles = 0;
	plan.statistics.walkableTiles = 0;
	plan.statistics.wallTiles = 0;
	plan.statistics.decorationTiles = 0;
	plan.statistics.pointOfInterestTiles = 0;
	for (const auto& cell : plan.cells) {
		if (cell.material == MaterialRole::None) continue;
		++plan.statistics.plannedTiles;
		if (cell.walkable) ++plan.statistics.walkableTiles;
		if ((cell.features & FeatureWall) != 0) ++plan.statistics.wallTiles;
		if ((cell.features & FeatureDecoration) != 0) ++plan.statistics.decorationTiles;
		if ((cell.features & FeaturePointOfInterest) != 0) ++plan.statistics.pointOfInterestTiles;
	}
	if (plan.statistics.connectedRegions == 0) {
		for (int z = plan.request.minZ; z <= plan.request.maxZ; ++z) plan.statistics.connectedRegions += ConnectedComponents(plan, z).size();
	}
	plan.statistics.approximateBytes = sizeof(GenerationPlan) + plan.cells.capacity() * sizeof(GeneratedCell) + plan.request.allowedCells.capacity() + plan.request.protectedCells.capacity();
}

std::vector<std::string> Tokenize(const std::string& text) {
	std::vector<std::string> tokens;
	std::string token;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-') {
			token.push_back(static_cast<char>(std::tolower(c)));
		} else if (!token.empty()) {
			tokens.push_back(token);
			token.clear();
		}
	}
	if (!token.empty()) tokens.push_back(token);
	return tokens;
}

} // namespace

int GenerationRequest::width() const noexcept {
	if (maxX < minX) return 0;
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(maxX) - minX + 1, std::numeric_limits<int>::max()));
}

int GenerationRequest::height() const noexcept {
	if (maxY < minY) return 0;
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(maxY) - minY + 1, std::numeric_limits<int>::max()));
}

int GenerationRequest::floorCount() const noexcept {
	if (maxZ < minZ) return 0;
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(maxZ) - minZ + 1, std::numeric_limits<int>::max()));
}

size_t GenerationRequest::cellCount() const noexcept {
	const uint64_t count = static_cast<uint64_t>(width()) * static_cast<uint64_t>(height()) * static_cast<uint64_t>(floorCount());
	return count > std::numeric_limits<size_t>::max() ? std::numeric_limits<size_t>::max() : static_cast<size_t>(count);
}

bool GenerationRequest::isAllowed(int x, int y, int z) const noexcept {
	return isSelected(x, y, z) && !isProtected(x, y, z);
}

bool GenerationRequest::isSelected(int x, int y, int z) const noexcept {
	if (x < minX || x > maxX || y < minY || y > maxY || z < minZ || z > maxZ) return false;
	const size_t index = RequestIndex(*this, x, y, z);
	return index < allowedCells.size() && allowedCells[index] != 0;
}

bool GenerationRequest::isProtected(int x, int y, int z) const noexcept {
	if (x < minX || x > maxX || y < minY || y > maxY || z < minZ || z > maxZ) return false;
	const size_t index = RequestIndex(*this, x, y, z);
	return index < protectedCells.size() && protectedCells[index] != 0;
}

bool GenerationPlan::hasErrors() const noexcept {
	return std::any_of(issues.begin(), issues.end(), [](const auto& issue) { return issue.severity == IssueSeverity::Error; });
}

const GeneratedCell* GenerationPlan::find(int x, int y, int z) const noexcept {
	if (!request.isAllowed(x, y, z)) return nullptr;
	const size_t index = PlanIndex(request, x, y, z);
	return index < cells.size() ? &cells[index] : nullptr;
}

const char* PresetName(Preset preset) noexcept {
	static constexpr std::array<const char*, static_cast<size_t>(Preset::Count)> names {
		"Natural Landscape", "Forest", "Mountain", "Cave", "Dungeon", "Hunting Area", "City", "Village",
		"Desert", "Snow Area", "Swamp", "Island", "Coastline", "River", "Ruins", "Custom"
	};
	const size_t index = static_cast<size_t>(preset);
	return index < names.size() ? names[index] : "Unknown";
}

const char* MaterialRoleName(MaterialRole role) noexcept {
	static constexpr std::array<const char*, static_cast<size_t>(MaterialRole::Count)> names { "None", "Base", "Secondary", "Water", "Path", "Rock", "Accent" };
	const size_t index = static_cast<size_t>(role);
	return index < names.size() ? names[index] : "Unknown";
}

const char* GenerationStageName(GenerationStage stage) noexcept {
	static constexpr std::array<const char*, 7> names { "Reading selection", "Building generation masks", "Generating terrain", "Creating paths and structures", "Planning decorations", "Validating connectivity", "Completed" };
	const size_t index = static_cast<size_t>(stage);
	return index < names.size() ? names[index] : "Working";
}

bool ValidateRequest(const GenerationRequest& request, std::vector<ValidationIssue>& issues) {
	if (request.width() <= 0 || request.height() <= 0 || request.floorCount() <= 0) {
		issues.push_back({ IssueSeverity::Error, "The generation rectangle is empty." });
	}
	if (request.minX < 0 || request.minY < 0 || request.maxX > 65535 || request.maxY > 65535 || request.minZ < 0 || request.maxZ > 15) {
		issues.push_back({ IssueSeverity::Error, "The generation rectangle is outside the supported map coordinates." });
	}
	if (request.allowedCells.size() != request.cellCount()) {
		issues.push_back({ IssueSeverity::Error, "The selection mask size does not match the generation rectangle." });
	} else if (std::none_of(request.allowedCells.begin(), request.allowedCells.end(), [](uint8_t value) { return value != 0; })) {
		issues.push_back({ IssueSeverity::Error, "The selection mask contains no tiles." });
	}
	if (!request.protectedCells.empty() && request.protectedCells.size() != request.cellCount()) {
		issues.push_back({ IssueSeverity::Error, "The protected-content mask size does not match the generation rectangle." });
	} else if (request.allowedCells.size() == request.cellCount()) {
		bool hasEditableCell = false;
		for (int z = request.minZ; z <= request.maxZ && !hasEditableCell; ++z) {
			for (int y = request.minY; y <= request.maxY && !hasEditableCell; ++y) {
				for (int x = request.minX; x <= request.maxX; ++x) {
					if (request.isAllowed(x, y, z)) {
						hasEditableCell = true;
						break;
					}
				}
			}
		}
		if (!hasEditableCell) issues.push_back({ IssueSeverity::Error, "Every selected tile contains protected content; no editable generation cell remains." });
	}
	if (request.cellCount() > MaximumPlanCells) {
		issues.push_back({ IssueSeverity::Error, "The requested area exceeds the configured memory safety limit of " + std::to_string(MaximumPlanCells) + " cells." });
	}
	if (static_cast<size_t>(request.preset) >= static_cast<size_t>(Preset::Count)) {
		issues.push_back({ IssueSeverity::Error, "The requested generation preset is invalid." });
	}
	if (static_cast<uint8_t>(request.replacementMode) > static_cast<uint8_t>(ReplacementMode::BlendWithExisting) || static_cast<uint8_t>(request.edgeBlending) > static_cast<uint8_t>(EdgeBlending::Strong)) {
		issues.push_back({ IssueSeverity::Error, "The requested replacement or edge policy is invalid." });
	}
	if (request.parameters.roomMinSize > request.parameters.roomMaxSize) {
		issues.push_back({ IssueSeverity::Error, "Minimum room size cannot exceed maximum room size." });
	}
	const auto& parameters = request.parameters;
	if (!std::isfinite(parameters.noiseScale) || !std::isfinite(parameters.persistence) || !std::isfinite(parameters.lacunarity) || !std::isfinite(parameters.waterLevel) || !std::isfinite(parameters.irregularity) ||
		parameters.noiseScale < 1.0 || parameters.octaves < 1 || parameters.octaves > 10 || parameters.persistence < 0.05 || parameters.persistence > 0.95 || parameters.lacunarity < 1.1 || parameters.lacunarity > 4.0 ||
		parameters.waterLevel < -1.0 || parameters.waterLevel > 1.0 || parameters.irregularity < 0.0 || parameters.irregularity > 2.0 || parameters.smoothingPasses < 0 || parameters.smoothingPasses > 8 ||
		parameters.decorationDensity < 0 || parameters.decorationDensity > 100 || parameters.pathWidth < 1 || parameters.pathWidth > 12 || parameters.roomCount < 2 || parameters.roomCount > 128 ||
		parameters.roomMinSize < 3 || parameters.roomMinSize > 64 || parameters.roomMaxSize < 3 || parameters.roomMaxSize > 128 || parameters.loops < 0 || parameters.loops > 32 ||
		parameters.secondaryRoadSpacing < 6 || parameters.secondaryRoadSpacing > 40 || parameters.edgeMargin < 1 || parameters.edgeMargin > 12) {
		issues.push_back({ IssueSeverity::Error, "One or more generation parameters are outside the supported range." });
	}
	return std::none_of(issues.begin(), issues.end(), [](const auto& issue) { return issue.severity == IssueSeverity::Error; });
}

GenerationPlan Generate(const GenerationRequest& request, std::stop_token stopToken, const ProgressCallback& progress) {
	GenerationPlan plan;
	plan.request = request;
	Report(progress, 0, GenerationStage::ReadingSelection);
	if (!ValidateRequest(plan.request, plan.issues)) {
		return plan;
	}
	CheckCancelled(stopToken);
	Report(progress, 10, GenerationStage::BuildingMasks);
	plan.cells.resize(request.cellCount());
	for (int z = request.minZ; z <= request.maxZ; ++z) {
		for (int y = request.minY; y <= request.maxY; ++y) {
			for (int x = request.minX; x <= request.maxX; ++x) {
				auto& cell = plan.cells[PlanIndex(request, x, y, z)];
				cell.x = x;
				cell.y = y;
				cell.z = z;
				cell.material = request.isAllowed(x, y, z) ? MaterialRole::Base : MaterialRole::None;
				cell.walkable = request.isAllowed(x, y, z);
			}
		}
	}

	Report(progress, 25, GenerationStage::GeneratingTerrain);
	switch (request.preset) {
		case Preset::Cave: GenerateCave(plan, stopToken); break;
		case Preset::Dungeon: GenerateDungeon(plan, stopToken); break;
		case Preset::Ruins: GenerateDungeon(plan, stopToken, true); break;
		case Preset::HuntingArea: GenerateHunt(plan, stopToken); break;
		case Preset::City: GenerateCity(plan, stopToken, false); break;
		case Preset::Village: GenerateCity(plan, stopToken, true); break;
		case Preset::Mountain: GenerateMountain(plan, stopToken); break;
		case Preset::River: GenerateRiver(plan, stopToken); break;
		default: GenerateNatural(plan, stopToken); break;
	}

	CheckCancelled(stopToken);
	Report(progress, 55, GenerationStage::CreatingStructures);
	PlanAlignedFloorTransitions(plan);
	CheckCancelled(stopToken);
	ComputeBlendWeights(plan);
	CheckCancelled(stopToken);
	Report(progress, 70, GenerationStage::PlanningDecorations);
	PlanDecorations(plan);
	CheckCancelled(stopToken);
	Report(progress, 85, GenerationStage::ValidatingConnectivity);
	CollectStatistics(plan);
	plan.stableHash = StablePlanHash(plan);
	Report(progress, 100, GenerationStage::Completed);
	return plan;
}

uint64_t StablePlanHash(const GenerationPlan& plan) noexcept {
	uint64_t hash = kHashOffset;
	auto add = [&](uint64_t value) { hash = (hash ^ value) * kHashPrime; };
	auto addDouble = [&](double value) { add(std::bit_cast<uint64_t>(value)); };
	add(plan.request.seed);
	add(static_cast<uint8_t>(plan.request.preset));
	add(static_cast<uint8_t>(plan.request.replacementMode));
	add(static_cast<uint8_t>(plan.request.edgeBlending));
	add(static_cast<uint32_t>(plan.request.minX));
	add(static_cast<uint32_t>(plan.request.minY));
	add(static_cast<uint32_t>(plan.request.minZ));
	add(static_cast<uint32_t>(plan.request.maxX));
	add(static_cast<uint32_t>(plan.request.maxY));
	add(static_cast<uint32_t>(plan.request.maxZ));
	const auto& parameters = plan.request.parameters;
	addDouble(parameters.noiseScale);
	add(static_cast<uint32_t>(parameters.octaves));
	addDouble(parameters.persistence);
	addDouble(parameters.lacunarity);
	addDouble(parameters.waterLevel);
	addDouble(parameters.irregularity);
	add(static_cast<uint32_t>(parameters.smoothingPasses));
	add(static_cast<uint32_t>(parameters.decorationDensity));
	add(static_cast<uint32_t>(parameters.pathWidth));
	add(static_cast<uint32_t>(parameters.roomCount));
	add(static_cast<uint32_t>(parameters.roomMinSize));
	add(static_cast<uint32_t>(parameters.roomMaxSize));
	add(static_cast<uint32_t>(parameters.loops));
	add(static_cast<uint32_t>(parameters.secondaryRoadSpacing));
	add(static_cast<uint32_t>(parameters.edgeMargin));
	add(parameters.autoFixConnectivity ? 1 : 0);
	for (uint8_t allowed : plan.request.allowedCells) add(allowed);
	for (uint8_t protectedCell : plan.request.protectedCells) add(protectedCell);
	for (const auto& cell : plan.cells) {
		add(static_cast<uint32_t>(cell.x));
		add(static_cast<uint32_t>(cell.y));
		add(static_cast<uint32_t>(cell.z));
		add(static_cast<uint8_t>(cell.material));
		add(cell.features);
		add(cell.blendWeight);
		add(cell.walkable ? 1 : 0);
	}
	return hash;
}

BriefInterpretation InterpretBrief(const std::string& text) {
	BriefInterpretation result;
	const auto tokens = Tokenize(text);
	const std::map<std::string, Preset> presets {
		{ "city", Preset::City }, { "village", Preset::Village }, { "mountain", Preset::Mountain }, { "cave", Preset::Cave },
		{ "dungeon", Preset::Dungeon }, { "forest", Preset::Forest }, { "desert", Preset::Desert }, { "snow", Preset::SnowArea },
		{ "swamp", Preset::Swamp }, { "island", Preset::Island }, { "ruins", Preset::Ruins }, { "river", Preset::River }, { "coast", Preset::Coastline },
		{ "cidade", Preset::City }, { "vila", Preset::Village }, { "montanha", Preset::Mountain }, { "caverna", Preset::Cave },
		{ "masmorra", Preset::Dungeon }, { "floresta", Preset::Forest }, { "deserto", Preset::Desert }, { "neve", Preset::SnowArea },
		{ "pantano", Preset::Swamp }, { "ilha", Preset::Island }, { "ruinas", Preset::Ruins }, { "rio", Preset::River }, { "costa", Preset::Coastline },
	};
	const std::set<std::string> modifiers {
		"road", "roads", "lake", "wide", "paths", "dense", "sparse", "vegetation", "multiple", "entrances", "floors", "hunt", "hunting", "west", "east", "north", "south",
		"estrada", "estradas", "lago", "largo", "larga", "largos", "largas", "caminho", "caminhos", "denso", "densa", "esparso", "esparsa", "vegetacao", "multiplas", "entradas", "andares", "caca", "oeste", "leste", "norte", "sul",
	};
	const std::set<std::string> ignoredWords { "with", "and", "the", "from", "com", "uma", "para", "dos", "das" };
	bool hasExplicitPreset = false;
	for (const auto& token : tokens) {
		if (const auto it = presets.find(token); it != presets.end()) {
			result.preset = it->second;
			hasExplicitPreset = true;
			result.hasSupportedTerms = true;
			result.recognized.push_back(token);
		} else if (modifiers.contains(token)) {
			result.hasSupportedTerms = true;
			result.recognized.push_back(token);
			if (token == "wide" || token == "largo" || token == "larga" || token == "largos" || token == "largas") result.parameters.pathWidth = 3;
			if (token == "dense" || token == "denso" || token == "densa") result.parameters.decorationDensity = 42;
			if (token == "sparse" || token == "esparso" || token == "esparsa") result.parameters.decorationDensity = 7;
			// Hunt has special precedence - always override
			if (token == "hunt" || token == "hunting" || token == "caca") {
				result.preset = Preset::HuntingArea;
				hasExplicitPreset = true;
			}
		} else if (ignoredWords.contains(token)) {
			continue;
		} else if (token.size() > 2) {
			result.unknown.push_back(token);
		}
	}
	std::sort(result.recognized.begin(), result.recognized.end());
	result.recognized.erase(std::unique(result.recognized.begin(), result.recognized.end()), result.recognized.end());
	std::sort(result.unknown.begin(), result.unknown.end());
	result.unknown.erase(std::unique(result.unknown.begin(), result.unknown.end()), result.unknown.end());
	return result;
}

void MergeLockedParts(GenerationPlan& generated, const GenerationPlan& previous, bool lockTerrain, bool lockStructures, bool lockDecorations) {
	if (generated.request.minX != previous.request.minX || generated.request.minY != previous.request.minY || generated.request.minZ != previous.request.minZ || generated.request.maxX != previous.request.maxX || generated.request.maxY != previous.request.maxY || generated.request.maxZ != previous.request.maxZ) {
		return;
	}
	if (generated.request.allowedCells != previous.request.allowedCells) {
		return;
	}
	if (generated.request.protectedCells != previous.request.protectedCells) return;
	for (auto& cell : generated.cells) {
		const auto* old = previous.find(cell.x, cell.y, cell.z);
		if (!old) continue;
		if (lockTerrain) {
			cell.material = old->material;
			cell.walkable = old->walkable;
			cell.blendWeight = old->blendWeight;
		}
		if (lockStructures) {
			cell.features = static_cast<uint16_t>((cell.features & ~kStructureFeatures) | (old->features & kStructureFeatures));
			// Retain valid navigation terrain for locked entrances, exits, transitions, and POIs
			if (old->material == MaterialRole::Path || (old->features & FeatureWall) != 0 || 
			    (old->features & (FeatureEntrance | FeatureExit | FeatureTransition | FeaturePointOfInterest)) != 0) {
				cell.material = old->material;
				cell.walkable = old->walkable;
			}
		}
		if (lockDecorations) {
			cell.features = static_cast<uint16_t>((cell.features & ~FeatureDecoration) | (old->features & FeatureDecoration));
		}
	}
	// Revalidate and recompute derived structures after applying locks
	CollectStatistics(generated);
	generated.stableHash = StablePlanHash(generated);
}

} // namespace ProceduralMap
