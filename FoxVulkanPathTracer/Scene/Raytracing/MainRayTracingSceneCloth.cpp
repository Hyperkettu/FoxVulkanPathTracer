// Cloth (ClothComponent): sheets simulated on the CPU (Scene/Cloth.h), drawn as simulated surfaces written every frame

#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Scene/Cloth.h"

#include <filesystem>
#include <future>

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			namespace {
				using Fox::Scene::ClothComponent;
				using Fox::Scene::ClothSimulation;

				// The entity's world matrix without its scale (cloth has its own size)
				glm::mat4 Unscaled(const glm::mat4& m) {
					glm::mat4 r(1.0f);
					for (int c = 0; c < 3; c++) {
						const glm::vec3 axis(m[c]);
						const float length = glm::length(axis);
						r[c] = glm::vec4(length > 1e-8f ? axis / length : glm::vec3(c == 0, c == 1, c == 2), 0.0f);
					}
					r[3] = m[3];
					return r;
				}

				ClothSimulation::Settings SettingsOf(const ClothComponent& c) {
					ClothSimulation::Settings s;
					s.width = c.size.x;
					s.height = c.size.y;
					s.resolution = c.resolution;
					s.flat = c.flat;
					s.density = c.density;
					s.stretchCompliance = c.stretch;
					s.bendCompliance = c.bend;
					s.damping = c.damping;
					s.thickness = c.thickness;
					s.friction = c.friction;
					s.selfCollision = c.selfCollision;
					s.tearable = c.tearable;
					s.tearStrain = c.tearStrain;
					s.substeps = c.substeps;
					s.drag = c.drag;
					s.lift = 0.3f;
					s.pins = static_cast<ClothSimulation::Pins>(c.pins);
					return s;
				}

				uint64_t KeyOf(MainRayTracingScene::Entity e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; }
			}

			std::string MainRayTracingScene::ClothKey(const ClothComponent& c) {
				char key[128];
				std::snprintf(key, sizeof(key), "%d|%.4f|%.4f|%d|%d|", c.enabled ? 1 : 0, c.size.x, c.size.y, c.resolution, c.flat ? 1 : 0);
				return key + c.texture;
			}

			namespace {
				std::string LookOf(const ClothComponent& c) {
					char key[160];
					std::snprintf(key, sizeof(key), "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f", c.color.r, c.color.g, c.color.b, c.roughness, c.tiling.x,
						c.tiling.y, c.alphaCutoff);
					return key;
				}
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddCloth(const glm::vec3& position, ClothComponent::Kind kind) {
				const Entity entity = graph.CreateEntity(ClothComponent::KindName(kind));
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);
				graph.GetRegistry().Add<ClothComponent>(entity, ClothComponent::Preset(kind));
				structureChanged = true;
				return entity;
			}

			Fox::Graphics::Vulkan::Material MainRayTracingScene::ClothMaterial(const ClothComponent& cloth, std::string& problem) {
				Fox::Graphics::Vulkan::Material material;
				material.baseColor[0] = cloth.color.r;
				material.baseColor[1] = cloth.color.g;
				material.baseColor[2] = cloth.color.b;
				material.roughness = std::clamp(cloth.roughness, 0.0f, 1.0f);
				material.specularWeight = 0.5f; // Fibres: a soft sheen, not a gloss
				material.uvScale[0] = std::max(cloth.tiling.x, 0.001f);
				material.uvScale[1] = std::max(cloth.tiling.y, 0.001f);
				if (cloth.texture.empty()) {
					return material;
				}
				// The albedo, and whatever other maps lie beside it (by their names)
				Fox::Scene::TextureLibrary& library = *textureLibrary;
				Fox::Scene::SurfaceTextures maps;
				maps.albedo = cloth.texture;
				std::error_code ignored;
				const std::filesystem::path albedoPath(cloth.texture);
				std::string stem = albedoPath.stem().string();
				std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				// Maps of the same set: sharing the name's start before "albedo" / "color" / "diffuse" / "basecolor"
				std::string prefix = stem;
				for (const char* word : { "albedo", "basecolor", "base_color", "color", "colour", "diffuse" }) {
					const size_t at = stem.find(word);
					if (at != std::string::npos) {
						prefix = stem.substr(0, at);
						break;
					}
				}
				for (const auto& entry : std::filesystem::directory_iterator(albedoPath.parent_path(), ignored)) {
					std::string name = entry.path().filename().string();
					std::string ext = entry.path().extension().string();
					std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if ((ext != ".png" && ext != ".jpg" && ext != ".jpeg") || entry.path().generic_string() == albedoPath.generic_string()
						|| name.rfind(prefix, 0) != 0) {
						continue;
					}
					const std::string path = entry.path().generic_string();
					if (name.find("normal") != std::string::npos) maps.normal = path;
					else if (name.find("rough") != std::string::npos) maps.roughness = path;
					else if (name.find("metal") != std::string::npos) maps.metallic = path;
					else if (name.find("occlusion") != std::string::npos || name.find("_ao") != std::string::npos || name.find("ambient") != std::string::npos) maps.occlusion = path;
				}
				material.baseColorTexture = library.Texture(maps.albedo, true);
				if (material.baseColorTexture < 0) {
					problem = "cannot read " + maps.albedo;
					return material;
				}
				if (!maps.normal.empty()) {
					material.normalTexture = library.Texture(maps.normal, false);
				}
				const bool packed = !(maps.occlusion.empty() && maps.roughness.empty() && maps.metallic.empty());
				if (packed) {
					const int texture = library.PackedOcclusionRoughnessMetallic(maps.occlusion, maps.roughness, maps.metallic);
					material.metallicRoughnessTexture = texture;
					material.occlusionTexture = maps.occlusion.empty() ? -1 : texture;
					material.metallic = 1.0f; // Multiplies the map's metallic (none: 0)
				}
				material.alphaCutoff = std::clamp(cloth.alphaCutoff, 0.0f, 1.0f);
				return material;
			}

			void MainRayTracingScene::PlaceCloths(const std::vector<Entity>& entities, std::string& problems) {
				auto& registry = graph.GetRegistry();
				clothHeld = Entity{};
				for (Entity entity : entities) {
					ClothComponent* cloth = registry.TryGet<ClothComponent>(entity);
					if (!cloth) {
						continue;
					}
					cloth->placed = true;
					cloth->placedKey = ClothKey(*cloth);
					cloth->placedLook = LookOf(*cloth);
					cloth->surface = SIZE_MAX;
					cloth->simulation = nullptr;
					cloth->reset = false;
					cloth->hidden = false;
					cloth->problem.clear();
					if (!cloth->enabled) {
						continue;
					}
					const Fox::Graphics::Vulkan::Material material = ClothMaterial(*cloth, cloth->problem);
					if (!cloth->problem.empty()) {
						problems += std::string(problems.empty() ? "" : "; ") + graph.GetName(entity) + ": " + cloth->problem;
					}
					cloth->simulation = std::make_shared<ClothSimulation>();
					cloth->simulation->Build(SettingsOf(*cloth), Unscaled(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix));
					std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
					std::vector<glm::vec4> previous;
					cloth->simulation->WriteVertices(vertices, previous);
					std::vector<uint32_t> indices(vertices.size());
					for (uint32_t i = 0; i < indices.size(); i++) {
						indices[i] = i;
					}
					cloth->surface = AddSimulatedSurface(vertices, indices, material, graph.GetName(entity));
					objectEntities.resize(GetObjectCount());
					objectEntities[GetSurfaceObject(cloth->surface)] = entity;
				}
			}

			void MainRayTracingScene::UpdateCloths(float dt) {
				auto& registry = graph.GetRegistry();
				bool any = false;
				registry.Each<ClothComponent>([&](Entity, ClothComponent& cloth) {
					if (!cloth.placed || ClothKey(cloth) != cloth.placedKey || (cloth.enabled && cloth.surface == SIZE_MAX && cloth.problem.empty())
						|| (!cloth.enabled && cloth.surface != SIZE_MAX)) {
						structureChanged = true;
						return;
					}
					any = any || (cloth.surface != SIZE_MAX && cloth.simulation);
				});
				if (!any) {
					return;
				}

				// What cloth collides with: physics shapes that are spheres, capsules or boxes (the same for every cloth)
				using Collider = ClothSimulation::Collider;
				using Fox::Scene::PhysicsComponent;
				std::vector<Collider> colliders;
				std::vector<uint32_t> bodies;
				std::unordered_map<uint64_t, glm::vec3> positionsNow;
				registry.Each<PhysicsComponent>([&](Entity entity, PhysicsComponent& component) {
					if (!component.enabled || component.trigger || !graph.IsActiveInHierarchy(entity)) {
						return;
					}
					const glm::mat4 m = graph.GetWorldMatrix(entity);
					const glm::vec3 scale(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
					const float widest = std::max({ scale.x, scale.y, scale.z });
					const glm::quat rotation = glm::quat_cast(glm::mat3(Unscaled(m)));
					Collider c;
					c.halfExtents = component.halfExtents * scale;
					c.radius = component.radius * widest;
					c.halfHeight = component.halfHeight * scale.y;
					glm::vec3 center = component.center * scale;
					PhysicsComponent::Shape shape = component.shape;
					const Fox::Scene::MeshComponent* mesh = registry.TryGet<Fox::Scene::MeshComponent>(entity);
					if (shape == PhysicsComponent::Shape::Auto) {
						if (mesh && mesh->type == Fox::Scene::MeshComponent::Type::Sphere) {
							shape = PhysicsComponent::Shape::Sphere;
							c.radius = mesh->radius * widest;
							center = glm::vec3(0.0f);
						}
						else if (mesh && mesh->type == Fox::Scene::MeshComponent::Type::Tree && mesh->tree) {
							const auto& trunk = mesh->tree->shape;
							shape = PhysicsComponent::Shape::Capsule;
							c.radius = std::max(0.02f, trunk.trunkRadius * 0.5f * (1.0f + trunk.trunkTaper) * std::max(scale.x, scale.z));
							const float height = trunk.trunkLength * scale.y;
							c.halfHeight = std::max(0.01f, 0.5f * height - c.radius);
							center = glm::vec3(0.0f, 0.5f * height, 0.0f);
						}
						else if (mesh && mesh->type == Fox::Scene::MeshComponent::Type::Model) {
							return; // A model's hull or triangles: not for cloth (give it a box)
						}
						else {
							shape = PhysicsComponent::Shape::Box;
						}
					}
					if (shape == PhysicsComponent::Shape::ConvexHull || shape == PhysicsComponent::Shape::Mesh) {
						return;
					}
					c.type = shape == PhysicsComponent::Shape::Sphere ? Collider::Type::Sphere
						: shape == PhysicsComponent::Shape::Capsule ? Collider::Type::Capsule : Collider::Type::Box;
					c.rotation = rotation;
					c.position = glm::vec3(m[3]) + rotation * center;
					c.user = KeyOf(entity);
					auto was = clothColliderPositions.find(c.user);
					c.previousPosition = was != clothColliderPositions.end() && glm::distance(was->second, c.position) < 5.0f ? was->second : c.position;
					positionsNow[c.user] = c.position;
					colliders.push_back(c);
					bodies.push_back(physicsPlaying && component.motion == PhysicsComponent::Motion::Dynamic ? component.body : Fox::Scene::PhysicsWorld::INVALID_BODY);
				});
				clothColliderPositions = std::move(positionsNow);

				// The scene's wind, gusting; the ground
				const float windAngle = glm::radians(environment.windDirection);
				ClothSimulation::World world;
				world.ground = [this](float x, float z) { return GetGroundHeightAt(x, z); };

				std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
				std::vector<glm::vec4> previous;
				std::vector<glm::vec3> impulses(colliders.size(), glm::vec3(0.0f)), angular(colliders.size(), glm::vec3(0.0f));

				// Each cloth steps on a thread of its own (they share nothing but what they read: the ground, the wind);
				// what they push on and their vertices are gathered here after
				struct Job {
					ClothComponent* cloth;
					ClothSimulation::Settings settings;
					glm::mat4 matrix;
					ClothSimulation::World world;
					std::vector<Collider> colliders;
				};
				std::vector<Job> jobs;
				registry.Each<ClothComponent>([&](Entity entity, ClothComponent& cloth) {
					if (cloth.surface == SIZE_MAX || !cloth.simulation) {
						return;
					}
					ClothSimulation& simulation = *cloth.simulation;
					if (const std::string look = LookOf(cloth); look != cloth.placedLook) {
						std::string ignored;
						SetMaterial(GetSurfaceMaterial(cloth.surface), ClothMaterial(cloth, ignored));
						cloth.placedLook = look;
					}
					const glm::mat4 matrix = Unscaled(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix);
					const ClothSimulation::Settings settings = SettingsOf(cloth);
					if (cloth.reset) {
						cloth.reset = false;
						simulation.Build(settings, matrix);
						if (clothHeld == entity) clothHeld = Entity{};
					}
					if (!graph.IsActiveInHierarchy(entity)) {
						// Hidden: no triangles (and still) while the entity is off
						if (!cloth.hidden) {
							simulation.WriteVertices(vertices, previous);
							const float hidden = std::numeric_limits<float>::quiet_NaN();
							for (auto& v : vertices) v.position[0] = v.position[1] = v.position[2] = hidden;
							UpdateSurfaceVertices(cloth.surface, 0, vertices.data(), static_cast<uint32_t>(vertices.size()));
							MarkSurfaceDeformed(cloth.surface, true);
							cloth.hidden = true;
						}
						return;
					}
					cloth.hidden = false;

					Fox::Animation::WindSkeleton::Settings wind;
					wind.windDirection = glm::vec3(std::cos(windAngle), 0.0f, std::sin(windAngle));
					wind.windSpeed = std::max(cloth.windSpeed, 0.0f);
					const float now = particleTime;
					ClothSimulation::World own = world;
					if (wind.windSpeed > 0.0f) {
						own.wind = [wind, now](const glm::vec3& p) {
							return wind.windDirection * Fox::Animation::WindSkeleton::GustSpeed(wind, p, now);
						};
					}
					jobs.push_back({ &cloth, settings, matrix, std::move(own), colliders });
				});
				std::vector<std::future<void>> running;
				for (size_t j = 1; j < jobs.size(); j++) {
					running.push_back(std::async(std::launch::async, [&job = jobs[j], dt] {
						job.cloth->simulation->Step(dt, job.settings, job.matrix, job.world, job.colliders);
					}));
				}
				if (!jobs.empty()) {
					jobs[0].cloth->simulation->Step(dt, jobs[0].settings, jobs[0].matrix, jobs[0].world, jobs[0].colliders);
				}
				for (auto& r : running) r.get();
				for (Job& job : jobs) {
					for (size_t i = 0; i < colliders.size(); i++) {
						impulses[i] += job.colliders[i].impulse;
						angular[i] += job.colliders[i].angularImpulse;
					}
					job.cloth->simulation->WriteVertices(vertices, previous);
					UpdateSurfaceVertices(job.cloth->surface, 0, vertices.data(), static_cast<uint32_t>(vertices.size()));
					UpdateSurfacePrevious(job.cloth->surface, previous.data(), static_cast<uint32_t>(previous.size()));
					MarkSurfaceDeformed(job.cloth->surface, true); // Folds move far, and torn triangles go: built anew every frame
				}

				// Two way: the cloth pushes back on dynamic bodies (a net slows the ball it catches)
				if (physics && physicsPlaying) {
					for (size_t i = 0; i < colliders.size(); i++) {
						if (bodies[i] == Fox::Scene::PhysicsWorld::INVALID_BODY || glm::dot(impulses[i], impulses[i]) < 1e-12f) {
							continue;
						}
						const float bodyMass = physics->GetBodyInfo(bodies[i]).mass;
						if (bodyMass <= 0.0f) continue;
						glm::vec3 linear = impulses[i];
						const float change = glm::length(linear) / bodyMass;
						const float most = glm::length(physics->GetLinearVelocity(bodies[i])) + 1.0f; // Never more than stops it
						const float scale = change > most ? most / change : 1.0f;
						physics->AddImpulse(bodies[i], linear * scale, angular[i] * scale * 0.2f);
					}
				}
			}

			bool MainRayTracingScene::GrabCloth(const glm::vec3& origin, const glm::vec3& direction) {
				ReleaseCloth();
				float best = std::numeric_limits<float>::max();
				Entity found{};
				graph.GetRegistry().Each<ClothComponent>([&](Entity entity, ClothComponent& cloth) {
					float distance;
					if (cloth.simulation && !cloth.hidden && cloth.simulation->Raycast(origin, direction, distance) && distance < best) {
						best = distance;
						found = entity;
					}
				});
				if (found.IsNull()) {
					return false;
				}
				ClothComponent& cloth = graph.GetRegistry().Get<ClothComponent>(found);
				cloth.simulation->Grab(origin, direction, std::max(0.04f, 0.03f * std::max(cloth.size.x, cloth.size.y)));
				clothHeld = found;
				return true;
			}

			void MainRayTracingScene::DragCloth(const glm::vec3& origin, const glm::vec3& direction) {
				if (clothHeld.IsNull()) return;
				ClothComponent* cloth = graph.IsValid(clothHeld) ? graph.GetRegistry().TryGet<ClothComponent>(clothHeld) : nullptr;
				if (!cloth || !cloth->simulation || !cloth->simulation->IsGrabbed()) {
					clothHeld = Entity{};
					return;
				}
				cloth->simulation->Drag(origin, direction);
			}

			void MainRayTracingScene::ReleaseCloth() {
				if (!clothHeld.IsNull() && graph.IsValid(clothHeld)) {
					if (ClothComponent* cloth = graph.GetRegistry().TryGet<ClothComponent>(clothHeld); cloth && cloth->simulation) {
						cloth->simulation->Release();
					}
				}
				clothHeld = Entity{};
			}

			bool MainRayTracingScene::CutCloth(const glm::vec3& origin, const glm::vec3& direction) {
				// The knife's way since the last call, in rays close enough together that the cuts join up (a slit, not a
				// row of holes)
				const glm::vec3 fromOrigin = clothCutStroke ? clothCutOrigin : origin;
				const glm::vec3 fromDirection = clothCutStroke ? clothCutDirection : direction;
				clothCutStroke = true;
				clothCutOrigin = origin;
				clothCutDirection = direction;
				const float angle = std::acos(std::clamp(glm::dot(fromDirection, direction), -1.0f, 1.0f));
				// The nearest cloth along a ray, and how far
				auto nearestAlong = [&](const glm::vec3& o, const glm::vec3& d, float& best) {
					best = std::numeric_limits<float>::max();
					ClothComponent* nearest = nullptr;
					graph.GetRegistry().Each<ClothComponent>([&](Entity, ClothComponent& cloth) {
						float distance;
						if (cloth.simulation && !cloth.hidden && cloth.simulation->Raycast(o, d, distance) && distance < best) {
							best = distance;
							nearest = &cloth;
						}
					});
					return nearest;
				};
				// How many: by how far apart the rays are where they meet the cloth
				float reach = 0.0f;
				nearestAlong(origin, direction, reach);
				if (reach == std::numeric_limits<float>::max()) {
					nearestAlong(fromOrigin, fromDirection, reach);
				}
				const int samples = reach < std::numeric_limits<float>::max()
					? std::clamp(static_cast<int>(std::ceil(angle * reach / 0.015f)), 1, 200) : 1;
				bool any = false;
				for (int s = 1; s <= samples; s++) {
					const float t = static_cast<float>(s) / static_cast<float>(samples);
					const glm::vec3 o = glm::mix(fromOrigin, origin, t);
					const glm::vec3 d = glm::normalize(glm::mix(fromDirection, direction, t));
					float distance;
					if (ClothComponent* cloth = nearestAlong(o, d, distance)) {
						const float radius = std::max(0.02f, 0.012f * std::max(cloth->size.x, cloth->size.y));
						any = cloth->simulation->Cut(o, d, radius) || any;
					}
				}
				return any;
			}
		}
	}
}
