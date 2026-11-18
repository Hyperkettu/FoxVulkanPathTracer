#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fox {

	namespace Scene {

		// A* path finding on the ground (MainRayTracingScene::FindPath fills the grid from the terrain, the
		// obstacles and the water; see NavAgentComponent).
		//
		// The grid is square cells over a rectangle of the ground, each with the ground's height at its centre and
		// whether it is blocked. A step to a neighbour (eight of them) is allowed when neither is blocked, a diagonal
		// step does not cut a blocked corner, and the climb is gentle enough: no steeper than maxSlope, or no higher
		// than maxStep. Steps cost their length along the ground, more uphill. The path found is then straightened:
		// waypoints that can see each other along walkable ground are joined, so it turns only where it has to.
		class NavigationGrid {
		public:
			struct Rules {
				float maxSlope = 0.7f;      // Rise over run (tan of the steepest angle)
				float maxStep = 0.3f;       // Metres: climbs this small are always allowed
				float uphillCost = 2.0f;    // Extra cost per unit of slope uphill
			};

			struct Result {
				std::vector<glm::vec3> points; // From the start to the goal (or the nearest reachable place), on the ground
				bool found = false;            // A path (or a partial one)
				bool reachesGoal = false;      // False: the goal could not be reached, the path ends as near as it could
				size_t expanded = 0;           // Cells the search looked at
			};

			// columns x rows cells of cellSize metres, the first one's corner at corner (x, z)
			void Create(const glm::vec2& corner, int columns, int rows, float cellSize);

			int Columns() const { return columns; }
			int Rows() const { return rows; }
			float CellSize() const { return cellSize; }
			glm::vec2 Corner() const { return corner; }
			glm::vec2 CellCentre(int column, int row) const {
				return corner + (glm::vec2(column, row) + 0.5f) * cellSize;
			}
			bool CellAt(float x, float z, int& column, int& row) const;

			float& Height(int column, int row) { return heights[Index(column, row)]; }
			float Height(int column, int row) const { return heights[Index(column, row)]; }
			bool IsBlocked(int column, int row) const { return blocked[Index(column, row)] != 0; }
			void Block(int column, int row) { blocked[Index(column, row)] = 1; }
			// The ground's height at a point, between the cell centres
			float HeightAt(float x, float z) const;

			// Blocks everything closer than radius to a blocked cell (an agent's width keeps it this far away)
			void Inflate(float radius);

			// The path from start to goal (world positions; their heights are not used)
			Result FindPath(const glm::vec3& start, const glm::vec3& goal, const Rules& rules) const;

			size_t BlockedCount() const;

		private:
			glm::vec2 corner{ 0.0f };
			int columns = 0, rows = 0;
			float cellSize = 1.0f;
			std::vector<float> heights;
			std::vector<uint8_t> blocked;

			size_t Index(int column, int row) const { return static_cast<size_t>(row) * columns + column; }
			bool Inside(int column, int row) const { return column >= 0 && row >= 0 && column < columns && row < rows; }
			// Whether a step between neighbouring cells is allowed (heights only)
			bool CanStep(float from, float to, float distance, const Rules& rules) const;
			// The nearest unblocked cell to a cell, within a few metres; false if none
			bool NearestOpen(int& column, int& row) const;
			// Whether a straight line between two points crosses only open cells with gentle enough climbs
			bool LineWalkable(const glm::vec2& a, const glm::vec2& b, const Rules& rules) const;
		};
	}
}
