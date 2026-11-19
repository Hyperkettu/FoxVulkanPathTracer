// C# scripts: the engine's functions they call (the NativeApi of Scripting/ScriptApi.h), and running them while the
// game plays (ScriptComponent).

#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <algorithm>

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			using Entity = MainRayTracingScene::Entity;
			using Fox::Scripting::ComponentType;
			using Fox::Scripting::Property;

			// The functions scripts call. Static: they are C function pointers; the scene is the one running scripts.
			struct ScriptBindings {
				static inline MainRayTracingScene* scene = nullptr;

				static uint64_t Id(Entity entity) {
					return entity.IsNull() ? 0 : (static_cast<uint64_t>(entity.generation) << 32) | (static_cast<uint64_t>(entity.index) + 1);
				}
				static Entity From(uint64_t id) {
					Entity entity;
					if (id != 0) {
						entity.index = static_cast<uint32_t>(id & 0xffffffffu) - 1;
						entity.generation = static_cast<uint32_t>(id >> 32);
					}
					return entity;
				}
				static SceneGraph& Graph() { return scene->graph; }
				// The entity, if it still exists
				static bool Valid(uint64_t id, Entity& entity) {
					entity = From(id);
					return scene && !entity.IsNull() && Graph().IsValid(entity);
				}

				static void Log(int32_t level, const char* message) {
					static const char* prefixes[] = { "", "Warning: ", "Error: " };
					std::cout << prefixes[std::clamp(level, 0, 2)] << (message ? message : "") << std::endl;
				}

				static uint64_t FindEntity(const char* name) {
					if (!name) return 0;
					for (Entity entity : Graph().DepthFirst()) {
						if (Graph().GetName(entity) == name) return Id(entity);
					}
					return 0;
				}
				static int32_t IsValid(uint64_t id) { Entity e; return Valid(id, e) ? 1 : 0; }
				static int32_t GetName(uint64_t id, char* buffer, int32_t capacity) {
					Entity e;
					if (!Valid(id, e)) return 0;
					const std::string& name = Graph().GetName(e);
					if (buffer && capacity > 0) {
						const size_t n = std::min(name.size(), static_cast<size_t>(capacity - 1));
						std::memcpy(buffer, name.data(), n);
						buffer[n] = 0;
					}
					return static_cast<int32_t>(name.size());
				}
				static void SetName(uint64_t id, const char* name) { Entity e; if (Valid(id, e) && name) Graph().SetName(e, name); }
				static int32_t GetActive(uint64_t id) { Entity e; return Valid(id, e) && Graph().IsActive(e) ? 1 : 0; }
				static void SetActive(uint64_t id, int32_t active) { Entity e; if (Valid(id, e)) Graph().SetActive(e, active != 0); }
				static uint64_t GetParent(uint64_t id) { Entity e; return Valid(id, e) ? Id(Graph().GetParent(e)) : 0; }
				static void SetParent(uint64_t id, uint64_t parent) {
					Entity e, p;
					if (Valid(id, e)) Graph().SetParent(e, Valid(parent, p) ? p : Entity{}, true);
				}
				static int32_t GetChildren(uint64_t id, uint64_t* children, int32_t capacity) {
					Entity e;
					if (!Valid(id, e)) return 0;
					const auto& list = Graph().GetChildren(e);
					for (int32_t i = 0; children && i < capacity && i < static_cast<int32_t>(list.size()); i++) children[i] = Id(list[i]);
					return static_cast<int32_t>(list.size());
				}

				static void GetTransform(uint64_t id, int32_t world, float* t) {
					Entity e;
					Transform transform;
					if (Valid(id, e)) transform = world ? Graph().GetWorldTransform(e) : Graph().GetLocalTransform(e);
					const float values[10] = { transform.position.x, transform.position.y, transform.position.z,
						transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w,
						transform.scale.x, transform.scale.y, transform.scale.z };
					std::memcpy(t, values, sizeof(values));
				}
				static void SetTransform(uint64_t id, int32_t world, const float* t) {
					Entity e;
					if (!Valid(id, e) || !Graph().HasTransform(e)) return;
					Transform transform;
					transform.position = glm::vec3(t[0], t[1], t[2]);
					transform.rotation = glm::quat(t[6], t[3], t[4], t[5]);
					if (glm::dot(transform.rotation, transform.rotation) < 1e-12f) transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
					transform.rotation = glm::normalize(transform.rotation);
					transform.scale = glm::vec3(t[7], t[8], t[9]);
					if (world) Graph().SetWorldTransform(e, transform);
					else Graph().SetLocalTransform(e, transform);
				}

				static uint64_t CreateEntity(const char* name, uint64_t parent) {
					Entity p;
					return Id(Graph().CreateEntity(name ? name : "Entity", Valid(parent, p) ? p : Entity{}));
				}
				static uint64_t Spawn(const char* what, const float* position) {
					if (!scene || !what) return 0;
					std::string kind(what);
					std::string lower = kind;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const glm::vec3 p(position[0], position[1], position[2]);
					if (lower == "ball") return Id(scene->AddPhysicsBall(p));
					if (lower == "box") return Id(scene->AddPhysicsCrate(p));
					if (lower == "sphere") return Id(scene->AddSphereObject(p, 0.5f, SurfaceMaterial{ glm::vec3(0.8f), 0.4f }));
					if (lower == "smoke") return Id(scene->AddSmoke(p));
					if (lower == "glowing smoke") return Id(scene->AddSmoke(p, true));
					if (lower == "agent") return Id(scene->AddNavAgent(p));
					if (lower == "empty") return Id(scene->CreateEmpty("Entity"));
					if (std::filesystem::exists(kind)) return Id(scene->AddModel(kind, false, p));
					std::cout << "Scripts: Spawn(\"" << kind << "\"): not a kind of thing (ball, box, sphere, smoke, glowing smoke, agent, empty) or a model file\n";
					return 0;
				}
				static uint64_t Duplicate(uint64_t id) { Entity e; return Valid(id, e) ? Id(scene->DuplicateEntity(e)) : 0; }
				static void Destroy(uint64_t id) { Entity e; if (Valid(id, e)) scene->DestroyEntity(e); }

				static int32_t HasComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: return r.Has<MeshComponent>(e);
					case ComponentType::Light: return r.Has<LightComponent>(e);
					case ComponentType::Physics: return r.Has<PhysicsComponent>(e);
					case ComponentType::NavAgent: return r.Has<NavAgentComponent>(e);
					case ComponentType::Volume: return r.Has<VolumeComponent>(e);
					case ComponentType::Camera: return r.Has<CameraComponent>(e);
					case ComponentType::Script: return r.Has<ScriptComponent>(e);
					case ComponentType::Water: return r.Has<WaterComponent>(e);
					}
					return 0;
				}
				static int32_t AddComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e) || HasComponent(id, type)) return 0;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: scene->AddMeshComponent(e, MeshComponent::Type::Sphere); return 1;
					case ComponentType::Light: scene->AddLightComponent(e, LightComponent::Type::Point); return 1;
					case ComponentType::Physics: r.Add<PhysicsComponent>(e); return 1; // Its body joins next frame
					case ComponentType::NavAgent: r.Add<NavAgentComponent>(e); return 1;
					case ComponentType::Volume: r.Add<VolumeComponent>(e); return 1;
					case ComponentType::Camera: r.Add<CameraComponent>(e); return 1;
					case ComponentType::Script: r.Add<ScriptComponent>(e); return 1;
					case ComponentType::Water: r.Add<WaterComponent>(e); scene->MarkStructureChanged(); return 1;
					}
					return 0;
				}
				static void RemoveComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e)) return;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: scene->RemoveMeshComponent(e); break;
					case ComponentType::Light: scene->RemoveLightComponent(e); break;
					case ComponentType::Physics: r.Remove<PhysicsComponent>(e); break; // Its body leaves next frame
					case ComponentType::NavAgent: r.Remove<NavAgentComponent>(e); break;
					case ComponentType::Volume: r.Remove<VolumeComponent>(e); break;
					case ComponentType::Camera: r.Remove<CameraComponent>(e); break;
					case ComponentType::Script: r.Remove<ScriptComponent>(e); break; // Its scripts stop next frame
					case ComponentType::Water:
						if (WaterComponent* water = r.TryGet<WaterComponent>(e)) water->enabled = false;
						scene->MarkStructureChanged();
						break;
					}
				}

				// The playing body of an entity (INVALID_BODY: none)
				static uint32_t Body(Entity e) {
					const PhysicsComponent* body = Graph().GetRegistry().TryGet<PhysicsComponent>(e);
					return body && scene->physics && scene->physicsPlaying ? body->body : PhysicsWorld::INVALID_BODY;
				}

				static int32_t GetProperty(uint64_t id, int32_t property, float* v) {
					v[0] = v[1] = v[2] = v[3] = 0.0f;
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					auto vec = [&](const glm::vec3& x) { v[0] = x.x; v[1] = x.y; v[2] = x.z; return 1; };
					switch (static_cast<Property>(property)) {
					case Property::PhysicsVelocity: { const uint32_t b = Body(e); return b != PhysicsWorld::INVALID_BODY ? vec(scene->physics->GetLinearVelocity(b)) : 0; }
					case Property::PhysicsAngularVelocity: { const uint32_t b = Body(e); return b != PhysicsWorld::INVALID_BODY ? vec(scene->physics->GetAngularVelocity(b)) : 0; }
					case Property::PhysicsMass: { const uint32_t b = Body(e); if (b == PhysicsWorld::INVALID_BODY) return 0; v[0] = scene->physics->GetBodyInfo(b).mass; return 1; }
					case Property::NavTarget: if (auto* n = r.TryGet<NavAgentComponent>(e)) return vec(n->target); return 0;
					case Property::NavSpeed: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->speed; return 1; } return 0;
					case Property::NavArrived: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->arrived ? 1.0f : 0.0f; return 1; } return 0;
					case Property::NavHasTarget: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->hasTarget ? 1.0f : 0.0f; return 1; } return 0;
					case Property::LightColor: if (auto* l = r.TryGet<LightComponent>(e)) return vec(l->color); return 0;
					case Property::LightIntensity: if (auto* l = r.TryGet<LightComponent>(e)) { v[0] = l->intensity; return 1; } return 0;
					case Property::LightEnabled: if (auto* l = r.TryGet<LightComponent>(e)) { v[0] = l->enabled ? 1.0f : 0.0f; return 1; } return 0;
					case Property::MeshVisible: if (auto* m = r.TryGet<MeshComponent>(e)) { v[0] = m->enabled ? 1.0f : 0.0f; return 1; } return 0;
					case Property::CameraFov: if (auto* c = r.TryGet<CameraComponent>(e)) { v[0] = c->fov; return 1; } return 0;
					case Property::VolumeDensity: if (auto* x = r.TryGet<VolumeComponent>(e)) { v[0] = x->density; return 1; } return 0;
					case Property::VolumeColor: if (auto* x = r.TryGet<VolumeComponent>(e)) return vec(x->color); return 0;
					case Property::VolumeGlow: if (auto* x = r.TryGet<VolumeComponent>(e)) { v[0] = x->glowStrength; return 1; } return 0;
					}
					return 0;
				}

				static int32_t SetProperty(uint64_t id, int32_t property, const float* v) {
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					const glm::vec3 x(v[0], v[1], v[2]);
					switch (static_cast<Property>(property)) {
					case Property::PhysicsVelocity: {
						const uint32_t b = Body(e);
						if (b == PhysicsWorld::INVALID_BODY) return 0;
						scene->physics->SetVelocity(b, x, scene->physics->GetAngularVelocity(b));
						return 1;
					}
					case Property::PhysicsAngularVelocity: {
						const uint32_t b = Body(e);
						if (b == PhysicsWorld::INVALID_BODY) return 0;
						scene->physics->SetVelocity(b, scene->physics->GetLinearVelocity(b), x);
						return 1;
					}
					case Property::NavSpeed: if (auto* n = r.TryGet<NavAgentComponent>(e)) { n->speed = std::max(v[0], 0.0f); return 1; } return 0;
					case Property::NavHasTarget:
						if (auto* n = r.TryGet<NavAgentComponent>(e)) {
							if (v[0] == 0.0f) { n->hasTarget = false; n->path.clear(); n->arrived = true; }
							return 1;
						}
						return 0;
					case Property::LightColor: if (auto* l = r.TryGet<LightComponent>(e)) { l->color = x; return 1; } return 0;
					case Property::LightIntensity: if (auto* l = r.TryGet<LightComponent>(e)) { l->intensity = v[0]; return 1; } return 0;
					case Property::LightEnabled: if (auto* l = r.TryGet<LightComponent>(e)) { l->enabled = v[0] != 0.0f; return 1; } return 0;
					case Property::MeshVisible: if (auto* m = r.TryGet<MeshComponent>(e)) { m->enabled = v[0] != 0.0f; return 1; } return 0;
					case Property::CameraFov: if (auto* c = r.TryGet<CameraComponent>(e)) { c->fov = v[0]; return 1; } return 0;
					case Property::VolumeDensity: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->density = std::max(v[0], 0.0f); return 1; } return 0;
					case Property::VolumeColor: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->color = x; return 1; } return 0;
					case Property::VolumeGlow: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->glowStrength = std::max(v[0], 0.0f); return 1; } return 0;
					default: return 0;
					}
				}

				static void AddImpulse(uint64_t id, const float* linear, const float* angular) {
					Entity e;
					if (!Valid(id, e)) return;
					const uint32_t b = Body(e);
					if (b == PhysicsWorld::INVALID_BODY) return;
					scene->physics->AddImpulse(b, linear ? glm::vec3(linear[0], linear[1], linear[2]) : glm::vec3(0.0f),
						angular ? glm::vec3(angular[0], angular[1], angular[2]) : glm::vec3(0.0f));
				}

				static int32_t AddScript(uint64_t id, const char* className) {
					Entity e;
					if (!Valid(id, e) || !className) return 0;
					auto& r = Graph().GetRegistry();
					ScriptComponent* component = r.TryGet<ScriptComponent>(e);
					if (!component) component = &r.Add<ScriptComponent>(e);
					ScriptComponent::Entry entry;
					entry.className = className;
					component->scripts.push_back(std::move(entry));
					return 1;
				}

				static void SetNavTarget(uint64_t id, const float* target) {
					Entity e;
					if (Valid(id, e)) scene->SetNavTarget(e, glm::vec3(target[0], target[1], target[2]));
				}

				static int32_t Raycast(const float* origin, const float* direction, float maxDistance, uint64_t ignore, uint64_t* entity, float* hit) {
					*entity = 0;
					const glm::vec3 o(origin[0], origin[1], origin[2]);
					glm::vec3 d(direction[0], direction[1], direction[2]);
					if (glm::dot(d, d) < 1e-12f) return 0;
					d = glm::normalize(d);
					if (scene->physics && scene->physicsPlaying) {
						Entity skip;
						const uint32_t ignored = Valid(ignore, skip) ? Body(skip) : PhysicsWorld::INVALID_BODY;
						PhysicsWorld::RayHit result;
						if (!scene->physics->CastRay(o, d, maxDistance, ignored, result)) return 0;
						for (const auto& playing : scene->playingBodies) {
							if (playing.body == result.body) {
								*entity = Id(playing.entity);
								break;
							}
						}
						const float values[7] = { result.point.x, result.point.y, result.point.z, result.normal.x, result.normal.y, result.normal.z, result.distance };
						std::memcpy(hit, values, sizeof(values));
						return 1;
					}
					// Not playing: the ground only
					glm::vec3 point;
					if (!scene->RaycastGround(o, d, point) || glm::distance(o, point) > maxDistance) return 0;
					const float values[7] = { point.x, point.y, point.z, 0.0f, 1.0f, 0.0f, glm::distance(o, point) };
					std::memcpy(hit, values, sizeof(values));
					return 1;
				}

				static int32_t Edge(uint8_t now, uint8_t before, int32_t mode) {
					return mode == 0 ? now : mode == 1 ? (now && !before) : (!now && before);
				}
				static int32_t GetKey(int32_t scancode, int32_t mode) {
					if (scancode < 0 || scancode >= 512) return 0;
					const auto& input = scene->scriptInput;
					return Edge(input.keys[scancode], input.previousKeys[scancode], mode);
				}
				static int32_t GetMouseButton(int32_t button, int32_t mode) {
					if (button < 0 || button >= 3) return 0;
					const auto& input = scene->scriptInput;
					return Edge(input.buttons[button], input.previousButtons[button], mode);
				}
				static void GetMouse(float* v) {
					const auto& input = scene->scriptInput;
					v[0] = input.mouseDelta.x; v[1] = input.mouseDelta.y; v[2] = input.mousePosition.x; v[3] = input.mousePosition.y;
				}
				static void GetTime(double* v) {
					const auto& clock = scene->clock;
					v[0] = clock.time; v[1] = clock.delta; v[2] = clock.timeScale; v[3] = static_cast<double>(clock.frame);
				}
				static uint64_t GetGameCamera() { return Id(scene->FindGameCamera()); }
			};

			namespace {
				// The Inspector's field values as a JSON object, for the scripting host
				std::string FieldsJson(const std::map<std::string, std::string>& fields) {
					nlohmann::json j = nlohmann::json::object();
					for (const auto& [name, value] : fields) {
						j[name] = value;
					}
					return j.dump();
				}
			}

			void MainRayTracingScene::InitializeScripts() {
				ScriptBindings::scene = this;
				Fox::Scripting::NativeApi api{};
				api.Log = ScriptBindings::Log;
				api.FindEntity = ScriptBindings::FindEntity;
				api.IsValid = ScriptBindings::IsValid;
				api.GetName = ScriptBindings::GetName;
				api.SetName = ScriptBindings::SetName;
				api.GetActive = ScriptBindings::GetActive;
				api.SetActive = ScriptBindings::SetActive;
				api.GetParent = ScriptBindings::GetParent;
				api.SetParent = ScriptBindings::SetParent;
				api.GetChildren = ScriptBindings::GetChildren;
				api.GetTransform = ScriptBindings::GetTransform;
				api.SetTransform = ScriptBindings::SetTransform;
				api.CreateEntity = ScriptBindings::CreateEntity;
				api.Spawn = ScriptBindings::Spawn;
				api.Duplicate = ScriptBindings::Duplicate;
				api.Destroy = ScriptBindings::Destroy;
				api.HasComponent = ScriptBindings::HasComponent;
				api.AddComponent = ScriptBindings::AddComponent;
				api.RemoveComponent = ScriptBindings::RemoveComponent;
				api.GetProperty = ScriptBindings::GetProperty;
				api.SetProperty = ScriptBindings::SetProperty;
				api.AddImpulse = ScriptBindings::AddImpulse;
				api.AddScript = ScriptBindings::AddScript;
				api.SetNavTarget = ScriptBindings::SetNavTarget;
				api.Raycast = ScriptBindings::Raycast;
				api.GetKey = ScriptBindings::GetKey;
				api.GetMouseButton = ScriptBindings::GetMouseButton;
				api.GetMouse = ScriptBindings::GetMouse;
				api.GetTime = ScriptBindings::GetTime;
				api.GetGameCamera = ScriptBindings::GetGameCamera;

				scripts = std::make_unique<Fox::Scripting::ScriptHost>();
				if (!scripts->Initialize("Scripts", api)) {
					scriptsError = scripts->GetError();
					std::cout << "Scripts: off (" << scriptsError << ")\n";
				}
			}

			void MainRayTracingScene::UpdateScripts(float step) {
				Fox::Scripting::ScriptHost* host = GetScripts();
				if (!host) {
					return;
				}
				auto& registry = graph.GetRegistry();

				// Scripts that stopped belonging: their entity or script gone, or switched off
				for (auto it = runningScripts.begin(); it != runningScripts.end();) {
					const auto [instance, entity] = *it;
					bool keep = false;
					if (graph.IsValid(entity)) {
						if (const ScriptComponent* component = registry.TryGet<ScriptComponent>(entity); component && component->enabled) {
							keep = std::any_of(component->scripts.begin(), component->scripts.end(),
								[instance](const ScriptComponent::Entry& entry) { return entry.instance == instance; });
						}
					}
					if (!keep) {
						host->DestroyInstance(instance);
						it = runningScripts.erase(it);
					}
					else {
						++it;
					}
				}

				// Scripts not running yet (play just started, or added since): they start before their first update
				registry.Each<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
					if (!component.enabled) {
						return;
					}
					for (auto& entry : component.scripts) {
						if (entry.instance == 0 && !entry.className.empty()) {
							entry.instance = host->CreateInstance(ScriptBindings::Id(entity), entry.className, FieldsJson(entry.fields));
							if (entry.instance == 0) {
								entry.instance = -1; // Not tried again until play starts again (or the scripts reload)
							}
							else {
								runningScripts[entry.instance] = entity;
							}
						}
					}
				});

				if (step > 0.0f) {
					host->Update(step);
				}
			}

			void MainRayTracingScene::StopScripts() {
				if (Fox::Scripting::ScriptHost* host = GetScripts()) {
					host->StopAll();
				}
				runningScripts.clear();
				graph.GetRegistry().Each<ScriptComponent>([](Entity, ScriptComponent& component) {
					for (auto& entry : component.scripts) {
						entry.instance = 0;
					}
				});
			}
		}
	}
}
