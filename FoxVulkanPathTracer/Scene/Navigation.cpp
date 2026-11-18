#include "FoxRenderer.h"

#include <queue>

namespace Fox {

	namespace Scene {

		namespace {
			constexpr float SQRT2 = 1.41421356f;
		}

		void NavigationGrid::Create(const glm::vec2& gridCorner, int gridColumns, int gridRows, float gridCellSize) {
			corner = gridCorner;
			columns = std::max(gridColumns, 1);
			rows = std::max(gridRows, 1);
			cellSize = std::max(gridCellSize, 1e-3f);
			heights.assign(static_cast<size_t>(columns) * rows, 0.0f);
			blocked.assign(static_cast<size_t>(columns) * rows, 0);
		}

		bool NavigationGrid::CellAt(float x, float z, int& column, int& row) const {
			column = static_cast<int>(std::floor((x - corner.x) / cellSize));
			row = static_cast<int>(std::floor((z - corner.y) / cellSize));
			return Inside(column, row);
		}

		float NavigationGrid::HeightAt(float x, float z) const {
			if (heights.empty()) {
				return 0.0f;
			}
			const float fx = glm::clamp((x - corner.x) / cellSize - 0.5f, 0.0f, static_cast<float>(columns - 1));
			const float fz = glm::clamp((z - corner.y) / cellSize - 0.5f, 0.0f, static_cast<float>(rows - 1));
			const int x0 = static_cast<int>(fx), z0 = static_cast<int>(fz);
			const int x1 = std::min(x0 + 1, columns - 1), z1 = std::min(z0 + 1, rows - 1);
			const float tx = fx - x0, tz = fz - z0;
			const float a = glm::mix(Height(x0, z0), Height(x1, z0), tx);
			const float b = glm::mix(Height(x0, z1), Height(x1, z1), tx);
			return glm::mix(a, b, tz);
		}

		size_t NavigationGrid::BlockedCount() const {
			size_t count = 0;
			for (uint8_t b : blocked) {
				count += b;
			}
			return count;
		}

		void NavigationGrid::Inflate(float radius) {
			if (radius <= 0.0f || blocked.empty()) {
				return;
			}
			// An agent stands at a cell's centre: it is too near a blocked cell when closer than its radius to that
			// cell's edge, about its radius plus half a cell to its centre
			const float reach = radius + 0.5f * cellSize;
			const int cells = static_cast<int>(std::ceil(reach / cellSize));
			std::vector<glm::ivec2> offsets;
			for (int dz = -cells; dz <= cells; dz++) {
				for (int dx = -cells; dx <= cells; dx++) {
					if ((dx != 0 || dz != 0) && (dx * dx + dz * dz) * cellSize * cellSize < reach * reach) {
						offsets.emplace_back(dx, dz);
					}
				}
			}
			const std::vector<uint8_t> original = blocked;
			auto originallyBlocked = [&](int c, int r) { return !Inside(c, r) || original[Index(c, r)] != 0; };
			for (int r = 0; r < rows; r++) {
				for (int c = 0; c < columns; c++) {
					if (!original[Index(c, r)]) {
						continue;
					}
					// Only the edges of blocked areas spread
					if (originallyBlocked(c - 1, r) && originallyBlocked(c + 1, r) && originallyBlocked(c, r - 1) && originallyBlocked(c, r + 1)) {
						continue;
					}
					for (const glm::ivec2& o : offsets) {
						if (Inside(c + o.x, r + o.y)) {
							blocked[Index(c + o.x, r + o.y)] = 1;
						}
					}
				}
			}
		}

		bool NavigationGrid::CanStep(float from, float to, float distance, const Rules& rules) const {
			const float rise = std::abs(to - from);
			return rise <= rules.maxStep || rise <= rules.maxSlope * distance;
		}

		bool NavigationGrid::NearestOpen(int& column, int& row) const {
			if (Inside(column, row) && !IsBlocked(column, row)) {
				return true;
			}
			const int reach = std::max(2, static_cast<int>(std::ceil(6.0f / cellSize)));
			int best = INT32_MAX, bestColumn = 0, bestRow = 0;
			for (int ring = 1; ring <= reach; ring++) {
				for (int dz = -ring; dz <= ring; dz++) {
					for (int dx = -ring; dx <= ring; dx++) {
						if (std::max(std::abs(dx), std::abs(dz)) != ring) {
							continue; // Only this ring
						}
						const int c = column + dx, r = row + dz;
						if (Inside(c, r) && !IsBlocked(c, r) && dx * dx + dz * dz < best) {
							best = dx * dx + dz * dz;
							bestColumn = c;
							bestRow = r;
						}
					}
				}
				// A ring further out cannot hold anything nearer than ring^2
				if (best <= (ring + 1) * (ring + 1) - 1 && best != INT32_MAX) {
					break;
				}
			}
			if (best == INT32_MAX) {
				return false;
			}
			column = bestColumn;
			row = bestRow;
			return true;
		}

		bool NavigationGrid::LineWalkable(const glm::vec2& a, const glm::vec2& b, const Rules& rules) const {
			const glm::vec2 d = b - a;
			const float length = glm::length(d);
			const int samples = std::max(1, static_cast<int>(std::ceil(length / (0.25f * cellSize))));
			int lastColumn = 0, lastRow = 0;
			if (!CellAt(a.x, a.y, lastColumn, lastRow) || IsBlocked(lastColumn, lastRow)) {
				return false;
			}
			for (int i = 1; i <= samples; i++) {
				const glm::vec2 p = a + d * (static_cast<float>(i) / samples);
				int c = 0, r = 0;
				if (!CellAt(p.x, p.y, c, r) || IsBlocked(c, r)) {
					return false;
				}
				if (c == lastColumn && r == lastRow) {
					continue;
				}
				// Across a corner: both cells beside it must be open too
				if (c != lastColumn && r != lastRow && (IsBlocked(lastColumn, r) || IsBlocked(c, lastRow))) {
					return false;
				}
				const float distance = cellSize * ((c != lastColumn && r != lastRow) ? SQRT2 : 1.0f);
				if (!CanStep(Height(lastColumn, lastRow), Height(c, r), distance, rules)) {
					return false;
				}
				lastColumn = c;
				lastRow = r;
			}
			return true;
		}

		NavigationGrid::Result NavigationGrid::FindPath(const glm::vec3& start, const glm::vec3& goal, const Rules& rules) const {
			Result result;
			if (heights.empty()) {
				return result;
			}
			auto clampCell = [&](const glm::vec3& p, int& c, int& r) {
				CellAt(p.x, p.z, c, r);
				c = glm::clamp(c, 0, columns - 1);
				r = glm::clamp(r, 0, rows - 1);
			};
			int startColumn, startRow, goalColumn, goalRow;
			clampCell(start, startColumn, startRow);
			clampCell(goal, goalColumn, goalRow);
			const int requestedGoalColumn = goalColumn, requestedGoalRow = goalRow;
			const int requestedStartColumn = startColumn, requestedStartRow = startRow;
			// Standing in (or aiming at) something: from and to the nearest open ground instead
			if (!NearestOpen(startColumn, startRow) || !NearestOpen(goalColumn, goalRow)) {
				return result;
			}

			const size_t count = heights.size();
			const size_t startIndex = Index(startColumn, startRow), goalIndex = Index(goalColumn, goalRow);
			std::vector<float> cost(count, std::numeric_limits<float>::infinity());
			std::vector<int32_t> parent(count, -1);
			std::vector<uint8_t> closed(count, 0);

			auto heuristic = [&](int c, int r) {
				const float dx = static_cast<float>(std::abs(c - goalColumn)), dz = static_cast<float>(std::abs(r - goalRow));
				return (std::max(dx, dz) + (SQRT2 - 1.0f) * std::min(dx, dz)) * cellSize;
			};

			using Entry = std::pair<float, int32_t>; // Estimated total cost, cell
			std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
			cost[startIndex] = 0.0f;
			open.emplace(heuristic(startColumn, startRow), static_cast<int32_t>(startIndex));

			size_t nearest = startIndex;
			float nearestDistance = heuristic(startColumn, startRow);
			static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
			static const int DZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

			while (!open.empty()) {
				const int32_t index = open.top().second;
				open.pop();
				if (closed[index]) {
					continue;
				}
				closed[index] = 1;
				result.expanded++;
				const int c = index % columns, r = index / columns;
				const float distanceLeft = heuristic(c, r);
				if (distanceLeft < nearestDistance) {
					nearestDistance = distanceLeft;
					nearest = index;
				}
				if (static_cast<size_t>(index) == goalIndex) {
					break;
				}
				const float height = heights[index];
				for (int n = 0; n < 8; n++) {
					const int nc = c + DX[n], nr = r + DZ[n];
					if (!Inside(nc, nr) || IsBlocked(nc, nr)) {
						continue;
					}
					const bool diagonal = n >= 4;
					if (diagonal && (IsBlocked(nc, r) || IsBlocked(c, nr))) {
						continue; // No cutting corners
					}
					const size_t next = Index(nc, nr);
					if (closed[next]) {
						continue;
					}
					const float distance = cellSize * (diagonal ? SQRT2 : 1.0f);
					const float rise = heights[next] - height;
					if (!CanStep(height, heights[next], distance, rules)) {
						continue;
					}
					const float step = std::sqrt(distance * distance + rise * rise) * (1.0f + rules.uphillCost * std::max(rise, 0.0f) / distance);
					const float total = cost[index] + step;
					if (total < cost[next]) {
						cost[next] = total;
						parent[next] = index;
						open.emplace(total + heuristic(nc, nr), static_cast<int32_t>(next));
					}
				}
			}

			result.reachesGoal = closed[goalIndex] != 0;
			const size_t end = result.reachesGoal ? goalIndex : nearest;

			// The cells back to the start, as points on the ground
			std::vector<glm::vec2> cells;
			for (int32_t index = static_cast<int32_t>(end); index >= 0; index = parent[index]) {
				cells.push_back(CellCentre(index % columns, index / columns));
				if (static_cast<size_t>(index) == startIndex) {
					break;
				}
			}
			std::reverse(cells.begin(), cells.end());
			// Exactly from where it stands and to where it was sent, when those were open ground
			if (startColumn == requestedStartColumn && startRow == requestedStartRow) {
				cells.front() = glm::vec2(start.x, start.z);
			}
			else {
				cells.insert(cells.begin(), glm::vec2(start.x, start.z)); // Steps out of what it stood in
			}
			if (result.reachesGoal && goalColumn == requestedGoalColumn && goalRow == requestedGoalRow) {
				if (cells.size() > 1) {
					cells.back() = glm::vec2(goal.x, goal.z);
				}
				else {
					cells.emplace_back(goal.x, goal.z);
				}
			}

			// Straightened: from each waypoint, on to the furthest one it can walk to in a straight line
			std::vector<glm::vec2> straight;
			straight.push_back(cells.front());
			size_t at = 0;
			while (at + 1 < cells.size()) {
				size_t furthest = at + 1;
				for (size_t next = at + 2; next < cells.size(); next++) {
					if (LineWalkable(cells[at], cells[next], rules)) {
						furthest = next;
					}
					else if (next > furthest + 8) {
						break; // Far past the last one it could see: around a corner
					}
				}
				straight.push_back(cells[furthest]);
				at = furthest;
			}

			for (const glm::vec2& p : straight) {
				result.points.emplace_back(p.x, HeightAt(p.x, p.y), p.y);
			}
			result.found = result.points.size() > 0;
			return result;
		}
	}
}
