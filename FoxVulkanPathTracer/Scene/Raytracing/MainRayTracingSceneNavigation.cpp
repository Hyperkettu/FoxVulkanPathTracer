// Navigation: agents (NavAgentComponent) walking over the ground on paths found with A* (Scene/Navigation.h)

#include "FoxRenderer.h"

#include <chrono>

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			namespace {
				using Fox::Scene::NavAgentComponent;
				using Fox::Scene::MeshComponent;
				const glm::vec3 UP(0.0f, 1.0f, 0.0f);
				constexpr int MAX_CELLS = 1024; // A side of the grid at most

				// Whether the segment from a to b meets the box
				bool SegmentHitsBox(const glm::vec3& a, const glm::vec3& b, const glm::vec3& low, const glm::vec3& high) {
					float t0 = 0.0f, t1 = 1.0f;
					const glm::vec3 d = b - a;
					for (int axis = 0; axis < 3; axis++) {
						if (std::abs(d[axis]) < 1e-9f) {
							if (a[axis] < low[axis] || a[axis] > high[axis]) {
								return false;
							}
							continue;
						}
						float tNear = (low[axis] - a[axis]) / d[axis], tFar = (high[axis] - a[axis]) / d[axis];
						if (tNear > tFar) {
							std::swap(tNear, tFar);
						}
						t0 = std::max(t0, tNear);
						t1 = std::min(t1, tFar);
						if (t0 > t1) {
							return false;
						}
					}
					return true;
				}

				// Whether a triangle (seen from above) overlaps the square from low to high: separating axes
				bool TriangleOverlapsSquare(const glm::vec2 (&v)[3], const glm::vec2& low, const glm::vec2& high) {
					if (std::max({ v[0].x, v[1].x, v[2].x }) < low.x || std::min({ v[0].x, v[1].x, v[2].x }) > high.x
						|| std::max({ v[0].y, v[1].y, v[2].y }) < low.y || std::min({ v[0].y, v[1].y, v[2].y }) > high.y) {
						return false;
					}
					const glm::vec2 corners[4] = { low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y) };
					for (int e = 0; e < 3; e++) {
						const glm::vec2 edge = v[(e + 1) % 3] - v[e];
						const glm::vec2 axis(-edge.y, edge.x);
						if (glm::dot(axis, axis) < 1e-14f) {
							continue;
						}
						float t0 = 1e30f, t1 = -1e30f, s0 = 1e30f, s1 = -1e30f;
						for (int i = 0; i < 3; i++) {
							const float t = glm::dot(v[i], axis);
							t0 = std::min(t0, t);
							t1 = std::max(t1, t);
						}
						for (const glm::vec2& corner : corners) {
							const float t = glm::dot(corner, axis);
							s0 = std::min(s0, t);
							s1 = std::max(s1, t);
						}
						if (t1 < s0 || s1 < t0) {
							return false;
						}
					}
					return true;
				}

				float WrapAngle(float angle) {
					while (angle > glm::pi<float>()) angle -= glm::two_pi<float>();
					while (angle < -glm::pi<float>()) angle += glm::two_pi<float>();
					return angle;
				}
			}

			bool MainRayTracingScene::RaycastGround(const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit) const {
				if (environment.terrain && terrainPlaced) {
					return terrain.Raycast(origin, direction, hit);
				}
				if (!environment.ground || std::abs(direction.y) < 1e-6f) {
					return false;
				}
				const float t = (environment.groundHeight - origin.y) / direction.y;
				if (t <= 0.0f) {
					return false;
				}
				hit = origin + direction * t;
				return true;
			}

			bool MainRayTracingScene::IsNavAgentPart(Entity entity) const {
				for (Entity e = entity; graph.IsValid(e); e = graph.GetParent(e)) {
					const NavAgentComponent* agent = graph.GetRegistry().TryGet<NavAgentComponent>(e);
					if (agent && agent->enabled) {
						return true;
					}
				}
				return false;
			}

			const MainRayTracingScene::ModelShape* MainRayTracingScene::ModelShapeOf(const std::string& file) {
				auto found = modelShapes.find(file);
				if (found != modelShapes.end()) {
					return found->second.get();
				}
				std::shared_ptr<ModelShape> shape;
				try {
					const auto& model = LoadModel(file, false);
					shape = std::make_shared<ModelShape>();
					const auto& vertices = model.GetVertices();
					shape->positions.reserve(vertices.size());
					for (const auto& vertex : vertices) {
						shape->positions.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2]);
					}
					// Each submesh where its node puts it (as RayTracingScene::AddMeshGeometry bakes it)
					const auto& submeshes = model.GetSubmeshesForGPU();
					const auto& instances = model.GetInstanceData();
					for (size_t i = 0; i < submeshes.size() && i < instances.size(); i++) {
						const glm::mat4& node = instances[i].transform;
						if (node == glm::mat4(1.0f)) {
							continue;
						}
						const size_t first = submeshes[i].vertexOffset;
						const size_t end = std::min(shape->positions.size(), first + submeshes[i].vertexCount);
						for (size_t v = first; v < end; v++) {
							shape->positions[v] = glm::vec3(node * glm::vec4(shape->positions[v], 1.0f));
						}
					}
					shape->indices.assign(model.GetIndices().begin(), model.GetIndices().end());
					shape->low = glm::vec3(1e30f);
					shape->high = glm::vec3(-1e30f);
					for (const glm::vec3& p : shape->positions) {
						shape->low = glm::min(shape->low, p);
						shape->high = glm::max(shape->high, p);
					}
					if (shape->positions.empty() || shape->indices.size() < 3) {
						shape.reset();
					}
				}
				catch (const std::exception& e) {
					std::cout << "Navigation: " << file << " could not be loaded (" << e.what() << "): not an obstacle\n";
					shape.reset();
				}
				modelShapes[file] = shape;
				return shape.get();
			}

			Fox::Scene::NavigationGrid::Result MainRayTracingScene::FindPath(Entity agent, const glm::vec3& from, const glm::vec3& to, std::string& status,
				std::shared_ptr<const Fox::Scene::NavigationGrid>* keep) {
				using Grid = Fox::Scene::NavigationGrid;
				const auto started = std::chrono::steady_clock::now();
				Grid::Result result;
				auto& registry = graph.GetRegistry();
				const NavAgentComponent defaults;
				const NavAgentComponent* settings = registry.TryGet<NavAgentComponent>(agent);
				if (!settings) {
					settings = &defaults;
				}

				// The grid: over the terrain, or over the ground plane around the way there
				const bool onTerrain = environment.terrain && terrainPlaced && terrain.IsCreated();
				if (!onTerrain && !environment.ground) {
					status = "No ground to walk on (Environment: ground plane or terrain)";
					return result;
				}
				glm::vec2 corner, extent;
				if (onTerrain) {
					const float size = terrain.settings.size;
					const glm::vec2 terrainLow = glm::vec2(terrain.settings.position.x, terrain.settings.position.z) - 0.5f * size;
					const glm::vec2 terrainHigh = terrainLow + size;
					const glm::vec2 a(from.x, from.z), b(to.x, to.z);
					if (glm::any(glm::lessThan(glm::min(a, b), terrainLow)) || glm::any(glm::greaterThan(glm::max(a, b), terrainHigh))) {
						status = "Off the terrain: agents walk on it";
						return result;
					}
					// Around the way (room to go round what is in it), within the terrain
					const float margin = std::max(30.0f, 0.5f * glm::distance(a, b));
					corner = glm::max(glm::min(a, b) - margin, terrainLow);
					extent = glm::min(glm::max(a, b) + margin, terrainHigh) - corner;
				}
				else {
					const glm::vec2 a(from.x, from.z), b(to.x, to.z);
					const float margin = std::max(20.0f, 0.5f * glm::distance(a, b));
					corner = glm::min(a, b) - margin;
					extent = glm::max(a, b) + margin - corner;
				}
				float cell = glm::clamp(settings->cellSize, 0.1f, 5.0f);
				cell = std::max(cell, std::max(extent.x, extent.y) / MAX_CELLS);
				const int columns = std::max(1, static_cast<int>(std::ceil(extent.x / cell)));
				const int rows = std::max(1, static_cast<int>(std::ceil(extent.y / cell)));
				auto gridPointer = std::make_shared<Grid>();
				Grid& grid = *gridPointer;
				grid.Create(corner, columns, rows, cell);
				float lowestGround = 1e30f, highestGround = -1e30f;
				for (int r = 0; r < rows; r++) {
					for (int c = 0; c < columns; c++) {
						const glm::vec2 p = grid.CellCentre(c, r);
						const float y = GetGroundHeightAt(p.x, p.y);
						grid.Height(c, r) = y;
						lowestGround = std::min(lowestGround, y);
						highestGround = std::max(highestGround, y);
					}
				}

				const float step = std::max(settings->maxStep, 0.0f);
				const float height = std::max(settings->height, step + 0.05f);

				// Obstacles: what stands up into the space it walks through, higher above the ground than a step and
				// lower than its head. Models by their triangles, spheres by their sphere, trees by their trunk, and
				// physics bodies where they are now. Not agents: they move.
				auto cellRange = [&](const glm::vec2& low, const glm::vec2& high, int& c0, int& r0, int& c1, int& r1) {
					grid.CellAt(low.x, low.y, c0, r0);
					grid.CellAt(high.x, high.y, c1, r1);
					c0 = std::max(c0, 0);
					r0 = std::max(r0, 0);
					c1 = std::min(c1, columns - 1);
					r1 = std::min(r1, rows - 1);
					return c0 <= c1 && r0 <= r1;
				};
				// Blocks the cell if something spans heights low..high over it, in the way
				auto blockSpan = [&](int c, int r, float low, float high) {
					const float ground = grid.Height(c, r);
					if (high > ground + step && low < ground + height) {
						grid.Block(c, r);
						return true;
					}
					return false;
				};
				const float halfCell = 0.5f * cell;
				std::vector<glm::vec3> placed; // A model's vertices in the world
				size_t obstacles = 0, triangles = 0;
				registry.Each<MeshComponent>([&](Entity entity, MeshComponent& mesh) {
					if (!mesh.enabled || entity == agent || graph.IsAncestorOf(agent, entity) || !graph.IsActiveInHierarchy(entity) || IsNavAgentPart(entity)) {
						return;
					}
					const glm::mat4 world = graph.GetWorldMatrix(entity);
					const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
					bool blocks = false;
					int c0, r0, c1, r1;

					if (mesh.type == MeshComponent::Type::Sphere) {
						const glm::vec3 centre(world[3]);
						const float radius = mesh.radius * std::max({ scale.x, scale.y, scale.z });
						if (centre.y - radius > highestGround + height || centre.y + radius < lowestGround + step
							|| !cellRange(glm::vec2(centre.x, centre.z) - radius, glm::vec2(centre.x, centre.z) + radius, c0, r0, c1, r1)) {
							return;
						}
						for (int r = r0; r <= r1; r++) {
							for (int c = c0; c <= c1; c++) {
								// Its nearest point of the cell to the centre (a disc as wide as the cell around it)
								const float d = std::max(0.0f, glm::distance(grid.CellCentre(c, r), glm::vec2(centre.x, centre.z)) - halfCell * 1.4142f);
								if (d < radius) {
									const float half = std::sqrt(radius * radius - d * d);
									blocks |= blockSpan(c, r, centre.y - half, centre.y + half);
								}
							}
						}
					}
					else if (mesh.type == MeshComponent::Type::Tree) {
						if (!mesh.tree) {
							return;
						}
						// The trunk (the crown is above it), grown by half a cell so a thin one is not missed
						const auto& trunk = mesh.tree->shape;
						const float radius = std::max(0.05f, trunk.trunkRadius * 0.5f * (1.0f + trunk.trunkTaper));
						const glm::vec3 grow = halfCell / glm::max(scale, glm::vec3(1e-4f));
						const glm::vec3 low = glm::vec3(-radius, 0.0f, -radius) - grow;
						const glm::vec3 high = glm::vec3(radius, trunk.trunkLength, radius) + grow;
						glm::vec3 worldLow(1e30f), worldHigh(-1e30f);
						for (int i = 0; i < 8; i++) {
							const glm::vec3 p = glm::vec3(world * glm::vec4((i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z, 1.0f));
							worldLow = glm::min(worldLow, p);
							worldHigh = glm::max(worldHigh, p);
						}
						if (!cellRange(glm::vec2(worldLow.x, worldLow.z), glm::vec2(worldHigh.x, worldHigh.z), c0, r0, c1, r1)) {
							return;
						}
						const glm::mat4 toLocal = glm::inverse(world);
						for (int r = r0; r <= r1; r++) {
							for (int c = c0; c <= c1; c++) {
								const glm::vec2 p = grid.CellCentre(c, r);
								const float ground = grid.Height(c, r);
								const glm::vec3 a = glm::vec3(toLocal * glm::vec4(p.x, ground + step, p.y, 1.0f));
								const glm::vec3 b = glm::vec3(toLocal * glm::vec4(p.x, ground + height, p.y, 1.0f));
								if (SegmentHitsBox(a, b, low, high)) {
									grid.Block(c, r);
									blocks = true;
								}
							}
						}
					}
					else {
						const ModelShape* shape = mesh.file.empty() ? nullptr : ModelShapeOf(mesh.file);
						if (!shape) {
							return;
						}
						// Quickly out when its bounds miss the grid or the heights it walks through
						glm::vec3 worldLow(1e30f), worldHigh(-1e30f);
						for (int i = 0; i < 8; i++) {
							const glm::vec3 p = glm::vec3(world * glm::vec4((i & 1) ? shape->high.x : shape->low.x,
								(i & 2) ? shape->high.y : shape->low.y, (i & 4) ? shape->high.z : shape->low.z, 1.0f));
							worldLow = glm::min(worldLow, p);
							worldHigh = glm::max(worldHigh, p);
						}
						if (worldLow.y > highestGround + height || worldHigh.y < lowestGround + step
							|| !cellRange(glm::vec2(worldLow.x, worldLow.z), glm::vec2(worldHigh.x, worldHigh.z), c0, r0, c1, r1)) {
							return;
						}
						placed.resize(shape->positions.size());
						for (size_t i = 0; i < placed.size(); i++) {
							placed[i] = glm::vec3(world * glm::vec4(shape->positions[i], 1.0f));
						}
						// Every triangle blocks the cells it covers (from above) where it is at a height in the way
						for (size_t t = 0; t + 2 < shape->indices.size(); t += 3) {
							const glm::vec3& a = placed[shape->indices[t]];
							const glm::vec3& b = placed[shape->indices[t + 1]];
							const glm::vec3& d = placed[shape->indices[t + 2]];
							const float top = std::max({ a.y, b.y, d.y }), bottom = std::min({ a.y, b.y, d.y });
							if (top <= lowestGround + step || bottom >= highestGround + height) {
								continue;
							}
							const glm::vec2 flat[3] = { glm::vec2(a.x, a.z), glm::vec2(b.x, b.z), glm::vec2(d.x, d.z) };
							int tc0, tr0, tc1, tr1;
							if (!cellRange(glm::min(glm::min(flat[0], flat[1]), flat[2]), glm::max(glm::max(flat[0], flat[1]), flat[2]), tc0, tr0, tc1, tr1)) {
								continue;
							}
							triangles++;
							const glm::vec3 normal = glm::cross(b - a, d - a);
							const float length = glm::length(normal);
							// Not steep: its height over the cell from its plane; steep (a wall): all of its heights
							const bool sloped = length > 0.0f && std::abs(normal.y) > 0.2f * length;
							auto planeHeight = [&](float x, float z) {
								return glm::clamp(a.y - (normal.x * (x - a.x) + normal.z * (z - a.z)) / normal.y, bottom, top);
							};
							const bool single = tc0 == tc1 && tr0 == tr1;
							for (int r = tr0; r <= tr1; r++) {
								for (int c = tc0; c <= tc1; c++) {
									const glm::vec2 centre = grid.CellCentre(c, r);
									const glm::vec2 low = centre - halfCell, high = centre + halfCell;
									if (!single && !TriangleOverlapsSquare(flat, low, high)) {
										continue;
									}
									float spanLow = bottom, spanHigh = top;
									if (sloped) {
										const float h[4] = { planeHeight(low.x, low.y), planeHeight(high.x, low.y), planeHeight(low.x, high.y), planeHeight(high.x, high.y) };
										spanLow = std::min({ h[0], h[1], h[2], h[3] });
										spanHigh = std::max({ h[0], h[1], h[2], h[3] });
									}
									blocks |= blockSpan(c, r, spanLow, spanHigh);
								}
							}
						}
					}
					obstacles += blocks ? 1 : 0;
				});

				// Water deeper than it wades through
				registry.Each<Fox::Scene::WaterComponent>([&](Entity entity, Fox::Scene::WaterComponent& water) {
					if (!water.enabled || !graph.IsActiveInHierarchy(entity)) {
						return;
					}
					const glm::vec3 centre(graph.GetWorldMatrix(entity)[3]);
					const glm::vec2 half = 0.5f * water.size;
					int c0, r0, c1, r1;
					grid.CellAt(centre.x - half.x, centre.z - half.y, c0, r0);
					grid.CellAt(centre.x + half.x, centre.z + half.y, c1, r1);
					c0 = std::max(c0, 0);
					r0 = std::max(r0, 0);
					c1 = std::min(c1, columns - 1);
					r1 = std::min(r1, rows - 1);
					for (int r = r0; r <= r1; r++) {
						for (int c = c0; c <= c1; c++) {
							const glm::vec2 p = grid.CellCentre(c, r);
							float level = centre.y;
							glm::vec3 normal;
							glm::vec2 flow;
							if (water.placed && water.simulation) {
								float surface;
								if (water.simulation->SampleSurface(p.x, p.y, surface, normal, flow)) {
									level = surface;
								}
							}
							if (level - grid.Height(c, r) > settings->maxWaterDepth) {
								grid.Block(c, r);
							}
						}
					}
				});

				grid.Inflate(std::max(settings->radius, 0.0f));

				Grid::Rules rules;
				rules.maxSlope = std::tan(glm::radians(glm::clamp(settings->maxSlope, 1.0f, 89.0f)));
				rules.maxStep = step;
				result = grid.FindPath(from, to, rules);
				if (keep) {
					*keep = gridPointer;
				}

				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
				char text[256];
				if (!result.found) {
					std::snprintf(text, sizeof(text), "No way: nowhere open to walk near it (%.1f ms)", ms);
				}
				else {
					float length = 0.0f;
					for (size_t i = 1; i < result.points.size(); i++) {
						length += glm::distance(result.points[i - 1], result.points[i]);
					}
					std::snprintf(text, sizeof(text), "%s%.1f m, %zu turns (%dx%d cells of %.2f m, %zu obstacles from %zu triangles, %zu searched, %.1f ms)",
						result.reachesGoal ? "" : "Cannot reach the target, going as near as it can: ", length,
						result.points.size() > 2 ? result.points.size() - 2 : 0, columns, rows, cell, obstacles, triangles, result.expanded, ms);
				}
				status = text;
				return result;
			}

			void MainRayTracingScene::SetNavTarget(Entity agent, const glm::vec3& target) {
				if (!graph.IsValid(agent)) {
					return;
				}
				auto& registry = graph.GetRegistry();
				NavAgentComponent* nav = registry.TryGet<NavAgentComponent>(agent);
				if (!nav) {
					nav = &registry.Add<NavAgentComponent>(agent);
				}
				nav->target = target;
				nav->hasTarget = true;
				nav->Invalidate();
				nav->replanTimer = 0.0f;
			}

			void MainRayTracingScene::InvalidatePaths() {
				graph.GetRegistry().Each<NavAgentComponent>([](Entity, NavAgentComponent& nav) {
					nav.Invalidate();
					nav.replanTimer = 0.0f;
				});
			}

			// --- Audio ---

			namespace {
				Fox::Audio::AudioEngine::Vec3 ToAudio(const glm::vec3& v) { return { v.x, v.y, v.z }; }
			}

			bool MainRayTracingScene::PlayEntitySound(Entity entity) {
				Fox::Audio::AudioEngine* engine = GetAudio();
				auto* sound = graph.IsValid(entity) ? graph.GetRegistry().TryGet<Fox::Scene::SoundComponent>(entity) : nullptr;
				if (!engine || !sound || !sound->enabled || sound->file.empty()) {
					return false;
				}
				if (sound->voice != 0) {
					engine->Stop(sound->voice);
				}
				if (sound->music) {
					sound->voice = engine->PlayMusic(sound->file, sound->loop, sound->volume, 0.5f);
				}
				else {
					Fox::Audio::AudioEngine::PlayOptions options;
					options.volume = sound->volume;
					options.pitch = sound->pitch;
					options.loop = sound->loop;
					options.spatial = sound->spatial;
					options.position = ToAudio(glm::vec3(graph.GetWorldMatrix(entity)[3]));
					options.minDistance = sound->minDistance;
					options.maxDistance = sound->maxDistance;
					sound->voice = engine->Play(sound->file, options);
				}
				return sound->voice != 0;
			}

			void MainRayTracingScene::StopEntitySound(Entity entity, float fadeSeconds) {
				auto* sound = graph.IsValid(entity) ? graph.GetRegistry().TryGet<Fox::Scene::SoundComponent>(entity) : nullptr;
				if (!sound || sound->voice == 0) {
					return;
				}
				if (Fox::Audio::AudioEngine* engine = GetAudio()) {
					if (sound->music && engine->GetMusic() == sound->voice) {
						engine->StopMusic(fadeSeconds);
					}
					else {
						engine->Stop(sound->voice, fadeSeconds);
					}
				}
				sound->voice = 0;
			}

			bool MainRayTracingScene::IsEntitySoundPlaying(Entity entity) const {
				const auto* sound = graph.IsValid(entity) ? graph.GetRegistry().TryGet<Fox::Scene::SoundComponent>(entity) : nullptr;
				return sound && sound->voice != 0 && audio && audio->IsPlaying(sound->voice);
			}

			void MainRayTracingScene::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
				if (Fox::Audio::AudioEngine* engine = GetAudio()) {
					engine->SetListener(ToAudio(position), ToAudio(forward), ToAudio(up));
				}
			}

			void MainRayTracingScene::UpdateAudio() {
				Fox::Audio::AudioEngine* engine = GetAudio();
				if (!engine) {
					return;
				}
				// Sounds in the world follow their entities; finished ones are forgotten; the game's pause pauses them
				graph.GetRegistry().Each<Fox::Scene::SoundComponent>([&](Entity entity, Fox::Scene::SoundComponent& sound) {
					if (sound.voice == 0) {
						return;
					}
					if (!engine->IsPlaying(sound.voice) || !sound.enabled || !graph.IsActiveInHierarchy(entity)) {
						if (engine->IsPlaying(sound.voice)) {
							engine->Stop(sound.voice);
						}
						sound.voice = 0;
						return;
					}
					if (sound.spatial && !sound.music) {
						engine->SetPosition(sound.voice, ToAudio(glm::vec3(graph.GetWorldMatrix(entity)[3])));
					}
				});
				engine->SetPaused(physicsPlaying && clock.paused);
				engine->Update();
			}

			std::vector<std::string> MainRayTracingScene::ListSounds() const {
				std::vector<std::string> files;
				std::error_code failed;
				for (const auto& entry : std::filesystem::recursive_directory_iterator("Sounds", failed)) {
					std::string extension = entry.path().extension().string();
					std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (extension == ".mp3" || extension == ".wav" || extension == ".flac") {
						files.push_back(entry.path().generic_string());
					}
				}
				std::sort(files.begin(), files.end());
				return files;
			}

			// --- Game camera ---

			MainRayTracingScene::Entity MainRayTracingScene::FindGameCamera() const {
				Entity best;
				int bestPriority = INT32_MIN;
				graph.GetRegistry().Each<Fox::Scene::CameraComponent>([&](Entity entity, const Fox::Scene::CameraComponent& camera) {
					if (camera.enabled && graph.IsActiveInHierarchy(entity) && camera.priority > bestPriority) {
						best = entity;
						bestPriority = camera.priority;
					}
				});
				return best;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddGameCamera(const glm::vec3& position, const glm::vec3& forward, float fovDegrees) {
				size_t count = 0;
				graph.GetRegistry().Each<Fox::Scene::CameraComponent>([&](Entity, Fox::Scene::CameraComponent&) { count++; });
				const Entity entity = graph.CreateEntity(count == 0 ? "Game camera" : "Game camera " + std::to_string(count + 1));
				Fox::Scene::Transform transform;
				transform.position = position;
				transform.rotation = RotationTowards(-forward); // Its -Z along forward
				graph.SetWorldTransform(entity, transform);
				graph.GetRegistry().Add<Fox::Scene::CameraComponent>(entity).fov = fovDegrees;
				return entity;
			}

			// --- Volumetrics (here for want of a better file: short) ---

			size_t MainRayTracingScene::FillVolumeConstants(Fox::Graphics::Vulkan::CameraUBO& constants) const {
				const Fog& fog = environment.fog;
				constants.fog = glm::vec4(std::max(fog.density, 0.0f), std::max(fog.falloff, 0.0f), fog.baseHeight, glm::clamp(fog.anisotropy, -0.95f, 0.95f));
				constants.fogColor = glm::vec4(glm::clamp(fog.color, glm::vec3(0.0f), glm::vec3(1.0f)), fog.enabled ? 1.0f : 0.0f);
				const Clouds& clouds = environment.clouds;
				constants.cloudLayer = glm::vec4(clouds.bottom, std::max(clouds.top, clouds.bottom + 1.0f), glm::clamp(clouds.coverage, 0.0f, 1.0f), std::max(clouds.density, 0.0f));
				constants.cloudShape = glm::vec4(std::max(clouds.size, 1.0f), 0.0f, glm::clamp(clouds.anisotropy, -0.95f, 0.95f), clouds.enabled ? 1.0f : 0.0f);
				constants.cloudWind = glm::vec4(clouds.wind, glm::clamp(clouds.albedo, 0.0f, 1.0f));
				constants.cloudShadows = clouds.shadows ? 1u : 0u;

				size_t found = 0;
				uint32_t count = 0;
				graph.GetRegistry().Each<Fox::Scene::VolumeComponent>([&](Entity entity, const Fox::Scene::VolumeComponent& volume) {
					if (!volume.enabled || !graph.IsActiveInHierarchy(entity) || volume.density <= 0.0f) {
						return;
					}
					found++;
					if (count >= Fox::Graphics::Vulkan::MAX_VOLUMES) {
						return;
					}
					const glm::mat4 box = graph.GetWorldMatrix(entity) * glm::scale(glm::mat4(1.0f), glm::max(volume.size, glm::vec3(0.01f)));
					const glm::mat4 toLocal = glm::inverse(box);
					Fox::Graphics::Vulkan::VolumeGPU& gpu = constants.volumes[count++];
					for (int row = 0; row < 3; row++) {
						gpu.rows[row] = glm::vec4(toLocal[0][row], toLocal[1][row], toLocal[2][row], toLocal[3][row]);
					}
					gpu.colorDensity = glm::vec4(glm::clamp(volume.color, glm::vec3(0.0f), glm::vec3(1.0f)), volume.density);
					gpu.emission = glm::vec4(volume.glow * std::max(volume.glowStrength, 0.0f), glm::clamp(volume.anisotropy, -0.95f, 0.95f));
					gpu.motion = glm::vec4(volume.velocity, std::max(volume.noiseSize, 0.01f));
					gpu.shape = glm::vec4(glm::clamp(volume.noiseStrength, 0.0f, 1.0f), glm::clamp(volume.softness, 0.0f, 1.0f), volume.round ? 1.0f : 0.0f, 0.0f);
				});
				constants.volumeCount = count;
				return found;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddSmoke(const glm::vec3& position, bool glowing) {
				const Entity entity = graph.CreateEntity(glowing ? "Glowing smoke" : "Smoke");
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);
				auto& volume = graph.GetRegistry().Add<Fox::Scene::VolumeComponent>(entity);
				if (glowing) {
					volume.color = glm::vec3(0.25f, 0.22f, 0.2f);
					volume.glowStrength = 3.0f;
					volume.density = 0.8f;
					volume.velocity = glm::vec3(0.0f, 1.2f, 0.0f);
				}
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddNavAgent(const glm::vec3& position) {
				size_t count = 0;
				graph.GetRegistry().Each<NavAgentComponent>([&](Entity, NavAgentComponent&) { count++; });
				const float radius = 0.4f;
				const Entity entity = AddSphereObject(position + glm::vec3(0.0f, radius, 0.0f), radius,
					SurfaceMaterial{ glm::vec3(0.12f, 0.35f, 0.8f), 0.4f });
				graph.SetName(entity, "Agent " + std::to_string(count + 1));
				// A nose, to see where it faces (+z)
				const Entity nose = AddSphereObject(position + glm::vec3(0.0f, radius + 0.08f, radius * 0.9f), 0.12f,
					SurfaceMaterial{ glm::vec3(0.9f, 0.75f, 0.1f), 0.4f }, entity);
				graph.SetName(nose, "Nose");
				NavAgentComponent& nav = graph.GetRegistry().Add<NavAgentComponent>(entity);
				nav.radius = radius;
				return entity;
			}

			void MainRayTracingScene::StartNavigation() {
				graph.GetRegistry().Each<NavAgentComponent>([&](Entity entity, NavAgentComponent& nav) {
					Fox::Scene::Transform world = graph.GetWorldTransform(entity);
					nav.groundOffset = world.position.y - GetGroundHeightAt(world.position.x, world.position.z);
					nav.startRotation = world.rotation;
					nav.scale = world.scale;
					const glm::vec3 front = world.rotation * nav.ForwardAxis();
					nav.startYaw = (front.x * front.x + front.z * front.z) > 1e-8f ? std::atan2(front.z, front.x) : 0.0f;
					nav.yaw = nav.startYaw;
					nav.Invalidate();
				});
			}

			void MainRayTracingScene::UpdateNavigation(float dt) {
				auto& registry = graph.GetRegistry();
				if (meshesMoved) {
					obstacleGeneration++;
				}
				// Edit mode: once moved meshes have stood still a moment, the agents plan around where they are now
				if (!physicsPlaying) {
					if (meshesMoved) {
						pathsStale = true;
						meshesQuietFor = 0.0f;
					}
					else {
						meshesQuietFor += dt;
					}
					if (pathsStale && meshesQuietFor > 0.3f) {
						pathsStale = false;
						InvalidatePaths();
					}
				}
				meshesMoved = false;

				registry.Each<NavAgentComponent>([&](Entity entity, NavAgentComponent& nav) {
					if (!nav.enabled || !graph.IsActiveInHierarchy(entity)) {
						return;
					}
					if (!nav.hasTarget) {
						nav.path.clear();
						nav.status = "No target: select it and Shift + click the ground";
						return;
					}
					const glm::vec3 position(graph.GetWorldMatrix(entity)[3]);
					nav.replanTimer -= dt;

					auto plan = [&] {
						auto result = FindPath(entity, position, nav.target, nav.status, &nav.grid);
						nav.path = std::move(result.points);
						nav.next = 1;
						nav.planned = true;
						nav.reachable = result.reachesGoal;
						nav.plannedFrom = position;
						nav.arrived = nav.path.size() < 2;
						nav.replanTimer = physicsPlaying ? 1.0f : 0.3f;
						nav.plannedGeneration = obstacleGeneration;
					};

					if (!physicsPlaying) {
						// Its way from where it stands, again when it was moved (not more often than a few times a second)
						const glm::vec2 moved(position.x - nav.plannedFrom.x, position.z - nav.plannedFrom.z);
						if ((!nav.planned || glm::dot(moved, moved) > 0.25f * 0.25f) && nav.replanTimer <= 0.0f) {
							plan();
						}
						return;
					}

					// Playing: again when something it might bump into moved (bodies falling, rolling, pushed)
					if (!nav.planned || (nav.plannedGeneration != obstacleGeneration && nav.replanTimer <= 0.0f && !nav.arrived)) {
						plan();
					}
					if (nav.arrived || nav.next >= nav.path.size()) {
						if (!nav.arrived) {
							nav.arrived = true;
						}
						return;
					}

					// Turn towards the next waypoint; walk on as it faces it (slower while turning, not at all facing away)
					glm::vec3 p = position;
					const glm::vec3& waypoint = nav.path[nav.next];
					const glm::vec2 towards(waypoint.x - p.x, waypoint.z - p.z);
					if (glm::dot(towards, towards) > 1e-8f) {
						const float wanted = std::atan2(towards.y, towards.x);
						const float turn = WrapAngle(wanted - nav.yaw);
						const float most = glm::radians(std::max(nav.turnRate, 1.0f)) * dt;
						nav.yaw = WrapAngle(nav.yaw + glm::clamp(turn, -most, most));
					}
					float facing = 1.0f;
					if (glm::dot(towards, towards) > 1e-8f) {
						facing = std::cos(WrapAngle(std::atan2(towards.y, towards.x) - nav.yaw));
					}
					float left = std::max(nav.speed, 0.0f) * glm::clamp(facing, 0.0f, 1.0f) * dt;
					while (left > 0.0f && nav.next < nav.path.size()) {
						const glm::vec3& to = nav.path[nav.next];
						const glm::vec2 d(to.x - p.x, to.z - p.z);
						const float distance = glm::length(d);
						if (distance <= left) {
							p.x = to.x;
							p.z = to.z;
							left -= distance;
							nav.next++;
						}
						else {
							p.x += d.x / distance * left;
							p.z += d.y / distance * left;
							left = 0.0f;
						}
					}
					if (nav.next >= nav.path.size()) {
						nav.arrived = true;
						nav.status = nav.reachable ? "Arrived" : "As near as it can get";
					}

					// On the ground, as high above it as it started; turned to its heading (and tilted to the slope)
					p.y = GetGroundHeightAt(p.x, p.z) + nav.groundOffset;
					glm::quat rotation = glm::angleAxis(-(nav.yaw - nav.startYaw), UP) * nav.startRotation;
					if (nav.alignToGround) {
						const float e = 0.4f;
						const glm::vec3 normal = glm::normalize(glm::vec3(
							GetGroundHeightAt(p.x - e, p.z) - GetGroundHeightAt(p.x + e, p.z), 2.0f * e,
							GetGroundHeightAt(p.x, p.z - e) - GetGroundHeightAt(p.x, p.z + e)));
						const glm::vec3 axis = glm::cross(UP, normal);
						const float sine = glm::length(axis);
						if (sine > 1e-5f) {
							rotation = glm::angleAxis(std::atan2(sine, glm::dot(UP, normal)), axis / sine) * rotation;
						}
					}
					Fox::Scene::Transform world;
					world.position = p;
					world.rotation = glm::normalize(rotation);
					world.scale = nav.scale;
					graph.SetWorldTransform(entity, world);
				});
			}
		}
	}
}
