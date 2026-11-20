// Cloth: XPBD in small steps, with collisions, wind and tearing (see Scene/Cloth.h)

#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Scene/Cloth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>

namespace Fox {

	namespace Scene {

		namespace {
			constexpr float AIR_DENSITY = 1.2f;          // kg/m^3
			constexpr float GRAB_COMPLIANCE = 2e-6f;     // m/N: the mouse's hold, nearly hard
			constexpr uint32_t MAX_TEARS_PER_STEP = 24;  // Splits a frame (a tear runs over a few frames, as it does)
			constexpr uint32_t PARTICLE_LIMIT = 16384;
			const glm::uint NONE = 0xffffffffu;

			float Cross2(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }
		}

		glm::vec3 ClothSimulation::LocalOf(const glm::vec2& r) const {
			const float halfWidth = 0.5f * built.width;
			return built.flat ? glm::vec3(r.x - halfWidth, 0.0f, r.y - 0.5f * built.height) : glm::vec3(r.x - halfWidth, -r.y, 0.0f);
		}

		bool ClothSimulation::PinnedAt(const glm::vec2& r, Pins pins) const {
			const float e = 0.25f * std::min(spacing, spacingDown);
			const bool top = r.y < e;
			const bool left = r.x < e;
			// Corners: a patch around each (as a grommet or a knot holds a bit of the cloth, not one thread)
			const float patch = std::max(1.6f * std::max(spacing, spacingDown), 0.04f * std::min(built.width, built.height));
			auto nearCorner = [&](bool bottoms) {
				for (int cy = 0; cy < (bottoms ? 2 : 1); cy++) {
					for (int cx = 0; cx < 2; cx++) {
						const glm::vec2 corner(cx ? built.width : 0.0f, cy ? built.height : 0.0f);
						if (glm::distance(r, corner) < patch + e) return true;
					}
				}
				return false;
			};
			switch (pins) {
			case Pins::TopEdge: return top;
			case Pins::TopCorners: return nearCorner(false);
			case Pins::LeftEdge: return left;
			case Pins::Corners: return nearCorner(true);
			default: return false;
			}
		}

		void ClothSimulation::UpdatePins(Pins pins) {
			placedPins = pins;
			for (size_t i = 0; i < positions.size(); i++) {
				pinned[i] = active[i] && PinnedAt(rest[i], pins) ? 1 : 0;
				inverseMass[i] = active[i] && !pinned[i] && mass[i] > 0.0f ? 1.0f / mass[i] : 0.0f;
			}
			UpdateTethers();
		}

		void ClothSimulation::UpdateTethers() {
			// Distances through the cloth from the pins (Dijkstra over its edges, all pins at once): a piece torn away
			// has none and falls
			const size_t count = positions.size();
			tetherAnchor.assign(count, NONE);
			tetherLength.assign(count, std::numeric_limits<float>::max());
			std::vector<uint32_t> start(count + 1, 0), links(stretch.size() * 2);
			for (const Constraint& c : stretch) { start[c.a + 1]++; start[c.b + 1]++; }
			for (size_t i = 0; i < count; i++) start[i + 1] += start[i];
			std::vector<uint32_t> fill(start.begin(), start.end() - 1);
			std::vector<float> lengths(links.size());
			for (const Constraint& c : stretch) {
				links[fill[c.a]] = c.b; lengths[fill[c.a]++] = c.rest;
				links[fill[c.b]] = c.a; lengths[fill[c.b]++] = c.rest;
			}
			using Item = std::pair<float, uint32_t>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
			for (uint32_t i = 0; i < count; i++) {
				if (pinned[i]) {
					tetherAnchor[i] = i;
					tetherLength[i] = 0.0f;
					queue.push({ 0.0f, i });
				}
			}
			while (!queue.empty()) {
				const auto [distance, i] = queue.top();
				queue.pop();
				if (distance > tetherLength[i]) continue;
				for (uint32_t e = start[i]; e < start[i + 1]; e++) {
					const uint32_t j = links[e];
					const float through = distance + lengths[e];
					if (through < tetherLength[j]) {
						tetherLength[j] = through;
						tetherAnchor[j] = tetherAnchor[i];
						queue.push({ through, j });
					}
				}
			}
			// Through the cloth is a little longer than straight across: the straight distance in the sheet when shorter
			for (uint32_t i = 0; i < count; i++) {
				if (tetherAnchor[i] == NONE || tetherAnchor[i] == i) continue;
				tetherLength[i] = std::min(tetherLength[i], glm::distance(rest[i], rest[tetherAnchor[i]]) * 1.02f);
			}
			// Stretch solved outwards from the pins: one sweep then carries their hold all through the cloth, as a
			// chain hangs (in any other order the far side's corrections undo the near side's, and it stretches)
			std::stable_sort(stretch.begin(), stretch.end(), [&](const Constraint& x, const Constraint& y) {
				return std::min(tetherLength[x.a], tetherLength[x.b]) < std::min(tetherLength[y.a], tetherLength[y.b]);
			});
		}

		void ClothSimulation::SolveTethers() {
			// Slack: a tearable cloth may stretch towards its tear limit (else nothing would ever tear it)
			const float slack = built.tearable ? std::clamp(0.2f * (built.tearStrain - 1.0f), 0.03f, 0.15f) : 0.01f;
			for (size_t i = 0; i < positions.size(); i++) {
				const uint32_t a = tetherAnchor[i];
				if (a == NONE || !inverseMass[i] || a == i) continue;
				const glm::vec3 d = positions[i] - positions[a];
				const float length = glm::length(d);
				const float most = tetherLength[i] * (1.0f + slack);
				if (length > most) positions[i] = positions[a] + d * (most / length);
			}
		}

		void ClothSimulation::Build(const Settings& settings, const glm::mat4& world) {
			built = settings;
			built.width = std::max(settings.width, 0.05f);
			built.height = std::max(settings.height, 0.05f);
			const uint32_t across = static_cast<uint32_t>(std::clamp(settings.resolution, 2, 128));
			spacing = built.width / static_cast<float>(across);
			uint32_t down = std::max(1u, static_cast<uint32_t>(std::lround(built.height / spacing)));
			if ((across + 1) * (down + 1) > PARTICLE_LIMIT / 2) {
				down = std::max(1u, PARTICLE_LIMIT / 2 / (across + 1) - 1);
			}
			spacingDown = built.height / static_cast<float>(down);
			columns = across + 1;
			rows = down + 1;
			const uint32_t count = columns * rows;
			maxParticles = std::min(PARTICLE_LIMIT, count * 2 + 64);

			rest.clear();
			positions.clear();
			for (uint32_t j = 0; j < rows; j++) {
				for (uint32_t i = 0; i < columns; i++) {
					const glm::vec2 r(static_cast<float>(i) * spacing, static_cast<float>(j) * spacingDown);
					rest.push_back(r);
					positions.push_back(glm::vec3(world * glm::vec4(LocalOf(r), 1.0f)));
				}
			}
			previous = positions;
			framePositions = positions;
			velocities.assign(count, glm::vec3(0.0f));
			windAcceleration.assign(count, glm::vec3(0.0f));
			groundHeight.assign(count, -std::numeric_limits<float>::infinity());
			mass.assign(count, 0.0f);
			inverseMass.assign(count, 0.0f);
			pinned.assign(count, 0);
			active.assign(count, 1);

			// Two triangles a cell, the diagonal alternating (no grain to the folds)
			triangles.clear();
			for (uint32_t j = 0; j + 1 < rows; j++) {
				for (uint32_t i = 0; i + 1 < columns; i++) {
					const uint32_t a = j * columns + i, b = a + 1, c = a + columns, d = c + 1;
					if ((i + j) & 1) {
						triangles.push_back({ a, c, b });
						triangles.push_back({ b, c, d });
					}
					else {
						triangles.push_back({ a, c, d });
						triangles.push_back({ a, d, b });
					}
				}
			}
			alive.assign(triangles.size(), 1);
			particleTriangles.assign(count, {});
			for (uint32_t t = 0; t < triangles.size(); t++) {
				for (int k = 0; k < 3; k++) {
					particleTriangles[triangles[t][k]].push_back(t);
				}
			}
			grabbed.clear();
			stats = Stats{};
			age = 0.0f;
			carry = 0.0f;
			strained.clear();
			lastWorld = world;
			RebuildConstraints();
			UpdatePins(settings.pins);
			boundsLow = boundsHigh = positions[0];
			for (const glm::vec3& p : positions) {
				boundsLow = glm::min(boundsLow, p);
				boundsHigh = glm::max(boundsHigh, p);
			}
		}

		void ClothSimulation::RebuildConstraints() {
			topologyChanged = false;
			// Masses: a third of each triangle's
			std::fill(mass.begin(), mass.end(), 0.0f);
			const float density = std::max(built.density, 0.01f);
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::uvec3& tri = triangles[t];
				const float area = 0.5f * std::abs(Cross2(rest[tri.y] - rest[tri.x], rest[tri.z] - rest[tri.x]));
				for (int k = 0; k < 3; k++) {
					mass[tri[k]] += area * density / 3.0f;
				}
			}
			for (size_t i = 0; i < positions.size(); i++) {
				active[i] = mass[i] > 0.0f ? 1 : 0;
			}

			// Edges (sorted, so the order is the same every time): stretch along each, bending across each shared one
			struct Entry { uint64_t key; uint32_t opposite; };
			std::vector<Entry> entries;
			entries.reserve(triangles.size() * 3);
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::uvec3& tri = triangles[t];
				for (int k = 0; k < 3; k++) {
					const uint32_t a = tri[k], b = tri[(k + 1) % 3];
					const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
					entries.push_back({ key, tri[(k + 2) % 3] });
				}
			}
			std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) { return x.key < y.key || (x.key == y.key && x.opposite < y.opposite); });
			stretch.clear();
			bend.clear();
			for (size_t e = 0; e < entries.size();) {
				size_t end = e + 1;
				while (end < entries.size() && entries[end].key == entries[e].key) end++;
				const uint32_t a = static_cast<uint32_t>(entries[e].key >> 32), b = static_cast<uint32_t>(entries[e].key & 0xffffffffu);
				stretch.push_back({ a, b, glm::length(rest[a] - rest[b]) });
				if (end - e == 2) {
					const uint32_t o1 = entries[e].opposite, o2 = entries[e + 1].opposite;
					bend.push_back({ o1, o2, glm::length(rest[o1] - rest[o2]) });
				}
				e = end;
			}
			UpdatePins(placedPins);
			uint32_t particles = 0, living = 0;
			for (uint8_t a : active) particles += a;
			for (uint8_t a : alive) living += a;
			stats.particles = particles;
			stats.triangles = living;
			stats.constraints = static_cast<uint32_t>(stretch.size() + bend.size());
		}

		void ClothSimulation::SolveDistance(const std::vector<Constraint>& constraints, float compliance, float h, bool backwards) {
			// Gauss-Seidel, in the given order (stretch: outwards from the pins); backwards: the other way
			const float alpha = std::max(compliance, 0.0f) / (h * h);
			const size_t count = constraints.size();
			for (size_t k = 0; k < count; k++) {
				const Constraint& c = constraints[backwards ? count - 1 - k : k];
				const float wa = inverseMass[c.a], wb = inverseMass[c.b];
				const float w = wa + wb;
				if (w <= 0.0f) continue;
				const glm::vec3 d = positions[c.a] - positions[c.b];
				const float length = glm::length(d);
				if (length < 1e-9f) continue;
				const float s = -(length - c.rest) / (w + alpha);
				const glm::vec3 n = d / length;
				positions[c.a] += n * (s * wa);
				positions[c.b] -= n * (s * wb);
			}
		}

		void ClothSimulation::ComputeWind(const World& environment, float dt) {
			// The air on each particle: its share of the cloth's area, facing the way the cloth around it faces (the
			// normals of its triangles, area weighted). Per particle, not per triangle: a crease one triangle wide has
			// its triangles facing opposite ways, which cancel here; per triangle the air would push them apart and
			// fine wrinkles would grow out of nothing.
			const size_t count = positions.size();
			std::fill(windAcceleration.begin(), windAcceleration.end(), glm::vec3(0.0f));
			airVelocity.assign(count, glm::vec3(0.0f));
			if (environment.wind) {
				for (size_t i = 0; i < count; i++) {
					if (active[i]) airVelocity[i] = environment.wind(positions[i]);
				}
			}
			std::vector<glm::vec3>& normals = windNormals;
			normals.assign(count, glm::vec3(0.0f));
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::uvec3& tri = triangles[t];
				const glm::vec3 twiceArea = glm::cross(positions[tri.y] - positions[tri.x], positions[tri.z] - positions[tri.x]);
				for (int k = 0; k < 3; k++) normals[tri[k]] += twiceArea; // Length: twice the area, summed
			}
			const float density = std::max(built.density, 0.01f);
			const float drag = 0.5f * AIR_DENSITY * std::max(built.drag, 0.0f);
			const float skin = 0.5f * AIR_DENSITY * std::max(built.lift, 0.0f) * 0.1f;
			for (size_t i = 0; i < count; i++) {
				if (!active[i] || mass[i] <= 0.0f) continue;
				const float length = glm::length(normals[i]);
				if (length < 1e-12f) continue;
				const glm::vec3 n = normals[i] / length;
				const float area = mass[i] / density; // Its third of each of its triangles
				const glm::vec3 u = airVelocity[i] - velocities[i];
				const float un = glm::dot(u, n);
				const glm::vec3 along = u - n * un;
				// Pressure on its face (along the normal, whichever side the air comes from) and friction along it
				glm::vec3 force = n * (drag * area * std::abs(un) * un) + along * (skin * area * glm::length(along));
				// Explicit: never more than stops it relative to the air in one frame (light cloth in a storm stays stable)
				const float most = mass[i] * glm::length(u) / std::max(dt, 1e-4f);
				const float magnitude = glm::length(force);
				if (magnitude > most) force *= most / magnitude;
				windAcceleration[i] = force / mass[i];
			}
		}

		void ClothSimulation::Collide(const World& environment, std::vector<Collider>& colliders, float substepFraction, float h) {
			const float thickness = std::max(built.thickness, 0.001f);
			const float friction = std::clamp(built.friction, 0.0f, 1.0f);
			const size_t count = positions.size();
			const float steps = static_cast<float>(std::max(built.substeps, 1));

			// Friction (Coulomb): what the particle slid along the surface this substep (relative to the surface's own
			// move) is taken away, all of it while it is within friction x the push out (static: cloth stays put on a
			// slope gentler than that, even on a ball), else part of it (kinetic: it slides, dragging)
			auto applyFriction = [&](size_t i, const glm::vec3& normal, float push, const glm::vec3& surfaceMove) {
				glm::vec3 slide = positions[i] - previous[i] - surfaceMove;
				slide -= normal * glm::dot(slide, normal);
				const float length = glm::length(slide);
				if (length < 1e-9f) return;
				positions[i] -= length <= 1.5f * friction * push ? slide : slide * (0.5f * friction);
			};

			if (environment.ground) {
				for (size_t i = 0; i < count; i++) {
					if (!inverseMass[i]) continue;
					const float floor = groundHeight[i] + thickness;
					if (positions[i].y < floor) {
						const float push = floor - positions[i].y;
						positions[i].y = floor;
						applyFriction(i, glm::vec3(0.0f, 1.0f, 0.0f), push, glm::vec3(0.0f));
					}
				}
			}

			for (Collider& c : colliders) {
				const glm::vec3 centre = glm::mix(c.previousPosition, c.position, substepFraction);
				const glm::vec3 move = (c.position - c.previousPosition) / steps;
				const glm::quat inverse = glm::conjugate(c.rotation);
				const float reach = thickness + (c.type == Collider::Type::Sphere ? c.radius
					: c.type == Collider::Type::Capsule ? c.halfHeight + c.radius : glm::length(c.halfExtents));
				if (glm::any(glm::lessThan(centre + glm::vec3(reach), boundsLow - glm::vec3(0.5f)))
					|| glm::any(glm::greaterThan(centre - glm::vec3(reach), boundsHigh + glm::vec3(0.5f)))) {
					continue;
				}
				for (size_t i = 0; i < count; i++) {
					if (!inverseMass[i]) continue;
					const glm::vec3 d = positions[i] - centre;
					if (glm::dot(d, d) > reach * reach) continue;
					glm::vec3 normal(0.0f), correction(0.0f);
					if (c.type == Collider::Type::Sphere || c.type == Collider::Type::Capsule) {
						glm::vec3 nearest = centre;
						if (c.type == Collider::Type::Capsule) {
							const glm::vec3 axis = c.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
							nearest += axis * std::clamp(glm::dot(d, axis), -c.halfHeight, c.halfHeight);
						}
						const glm::vec3 out = positions[i] - nearest;
						const float distance = glm::length(out);
						const float radius = c.radius + thickness;
						if (distance >= radius) continue;
						normal = distance > 1e-6f ? out / distance : glm::vec3(0.0f, 1.0f, 0.0f);
						correction = normal * (radius - distance);
					}
					else {
						const glm::vec3 local = inverse * d;
						const glm::vec3 clamped = glm::clamp(local, -c.halfExtents, c.halfExtents);
						const glm::vec3 outside = local - clamped;
						const float distance = glm::length(outside);
						if (distance > 0.0f) {
							if (distance >= thickness) continue;
							normal = c.rotation * (outside / distance);
							correction = normal * (thickness - distance);
						}
						else {
							// Inside: out through the nearest face
							int axis = 0;
							float least = std::numeric_limits<float>::max();
							for (int a = 0; a < 3; a++) {
								const float gap = c.halfExtents[a] - std::abs(local[a]);
								if (gap < least) { least = gap; axis = a; }
							}
							glm::vec3 n(0.0f);
							n[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
							normal = c.rotation * n;
							correction = normal * (least + thickness);
						}
					}
					const glm::vec3 before = positions[i];
					positions[i] += correction;
					applyFriction(i, normal, glm::length(correction), move);
					// The cloth pushes back as hard (two way: a ball is caught by a net)
					const glm::vec3 impulse = -(positions[i] - before) * (mass[i] / h);
					c.impulse += impulse;
					c.angularImpulse += glm::cross(positions[i] - centre, impulse);
				}
			}
		}

		void ClothSimulation::FindSelfPairs() {
			// Once a frame: the pairs that could come within reach of each other during it (a spatial hash of cells
			// as large as that reach); each substep then only checks those
			const float distance = 0.8f * std::min(spacing, spacingDown);
			const float reach = 1.6f * distance;
			const float inverseCell = 1.0f / reach;
			const size_t count = positions.size();
			const uint32_t tableSize = static_cast<uint32_t>(2 * count + 1);
			auto hashOf = [&](int x, int y, int z) {
				const uint32_t h = static_cast<uint32_t>((x * 92837111) ^ (y * 689287499) ^ (z * 283923481));
				return h % tableSize;
			};
			cellStart.assign(tableSize + 1, 0);
			cellEntries.resize(count);
			cells.resize(count);
			for (size_t i = 0; i < count; i++) {
				cells[i] = glm::ivec3(glm::floor(positions[i] * inverseCell));
				if (active[i]) cellStart[hashOf(cells[i].x, cells[i].y, cells[i].z)]++;
			}
			uint32_t sum = 0;
			for (uint32_t& start : cellStart) {
				sum += start;
				start = sum;
			}
			for (size_t i = 0; i < count; i++) {
				if (active[i]) cellEntries[--cellStart[hashOf(cells[i].x, cells[i].y, cells[i].z)]] = static_cast<uint32_t>(i);
			}
			selfPairs.clear();
			const float reachSquared = reach * reach, distanceSquared = distance * distance;
			for (size_t i = 0; i < count; i++) {
				if (!active[i]) continue;
				for (int dz = -1; dz <= 1; dz++) {
					for (int dy = -1; dy <= 1; dy++) {
						for (int dx = -1; dx <= 1; dx++) {
							const uint32_t h = hashOf(cells[i].x + dx, cells[i].y + dy, cells[i].z + dz);
							for (uint32_t e = cellStart[h]; e < cellStart[h + 1]; e++) {
								const uint32_t j = cellEntries[e];
								if (j <= i || inverseMass[i] + inverseMass[j] <= 0.0f) continue;
								const glm::vec3 d = positions[i] - positions[j];
								if (glm::dot(d, d) >= reachSquared) continue;
								const glm::vec2 apart = rest[i] - rest[j];
								if (glm::dot(apart, apart) < distanceSquared) continue; // Neighbours in the sheet (or a split pair)
								selfPairs.push_back({ static_cast<uint32_t>(i), j });
							}
						}
					}
				}
			}
			// A pair found twice (two cells hashing alike) is harmless: the second push finds them apart
		}

		void ClothSimulation::CollideSelf() {
			const float distance = 0.8f * std::min(spacing, spacingDown);
			const float distanceSquared = distance * distance;
			for (const auto& [i, j] : selfPairs) {
				const glm::vec3 d = positions[i] - positions[j];
				const float squared = glm::dot(d, d);
				if (squared >= distanceSquared || squared < 1e-12f) continue;
				const float w = inverseMass[i] + inverseMass[j];
				if (w <= 0.0f) continue;
				const float length = std::sqrt(squared);
				const glm::vec3 push = d * ((distance - length) / (length * w));
				positions[i] += push * inverseMass[i];
				positions[j] -= push * inverseMass[j];
			}
		}

		void ClothSimulation::Step(float dt, const Settings& settings, const glm::mat4& world, const World& environment, std::vector<Collider>& colliders) {
			if (positions.empty()) return;
			const auto started = std::chrono::steady_clock::now();
			dt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
			// Settings that need no rebuild
			const Settings shape = built;
			built = settings;
			built.width = shape.width;
			built.height = shape.height;
			built.resolution = shape.resolution;
			built.flat = shape.flat;
			if (settings.density != shape.density) {
				RebuildConstraints();
			}
			if (settings.pins != placedPins) {
				UpdatePins(settings.pins);
			}
			for (Collider& c : colliders) {
				c.impulse = c.angularImpulse = glm::vec3(0.0f);
			}
			framePositions = positions;
			if (dt <= 0.0f) {
				return;
			}

			const size_t count = positions.size();
			if (environment.ground) {
				for (size_t i = 0; i < count; i++) {
					groundHeight[i] = environment.ground(positions[i].x, positions[i].z);
				}
			}
			ComputeWind(environment, dt);
			if (built.selfCollision) {
				FindSelfPairs();
			}

			// Fixed substeps (substeps per 1/60 s), time left over carried to the next frame. A substep whose length
			// changed from frame to frame would turn the last one's leftover error into velocity (and the cloth would
			// crumple by itself); behind by more than a few frames' work, the rest is dropped.
			const int perFrame = std::clamp(built.substeps, 1, 64);
			const float h = 1.0f / (60.0f * static_cast<float>(perFrame));
			carry += dt;
			const int steps = std::min(static_cast<int>(carry / h), perFrame * 2);
			carry = std::min(carry - static_cast<float>(steps) * h, h);
			if (steps == 0) {
				return;
			}
			const float keep = std::max(0.0f, 1.0f - std::max(built.damping, 0.0f) * h);
			std::vector<glm::vec3> pinFrom(count), pinTo(count);
			for (size_t i = 0; i < count; i++) {
				if (pinned[i]) {
					const glm::vec4 local(LocalOf(rest[i]), 1.0f);
					pinFrom[i] = glm::vec3(lastWorld * local);
					pinTo[i] = glm::vec3(world * local);
				}
			}
			for (int s = 0; s < steps; s++) {
				const float fraction = static_cast<float>(s + 1) / static_cast<float>(steps);
				for (size_t i = 0; i < count; i++) {
					if (!active[i]) continue;
					previous[i] = positions[i];
					if (pinned[i]) {
						positions[i] = glm::mix(pinFrom[i], pinTo[i], fraction);
						continue;
					}
					glm::vec3& v = velocities[i];
					v += h * (environment.gravity + windAcceleration[i]);
					v *= keep;
					const float speed = glm::length(v);
					if (speed > 60.0f) v *= 60.0f / speed;
					positions[i] += h * v;
				}
				// The mouse's hold
				if (!grabbed.empty()) {
					const float alpha = GRAB_COMPLIANCE / (h * h);
					for (const Grabbed& g : grabbed) {
						const float w = inverseMass[g.particle];
						if (w <= 0.0f) continue;
						positions[g.particle] += (grabTarget + g.offset - positions[g.particle]) * (w / (w + alpha));
					}
				}
				// Symmetric Gauss-Seidel: forwards, then backwards. One sweep one way only leaves errors that the velocity
				// update (position change / h) carries on and grows, on a large cloth, until it crumples by itself.
				SolveDistance(stretch, built.stretchCompliance, h, false);
				SolveDistance(stretch, built.stretchCompliance, h, true);
				SolveDistance(bend, built.bendCompliance, h, false);
				SolveDistance(bend, built.bendCompliance, h, true);
				SolveTethers();
				if (built.selfCollision) {
					CollideSelf();
				}
				Collide(environment, colliders, fraction, h);
				for (size_t i = 0; i < count; i++) {
					if (active[i]) velocities[i] = (positions[i] - previous[i]) / h;
				}
			}
			lastWorld = world;

			age += dt;
			if (built.tearable && age > 1.0f) {
				Tear();
			}
			if (topologyChanged) {
				RebuildConstraints();
			}

			bool first = true;
			for (size_t i = 0; i < positions.size(); i++) {
				if (!active[i]) continue;
				if (first) { boundsLow = boundsHigh = positions[i]; first = false; }
				boundsLow = glm::min(boundsLow, positions[i]);
				boundsHigh = glm::max(boundsHigh, positions[i]);
			}
			const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
			stats.milliseconds = stats.milliseconds > 0.0f ? glm::mix(stats.milliseconds, ms, 0.1f) : ms;
		}

		void ClothSimulation::KillTriangle(uint32_t t) {
			if (!alive[t]) return;
			alive[t] = 0;
			for (int k = 0; k < 3; k++) {
				auto& list = particleTriangles[triangles[t][k]];
				list.erase(std::remove(list.begin(), list.end(), t), list.end());
			}
			topologyChanged = true;
		}

		bool ClothSimulation::Split(uint32_t v, uint32_t other) {
			if (positions.size() >= maxParticles) return false;
			// The crack runs through v across the pulled edge: triangles towards the other end go with a copy of v
			const glm::vec2 pull = rest[other] - rest[v];
			std::vector<uint32_t> towards, away;
			for (uint32_t t : particleTriangles[v]) {
				const glm::uvec3& tri = triangles[t];
				const glm::vec2 centre = (rest[tri.x] + rest[tri.y] + rest[tri.z]) / 3.0f;
				(glm::dot(centre - rest[v], pull) > 1e-7f * spacing * spacing ? towards : away).push_back(t);
			}
			if (towards.empty() || away.empty()) return false;
			const uint32_t copy = static_cast<uint32_t>(positions.size());
			positions.push_back(positions[v]);
			previous.push_back(previous[v]);
			velocities.push_back(velocities[v]);
			framePositions.push_back(framePositions[v]);
			windAcceleration.push_back(windAcceleration[v]);
			rest.push_back(rest[v]);
			mass.push_back(mass[v]);
			inverseMass.push_back(inverseMass[v]);
			groundHeight.push_back(groundHeight[v]);
			pinned.push_back(pinned[v]);
			active.push_back(1);
			for (uint32_t t : towards) {
				for (int k = 0; k < 3; k++) {
					if (triangles[t][k] == v) triangles[t][k] = copy;
				}
			}
			particleTriangles.push_back(towards);
			particleTriangles[v] = away;
			topologyChanged = true;
			return true;
		}

		void ClothSimulation::Tear() {
			const float limit = std::max(built.tearStrain, 1.01f);
			struct Candidate { float strain; uint32_t a, b; };
			std::vector<Candidate> candidates;
			std::vector<uint64_t> over;
			for (const Constraint& c : stretch) {
				if (c.rest <= 0.0f || !active[c.a] || !active[c.b]) continue;
				const float strain = glm::length(positions[c.a] - positions[c.b]) / c.rest;
				if (strain > limit) {
					const uint64_t key = (static_cast<uint64_t>(c.a) << 32) | c.b;
					over.push_back(key);
					if (std::binary_search(strained.begin(), strained.end(), key)) candidates.push_back({ strain, c.a, c.b });
				}
			}
			std::sort(over.begin(), over.end());
			strained = std::move(over);
			if (candidates.empty()) return;
			std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) { return x.strain > y.strain; });
			uint32_t done = 0;
			for (const Candidate& c : candidates) {
				if (done >= MAX_TEARS_PER_STEP) break;
				// Still joined? (an earlier split this frame may have parted them)
				std::vector<uint32_t> shared;
				for (uint32_t t : particleTriangles[c.a]) {
					const glm::uvec3& tri = triangles[t];
					if (tri.x == c.b || tri.y == c.b || tri.z == c.b) shared.push_back(t);
				}
				if (shared.empty()) continue;
				// Split where the cloth is whole around (an inner vertex), else at the other end, else let the edge go
				uint32_t first = c.a, second = c.b;
				if (particleTriangles[c.b].size() > particleTriangles[c.a].size()) std::swap(first, second);
				if (!Split(first, second) && !Split(second, first)) {
					for (uint32_t t : shared) KillTriangle(t);
				}
				done++;
				stats.torn++;
			}
		}

		bool ClothSimulation::HitTriangle(const glm::vec3& origin, const glm::vec3& direction, float& distance, uint32_t& triangle, glm::vec3& weights) const {
			float best = std::numeric_limits<float>::max();
			bool hit = false;
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::vec3 p0 = positions[triangles[t].x], p1 = positions[triangles[t].y], p2 = positions[triangles[t].z];
				const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
				const glm::vec3 p = glm::cross(direction, e2);
				const float det = glm::dot(e1, p);
				if (std::abs(det) < 1e-12f) continue;
				const float inv = 1.0f / det;
				const glm::vec3 s = origin - p0;
				const float u = glm::dot(s, p) * inv;
				if (u < 0.0f || u > 1.0f) continue;
				const glm::vec3 q = glm::cross(s, e1);
				const float v = glm::dot(direction, q) * inv;
				if (v < 0.0f || u + v > 1.0f) continue;
				const float d = glm::dot(e2, q) * inv;
				if (d > 0.0f && d < best) {
					best = d;
					triangle = t;
					weights = glm::vec3(1.0f - u - v, u, v);
					hit = true;
				}
			}
			distance = best;
			return hit;
		}

		bool ClothSimulation::Raycast(const glm::vec3& origin, const glm::vec3& direction, float& distance) const {
			uint32_t t;
			glm::vec3 w;
			return HitTriangle(origin, direction, distance, t, w);
		}

		bool ClothSimulation::Grab(const glm::vec3& origin, const glm::vec3& direction, float radius) {
			float distance;
			uint32_t t;
			glm::vec3 w;
			grabbed.clear();
			if (!HitTriangle(origin, direction, distance, t, w)) return false;
			const glm::vec3 point = origin + direction * distance;
			grabDistance = distance;
			grabTarget = point;
			for (uint32_t i = 0; i < positions.size(); i++) {
				if (active[i] && glm::distance(positions[i], point) < radius) {
					grabbed.push_back({ i, positions[i] - point });
				}
			}
			if (grabbed.empty()) {
				const int k = w.x >= w.y && w.x >= w.z ? 0 : w.y >= w.z ? 1 : 2;
				const uint32_t i = triangles[t][k];
				grabbed.push_back({ i, positions[i] - point });
			}
			return true;
		}

		void ClothSimulation::Drag(const glm::vec3& origin, const glm::vec3& direction) {
			if (!grabbed.empty()) grabTarget = origin + direction * grabDistance;
		}

		bool ClothSimulation::Cut(const glm::vec3& origin, const glm::vec3& direction, float radius) {
			float distance;
			uint32_t hitTriangle;
			glm::vec3 w;
			if (!HitTriangle(origin, direction, distance, hitTriangle, w)) return false;
			const glm::vec3 point = origin + direction * distance;
			const float squared = radius * radius;
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::uvec3& tri = triangles[t];
				const glm::vec3 centre = (positions[tri.x] + positions[tri.y] + positions[tri.z]) / 3.0f;
				const glm::vec3 d = centre - point;
				if (t == hitTriangle || glm::dot(d, d) < squared) {
					KillTriangle(t);
					stats.cut++;
				}
			}
			grabbed.erase(std::remove_if(grabbed.begin(), grabbed.end(), [&](const Grabbed& g) { return particleTriangles[g.particle].empty(); }), grabbed.end());
			RebuildConstraints();
			return true;
		}

		void ClothSimulation::WriteVertices(std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<glm::vec4>& previousPositions) const {
			const size_t count = positions.size();
			// Smooth normals and tangents (along u) per particle, from its triangles as they are now
			std::vector<glm::vec3> normals(count, glm::vec3(0.0f)), tangents(count, glm::vec3(0.0f));
			std::vector<float> handedness(count, 1.0f);
			for (uint32_t t = 0; t < triangles.size(); t++) {
				if (!alive[t]) continue;
				const glm::uvec3& tri = triangles[t];
				const glm::vec3 e1 = positions[tri.y] - positions[tri.x], e2 = positions[tri.z] - positions[tri.x];
				const glm::vec3 n = glm::cross(e1, e2);
				// uv of the sheet: u across, v down (the image's top at the top edge)
				const glm::vec2 d1 = (rest[tri.y] - rest[tri.x]) / glm::vec2(built.width, built.height);
				const glm::vec2 d2 = (rest[tri.z] - rest[tri.x]) / glm::vec2(built.width, built.height);
				const float det = Cross2(d1, d2);
				glm::vec3 tangent(0.0f);
				float sign = 1.0f;
				if (std::abs(det) > 1e-12f) {
					const float r = 1.0f / det;
					tangent = (e1 * d2.y - e2 * d1.y) * r;
					const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * r;
					sign = glm::dot(glm::cross(n, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
				}
				for (int k = 0; k < 3; k++) {
					normals[tri[k]] += n;
					tangents[tri[k]] += tangent * glm::length(n);
					handedness[tri[k]] = sign;
				}
			}
			const float hidden = std::numeric_limits<float>::quiet_NaN();
			vertices.resize(triangles.size() * 3);
			previousPositions.resize(triangles.size() * 3);
			for (uint32_t t = 0; t < triangles.size(); t++) {
				for (int k = 0; k < 3; k++) {
					Fox::Graphics::Vulkan::Vertex& vertex = vertices[t * 3 + k];
					vertex = Fox::Graphics::Vulkan::Vertex{};
					vertex.position[3] = 1.0f;
					if (!alive[t]) {
						vertex.position[0] = vertex.position[1] = vertex.position[2] = hidden;
						previousPositions[t * 3 + k] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
						continue;
					}
					const uint32_t i = triangles[t][k];
					const glm::vec3 p = positions[i];
					glm::vec3 n = normals[i];
					const float nl = glm::length(n);
					n = nl > 1e-12f ? n / nl : glm::vec3(0.0f, 0.0f, 1.0f);
					glm::vec3 tangent = tangents[i] - n * glm::dot(tangents[i], n);
					const float tl = glm::length(tangent);
					tangent = tl > 1e-12f ? tangent / tl : glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.position[0] = p.x; vertex.position[1] = p.y; vertex.position[2] = p.z;
					vertex.normal[0] = n.x; vertex.normal[1] = n.y; vertex.normal[2] = n.z;
					vertex.uv[0] = rest[i].x / built.width;
					vertex.uv[1] = rest[i].y / built.height;
					vertex.tangent[0] = tangent.x; vertex.tangent[1] = tangent.y; vertex.tangent[2] = tangent.z; vertex.tangent[3] = handedness[i];
					previousPositions[t * 3 + k] = glm::vec4(framePositions[i], 1.0f);
				}
			}
		}
	}
}
