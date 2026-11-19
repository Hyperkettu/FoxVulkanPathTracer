#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <cstring>
#include <fstream>

namespace Fox {

	namespace Editor {

		namespace {
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;
			using Entity = Fox::ECS::Entity;
			using Fox::Scene::MeshComponent;
			using Fox::Scene::TransformComponent;
			using Fox::Scene::LightComponent;
			using Fox::Scene::WaterComponent;
			using Fox::Scene::PhysicsComponent;
			using Fox::Scene::NavAgentComponent;
			using Fox::Scene::VolumeComponent;
			using Fox::Scene::CameraComponent;
			using Fox::Scene::ScriptComponent;
			using State = SceneGraphEditor::InspectorState;

			const char* ENTITY_PAYLOAD = "FOX_ENTITY";

			bool SameRotation(const glm::quat& a, const glm::quat& b) {
				return std::abs(glm::dot(a, b)) > 1.0f - 1e-6f;
			}

			// --- Component editors ---

			// An inspector section per component type. A new component type gets an entry here.
			struct ComponentEditor {
				const char* name;
				bool closeButton; // Removed with the section's close button (otherwise from its right-click menu)
				bool (*has)(Scene& scene, Entity entity);
				// Menu items adding it (shown while the entity does not have it)
				void (*addMenu)(Scene& scene, Entity entity);
				void (*remove)(Scene& scene, Entity entity);
				void (*draw)(Scene& scene, Entity entity, State& state);
			};

			void DrawTransform(Scene& scene, Entity entity, State& state) {
				Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
				Fox::Scene::Transform transform = graph.GetLocalTransform(entity);

				if (state.eulerEntity != entity || !SameRotation(state.eulerRotation, transform.rotation)) {
					state.eulerEntity = entity;
					state.eulerRotation = transform.rotation;
					state.euler = glm::degrees(glm::eulerAngles(transform.rotation));
				}

				bool changed = ImGui::DragFloat3("Position", &transform.position.x, 0.05f, 0.0f, 0.0f, "%.3f");
				if (ImGui::DragFloat3("Rotation", &state.euler.x, 0.5f, 0.0f, 0.0f, "%.1f")) {
					transform.rotation = glm::normalize(glm::quat(glm::radians(state.euler)));
					state.eulerRotation = transform.rotation;
					changed = true;
				}
				if (ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.0f, 0.0f, "%.3f")) {
					for (int axis = 0; axis < 3; axis++) {
						if (std::abs(transform.scale[axis]) < 0.001f) {
							transform.scale[axis] = 0.001f; // A zero scale would lose the rotation for good
						}
					}
					changed = true;
				}
				if (changed) {
					graph.SetLocalTransform(entity, transform);
				}

				if (graph.GetParent(entity)) {
					const glm::vec3 world = glm::vec3(graph.GetWorldMatrix(entity)[3]);
					ImGui::TextDisabled("World position %.2f, %.2f, %.2f (relative to the parent above)", world.x, world.y, world.z);
				}
			}

			void DrawModelChoice(Scene& scene, MeshComponent& mesh, State& state) {
				if (!state.modelsListed) {
					state.modelFiles = SceneEditor::ListModels("Models");
					state.modelsListed = true;
				}
				if (ImGui::BeginCombo("File", mesh.file.empty() ? "(none)" : mesh.file.c_str())) {
					for (const std::string& file : state.modelFiles) {
						if (ImGui::Selectable(file.c_str(), file == mesh.file) && file != mesh.file) {
							mesh.file = file;
							scene.MeshChanged(&mesh, nullptr);
						}
					}
					if (state.modelFiles.empty()) {
						ImGui::TextDisabled("No .glb / .gltf files in Models/");
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Rescan")) {
					state.modelsListed = false;
				}
				if (ImGui::Checkbox("Displacement from normal maps", &mesh.displacement)) {
					scene.MeshChanged(&mesh, nullptr);
				}
				ImGui::SetItemTooltip("Real relief reconstructed from the model's normal maps: much more geometry, slower to load");
			}

			void DrawSphere(Scene& scene, MeshComponent& mesh) {
				bool changed = ImGui::DragFloat("Radius (m)", &mesh.radius, 0.01f, 0.01f, 100.0f, "%.2f");
				if (ImGui::BeginCombo("Preset", "Choose...")) {
					for (int i = 0; i < SceneEditor::SPHERE_PRESET_COUNT; i++) {
						if (ImGui::Selectable(SceneEditor::SPHERE_PRESETS[i])) {
							mesh.material = SceneEditor::SpherePreset(i);
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				auto& material = mesh.material;
				changed |= ImGui::ColorEdit3("Colour", &material.color.x);
				changed |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f, "%.2f");
				changed |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f, "%.2f");
				changed |= ImGui::SliderFloat("Transmission", &material.transmission, 0.0f, 1.0f, "%.2f");
				if (material.transmission > 0.0f) {
					changed |= ImGui::SliderFloat("Index of refraction", &material.ior, 1.0f, 2.5f, "%.2f");
					changed |= ImGui::ColorEdit3("Absorption colour", &material.attenuationColor.x);
					changed |= ImGui::DragFloat("Absorption distance (m)", &material.attenuationDistance, 0.05f, 0.0f, 100.0f, "%.2f");
				}
				if (changed) {
					mesh.radius = std::max(mesh.radius, 0.01f);
					scene.MeshChanged(&mesh, nullptr); // Rebuilt once the control is let go
				}
			}

			void DrawMesh(Scene& scene, Entity entity, State& state) {
				MeshComponent& mesh = scene.GetSceneGraph().GetRegistry().Get<MeshComponent>(entity);

				ImGui::Checkbox("Rendered", &mesh.enabled);
				ImGui::SetItemTooltip("Unchecked: the mesh stays in the scene, hidden");

				int type = static_cast<int>(mesh.type);
				const char* types[] = { "Model (glTF)", "Sphere", "Tree" };
				if (ImGui::Combo("Mesh", &type, types, IM_ARRAYSIZE(types)) && type != static_cast<int>(mesh.type)) {
					const MeshComponent::Type previous = mesh.type;
					mesh.type = static_cast<MeshComponent::Type>(type);
					if (mesh.type == MeshComponent::Type::Tree && !mesh.tree) {
						mesh.tree = std::make_shared<Fox::Scene::Tree>();
					}
					scene.MeshChanged(&mesh, &previous);
				}

				switch (mesh.type) {
				case MeshComponent::Type::Model:
					DrawModelChoice(scene, mesh, state);
					break;
				case MeshComponent::Type::Sphere:
					DrawSphere(scene, mesh);
					break;
				case MeshComponent::Type::Tree:
					if (mesh.tree && mesh.tree->mesh) {
						ImGui::TextDisabled("%zu triangles, %zu bones", mesh.tree->mesh->indices.size() / 3, mesh.tree->mesh->bones.size());
					}
					ImGui::TextWrapped("Shape, materials and wind: in the Trees window, which edits the selected tree.");
					break;
				}

				if (mesh.placed) {
					ImGui::TextDisabled("Scene object %zu%s", mesh.object, mesh.shown ? "" : " (hidden)");
				}
				else {
					ImGui::TextDisabled("Not in the scene yet: it rebuilds once no control is held");
				}
			}

			void DrawLight(Scene& scene, Entity entity, State&) {
				Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
				LightComponent& light = graph.GetRegistry().Get<LightComponent>(entity);

				ImGui::Checkbox("On", &light.enabled);

				int type = static_cast<int>(light.type);
				const char* types[] = { "Directional (sun)", "Point", "Spot" };
				if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types))) {
					light.type = static_cast<LightComponent::Type>(type); // Same room in the light buffer: no rebuild
				}

				ImGui::ColorEdit3("Colour", &light.color.x);
				if (light.type == LightComponent::Type::Directional) {
					ImGui::DragFloat("Strength", &light.intensity, 0.1f, 0.0f, 1000.0f, "%.1f");
					ImGui::SetItemTooltip("Irradiance on a surface facing it (times the colour)");
					ImGui::SliderFloat("Size", &light.angularRadius, 0.05f, 10.0f, "%.2f deg");
					ImGui::SetItemTooltip("Apparent radius of its disc in the sky (the real sun: 0.27). Larger = softer shadows");

					// Where it shines from: the entity's rotation, as direction and elevation in the sky
					glm::vec3 direction = glm::vec3(graph.GetWorldMatrix(entity)[2]);
					direction = glm::dot(direction, direction) > 1e-12f ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
					float azimuth = glm::degrees(std::atan2(direction.z, direction.x));
					float elevation = glm::degrees(std::asin(glm::clamp(direction.y, -1.0f, 1.0f)));
					bool turned = ImGui::SliderFloat("Direction", &azimuth, -180.0f, 180.0f, "%.0f deg");
					turned |= ImGui::SliderFloat("Elevation", &elevation, -10.0f, 90.0f, "%.1f deg");
					if (turned && graph.HasTransform(entity)) {
						const float a = glm::radians(azimuth), e = glm::radians(elevation);
						Fox::Scene::Transform transform = graph.GetWorldTransform(entity);
						transform.rotation = Scene::RotationTowards(glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a)));
						graph.SetWorldTransform(entity, transform);
					}
				}
				else {
					ImGui::DragFloat("Intensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.1f");
					ImGui::SetItemTooltip("Irradiance 1 m away (times the colour); it falls off with the square of the distance");
					ImGui::DragFloat("Bulb radius (m)", &light.radius, 0.005f, 0.0f, 5.0f, "%.3f");
					ImGui::SetItemTooltip("0: hard shadows. The bulb itself is not seen");
					if (light.type == LightComponent::Type::Spot) {
						ImGui::SliderFloat("Inner angle", &light.innerConeAngle, 0.0f, 89.0f, "%.1f deg");
						ImGui::SliderFloat("Outer angle", &light.outerConeAngle, 0.5f, 90.0f, "%.1f deg");
						light.innerConeAngle = std::min(light.innerConeAngle, light.outerConeAngle);
						ImGui::TextDisabled("It shines along the entity's -Z axis");
					}
				}

				if (light.placed) {
					ImGui::TextDisabled("Light %zu", light.index);
				}
				else {
					ImGui::TextDisabled("Not in the scene yet: it rebuilds once no control is held");
				}
			}

			void DrawWater(Scene& scene, Entity entity, State&) {
				WaterComponent& water = scene.GetSceneGraph().GetRegistry().Get<WaterComponent>(entity);

				// Its extent: placed anew when let go
				bool shape = ImGui::Checkbox("On", &water.enabled);
				shape |= ImGui::DragFloat2("Size (m)", &water.size.x, 0.1f, 1.0f, 500.0f, "%.1f");
				shape |= ImGui::SliderFloat("Cell size (m)", &water.cellSize, 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
				const uint32_t width = std::clamp(static_cast<uint32_t>(std::round(water.size.x / std::max(water.cellSize, 0.02f))), 4u, 512u);
				const uint32_t depth = std::clamp(static_cast<uint32_t>(std::round(water.size.y / std::max(water.cellSize, 0.02f))), 4u, 512u);
				ImGui::TextDisabled("%u x %u cells. The water fills up to the entity's height; move it to change the level.", width, depth);
				if (shape) {
					scene.MarkStructureChanged();
				}

				ImGui::SeparatorText("Look");
				bool look = ImGui::ColorEdit3("Colour in depth", &water.color.x);
				ImGui::SetItemTooltip("What white light turns into after Clarity metres of water (absorption)");
				look |= ImGui::DragFloat("Clarity (m)", &water.clarity, 0.05f, 0.1f, 100.0f, "%.2f");
				look |= ImGui::SliderFloat("Roughness", &water.roughness, 0.0f, 0.3f, "%.3f");
				look |= ImGui::SliderFloat("Ripples", &water.ripples, 0.0f, 2.0f, "%.2f");
				ImGui::SetItemTooltip("The wind's ripples finer than the simulation's cells (capillary and short gravity waves)");

				ImGui::SeparatorText("Motion");
				look |= ImGui::SliderFloat("Wind (m/s)", &water.windSpeed, 0.0f, 25.0f, "%.1f");
				look |= ImGui::SliderFloat("Wind towards", &water.windDirection, -180.0f, 180.0f, "%.0f deg");
				if (look) {
					scene.WaterLookChanged(entity);
				}
				ImGui::SliderFloat("Gusts", &water.gust, 0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Rain", &water.rain, 0.0f, 5.0f, "%.2f drops/m2/s", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Drop size (m)", &water.dropSize, 0.05f, 2.0f, "%.2f");
				ImGui::SliderFloat("Drop strength (m)", &water.dropStrength, 0.001f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Viscosity", &water.viscosity, 0.0f, 0.5f, "%.3f m2/s");
				ImGui::SetItemTooltip("Eddy viscosity: how quickly the motion smooths out (the turbulence finer than a cell)");
				ImGui::SliderFloat("Bed friction", &water.friction, 0.0f, 0.05f, "%.4f");
				ImGui::Checkbox("Splash where clicked", &water.splashOnClick);
				ImGui::SetItemTooltip("In edit mode, a click on the water splashes it (instead of selecting)");

				ImGui::SeparatorText("Foam and spray");
				ImGui::SliderFloat("Foam", &water.foam, 0.0f, 3.0f, "%.2f");
				ImGui::SetItemTooltip("How much foam breaking and churning water makes: steep wave faces, converging flow, the shore, fast water, splashes");
				ImGui::SliderFloat("Foam lifetime (s)", &water.foamLifetime, 0.5f, 30.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
				ImGui::SetItemTooltip("Time for foam to fade to a third (the bubbles burst)");
				ImGui::SliderFloat("Spray", &water.spray, 0.0f, 3.0f, "%.2f");
				ImGui::SetItemTooltip("How many droplets breaking water throws up (and the wind tears off crests above 9 m/s)");
				ImGui::SliderFloat("Droplet size (m)", &water.dropletSize, 0.002f, 0.05f, "%.3f", ImGuiSliderFlags_Logarithmic);
				int droplets = static_cast<int>(water.sprayDroplets);
				if (ImGui::SliderInt("Droplets in flight", &droplets, 0, 65536, "%d", ImGuiSliderFlags_Logarithmic)) {
					water.sprayDroplets = static_cast<uint32_t>(std::max(droplets, 0));
				}
				ImGui::SetItemTooltip("Room for droplets at once (0: no spray). Changing it places the water anew");
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					scene.MarkStructureChanged();
				}

				if (water.placed && water.simulation) {
					ImGui::TextDisabled("Simulated in %u steps a frame", water.simulation->LastSubsteps());
				}
				else {
					ImGui::TextDisabled("Not in the scene yet: it rebuilds once no control is held");
				}
			}

			void DrawPhysics(Scene& scene, Entity entity, State&) {
				PhysicsComponent& body = scene.GetSceneGraph().GetRegistry().Get<PhysicsComponent>(entity);
				const MeshComponent* mesh = scene.GetSceneGraph().GetRegistry().TryGet<MeshComponent>(entity);

				ImGui::Checkbox("On", &body.enabled);
				int motion = static_cast<int>(body.motion);
				const char* motions[] = { "Static", "Dynamic", "Kinematic" };
				if (ImGui::Combo("Motion", &motion, motions, 3)) {
					body.motion = static_cast<PhysicsComponent::Motion>(motion);
				}
				ImGui::SetItemTooltip("Static: stays. Dynamic: falls, collides, floats; its transform follows. Kinematic: follows its transform and pushes");

				// Its layer, by name
				const auto& layers = scene.GetEnvironment().layers;
				if (ImGui::BeginCombo("Layer", layers.Name(body.layer).c_str())) {
					for (int i = 0; i < Fox::Scene::PhysicsWorld::LAYER_COUNT; i++) {
						if (layers.names[i].empty() && i != body.layer) {
							continue; // Unnamed layers are not in use (name them in the Environment window)
						}
						if (ImGui::Selectable(layers.Name(i).c_str(), i == body.layer)) {
							body.layer = i;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Which layers it collides with: Environment window > Collision layers");
				ImGui::Checkbox("Trigger", &body.trigger);
				ImGui::SetItemTooltip("Collides with nothing; its scripts (and those of what enters) get OnTriggerEnter / OnTriggerExit");

				int shape = static_cast<int>(body.shape);
				const char* shapes[] = { "Auto", "Box", "Sphere", "Capsule", "Convex hull", "Triangle mesh" };
				if (ImGui::Combo("Shape", &shape, shapes, 6)) {
					body.shape = static_cast<PhysicsComponent::Shape>(shape);
				}
				if (body.shape == PhysicsComponent::Shape::Auto) {
					const char* automatic = !mesh ? "a box"
						: mesh->type == MeshComponent::Type::Sphere ? "its sphere"
						: mesh->type == MeshComponent::Type::Tree ? "its trunk (a capsule)"
						: body.motion == PhysicsComponent::Motion::Static ? "its model's triangles" : "its model's convex hull";
					ImGui::TextDisabled("Auto: %s", automatic);
				}
				const bool box = body.shape == PhysicsComponent::Shape::Box || (body.shape == PhysicsComponent::Shape::Auto && !mesh);
				if (box) {
					ImGui::DragFloat3("Half size (m)", &body.halfExtents.x, 0.01f, 0.01f, 100.0f, "%.2f");
				}
				if (body.shape == PhysicsComponent::Shape::Sphere || body.shape == PhysicsComponent::Shape::Capsule) {
					ImGui::DragFloat("Radius (m)", &body.radius, 0.01f, 0.01f, 100.0f, "%.2f");
				}
				if (body.shape == PhysicsComponent::Shape::Capsule) {
					ImGui::DragFloat("Half height (m)", &body.halfHeight, 0.01f, 0.01f, 100.0f, "%.2f");
					ImGui::SetItemTooltip("Half the straight part, along the entity's y");
				}
				if (box || body.shape == PhysicsComponent::Shape::Sphere || body.shape == PhysicsComponent::Shape::Capsule) {
					ImGui::DragFloat3("Centre (m)", &body.center.x, 0.01f, -100.0f, 100.0f, "%.2f");
				}
				if (body.shape == PhysicsComponent::Shape::Mesh && body.motion == PhysicsComponent::Motion::Dynamic) {
					ImGui::TextDisabled("A triangle mesh has no inside: it moves as kinematic");
				}

				if (body.motion == PhysicsComponent::Motion::Dynamic) {
					ImGui::SeparatorText("Mass");
					ImGui::DragFloat("Mass (kg)", &body.mass, 0.5f, 0.0f, 100000.0f, body.mass > 0.0f ? "%.1f" : "from density");
					if (body.mass <= 0.0f) {
						ImGui::DragFloat("Density (kg/m3)", &body.density, 5.0f, 10.0f, 20000.0f, "%.0f");
						ImGui::SetItemTooltip("Water is 1000: lighter floats, heavier sinks. Wood 400-700, ice 917, stone 2600, steel 7850");
					}
				}
				ImGui::SeparatorText("Surface");
				ImGui::SliderFloat("Friction", &body.friction, 0.0f, 1.5f, "%.2f");
				ImGui::SliderFloat("Bounciness", &body.restitution, 0.0f, 1.0f, "%.2f");
				if (body.motion == PhysicsComponent::Motion::Dynamic) {
					ImGui::SliderFloat("Linear damping", &body.linearDamping, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Angular damping", &body.angularDamping, 0.0f, 1.0f, "%.2f");
					ImGui::Checkbox("Continuous collision", &body.continuous);
					ImGui::SetItemTooltip("Swept: fast, small bodies do not pass through others (slower)");
					ImGui::SeparatorText("Water");
					ImGui::Checkbox("Floats", &body.floats);
					ImGui::SetItemTooltip("In water: lifted by what it displaces, slowed and carried by the flow, and it splashes");
					if (body.floats) {
						ImGui::SliderFloat("Water drag", &body.waterDrag, 0.0f, 3.0f, "%.2f");
					}
				}

				if (scene.IsPhysicsPlaying()) {
					ImGui::TextDisabled(body.body != 0xffffffffu ? "Playing: changes apply when physics plays again" : "Playing: not a body (it was off)");
				}
				else {
					ImGui::TextDisabled("Play (F5) simulates it");
				}
			}

			// What the scripts offer: classes and their fields, and compile errors (the host's JSON, read again after
			// every reload)
			struct ScriptCatalog {
				struct Field { std::string name, type, value; };
				struct Class { std::string name; std::vector<Field> fields; };
				std::vector<Class> classes;
				std::vector<std::string> errors;
				uint64_t generation = UINT64_MAX;

				const Class* Find(const std::string& name) const {
					for (const Class& c : classes) {
						if (c.name == name) return &c;
					}
					return nullptr;
				}
			};

			const ScriptCatalog& Catalog(Scene& scene) {
				static ScriptCatalog catalog;
				auto* host = scene.GetScripts();
				if (!host || host->GetInfoGeneration() == catalog.generation) {
					return catalog;
				}
				catalog = ScriptCatalog{};
				catalog.generation = host->GetInfoGeneration();
				try {
					const nlohmann::json info = nlohmann::json::parse(host->GetInfo());
					for (const auto& error : info.value("errors", nlohmann::json::array())) {
						catalog.errors.push_back(error.get<std::string>());
					}
					for (const auto& entry : info.value("classes", nlohmann::json::array())) {
						ScriptCatalog::Class c;
						c.name = entry.value("name", std::string());
						for (const auto& field : entry.value("fields", nlohmann::json::array())) {
							c.fields.push_back({ field.value("name", std::string()), field.value("type", std::string()), field.value("value", std::string()) });
						}
						catalog.classes.push_back(std::move(c));
					}
				}
				catch (const std::exception&) {
				}
				return catalog;
			}

			// A new script file from a template; false if it exists or cannot be written
			bool CreateScriptFile(Scene& scene, const std::string& name, std::string& message) {
				auto* host = scene.GetScripts();
				if (!host) {
					message = "Scripts are off";
					return false;
				}
				const bool valid = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')
					&& std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
				if (!valid) {
					message = "A class name: letters, digits and _";
					return false;
				}
				const std::filesystem::path file = host->GetScriptsDirectory() / (name + ".cs");
				if (std::filesystem::exists(file)) {
					message = file.filename().string() + " exists already";
					return false;
				}
				std::ofstream out(file);
				out << "using System.Numerics;\nusing Fox;\n\n"
					<< "public class " << name << " : Script\n{\n"
					<< "    public float speed = 1.0f; // Public fields show in the Inspector\n\n"
					<< "    public override void OnStart()\n    {\n        Debug.Log($\"{Entity.Name} starts\");\n    }\n\n"
					<< "    public override void OnUpdate(float deltaTime)\n    {\n    }\n}\n";
				if (!out) {
					message = "Cannot write " + file.string();
					return false;
				}
				out.close();
				host->Reload();
				message = "Created " + file.string();
				return true;
			}

			void DrawScript(Scene& scene, Entity entity, State&) {
				ScriptComponent& component = scene.GetSceneGraph().GetRegistry().Get<ScriptComponent>(entity);
				auto* host = scene.GetScripts();
				if (!host) {
					ImGui::TextWrapped("Scripts are off: %s", scene.GetScriptsError().c_str());
					return;
				}
				const ScriptCatalog& catalog = Catalog(scene);

				ImGui::Checkbox("On", &component.enabled);
				int remove = -1;
				for (size_t i = 0; i < component.scripts.size(); i++) {
					auto& entry = component.scripts[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::SeparatorText(entry.className.empty() ? "(no class)" : entry.className.c_str());
					if (ImGui::BeginCombo("Class", entry.className.c_str())) {
						for (const auto& c : catalog.classes) {
							if (ImGui::Selectable(c.name.c_str(), c.name == entry.className)) {
								entry.className = c.name;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove")) {
						remove = static_cast<int>(i);
					}
					const ScriptCatalog::Class* c = catalog.Find(entry.className);
					if (!c) {
						ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No such script class%s", catalog.errors.empty() ? "" : " (the scripts do not compile)");
					}
					else {
						for (const auto& field : c->fields) {
							auto found = entry.fields.find(field.name);
							const bool overridden = found != entry.fields.end();
							std::string value = overridden ? found->second : field.value;
							bool changed = false;
							char text[32];
							if (field.type == "float") {
								float f = std::strtof(value.c_str(), nullptr);
								if (ImGui::DragFloat(field.name.c_str(), &f, 0.01f)) {
									std::snprintf(text, sizeof(text), "%g", f);
									value = text;
									changed = true;
								}
							}
							else if (field.type == "int") {
								int n = std::atoi(value.c_str());
								if (ImGui::DragInt(field.name.c_str(), &n)) {
									value = std::to_string(n);
									changed = true;
								}
							}
							else if (field.type == "bool") {
								bool b = value == "true";
								if (ImGui::Checkbox(field.name.c_str(), &b)) {
									value = b ? "true" : "false";
									changed = true;
								}
							}
							else if (field.type == "vector3") {
								float v[3] = { 0.0f, 0.0f, 0.0f };
								sscanf_s(value.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]);
								if (ImGui::DragFloat3(field.name.c_str(), v, 0.01f)) {
									char vector[96];
									std::snprintf(vector, sizeof(vector), "%g,%g,%g", v[0], v[1], v[2]);
									value = vector;
									changed = true;
								}
							}
							else {
								char buffer[256];
								std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
								if (ImGui::InputText(field.name.c_str(), buffer, sizeof(buffer))) {
									value = buffer;
									changed = true;
								}
							}
							if (changed) {
								entry.fields[field.name] = value;
							}
							if (overridden) {
								ImGui::SameLine();
								ImGui::PushID(field.name.c_str());
								if (ImGui::SmallButton("x")) {
									entry.fields.erase(field.name);
								}
								ImGui::SetItemTooltip("Back to the class's value (%s)", field.value.c_str());
								ImGui::PopID();
							}
						}
						if (scene.IsPlaying()) {
							ImGui::TextDisabled(entry.instance > 0 ? "Running (field edits apply when play starts again)" : "Not running");
						}
					}
					ImGui::PopID();
				}
				if (remove >= 0) {
					component.scripts.erase(component.scripts.begin() + remove);
				}

				ImGui::Spacing();
				if (ImGui::BeginCombo("##add", "Add script...")) {
					for (const auto& c : catalog.classes) {
						if (ImGui::Selectable(c.name.c_str())) {
							ScriptComponent::Entry entry;
							entry.className = c.name;
							component.scripts.push_back(std::move(entry));
						}
					}
					if (catalog.classes.empty()) {
						ImGui::TextDisabled("No script classes in %s", host->GetScriptsDirectory().string().c_str());
					}
					ImGui::EndCombo();
				}

				static char newName[64] = "NewScript";
				static std::string message;
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::InputText("##newscript", newName, sizeof(newName));
				ImGui::SameLine();
				if (ImGui::Button("New script file")) {
					if (CreateScriptFile(scene, newName, message)) {
						ScriptComponent::Entry entry;
						entry.className = newName;
						component.scripts.push_back(std::move(entry));
					}
				}
				ImGui::SetItemTooltip("Writes Scripts/<name>.cs from a template and adds it here; saving the file reloads it");
				if (!message.empty()) {
					ImGui::TextDisabled("%s", message.c_str());
				}

				if (!catalog.errors.empty()) {
					ImGui::SeparatorText("Compile errors");
					for (const auto& error : catalog.errors) {
						ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error.c_str());
					}
				}
			}

			void DrawCamera(Scene& scene, Entity entity, State&) {
				CameraComponent& camera = scene.GetSceneGraph().GetRegistry().Get<CameraComponent>(entity);
				ImGui::Checkbox("On", &camera.enabled);
				ImGui::SliderFloat("Field of view", &camera.fov, 10.0f, 120.0f, "%.0f deg");
				ImGui::DragInt("Priority", &camera.priority, 0.1f, -100, 100);
				ImGui::SetItemTooltip("Of several cameras, the game looks through the enabled one with the highest priority");
				const bool used = scene.FindGameCamera() == entity;
				ImGui::TextDisabled(used ? "Play (F5) looks through it (F8: free camera)" : "Another camera has a higher priority");
			}

			void DrawVolume(Scene& scene, Entity entity, State&) {
				VolumeComponent& volume = scene.GetSceneGraph().GetRegistry().Get<VolumeComponent>(entity);
				ImGui::Checkbox("On", &volume.enabled);
				ImGui::SameLine();
				ImGui::Checkbox("Round", &volume.round);
				ImGui::SetItemTooltip("A puff filling the ellipsoid in its box; off: it fills the box");
				ImGui::DragFloat3("Size (m)", &volume.size.x, 0.05f, 0.01f, 1000.0f, "%.2f");
				ImGui::SliderFloat("Soft edge", &volume.softness, 0.0f, 1.0f, "%.2f");
				ImGui::DragFloat("Density (/m)", &volume.density, 0.01f, 0.0f, 50.0f, "%.2f");
				ImGui::SetItemTooltip("Extinction per metre where densest: light gets about 1 / density metres into it");
				ImGui::ColorEdit3("Colour", &volume.color.x);
				ImGui::SetItemTooltip("What it scatters (the rest it absorbs): white steam, grey smoke, black soot");
				ImGui::SliderFloat("Forward scattering", &volume.anisotropy, -0.9f, 0.95f, "%.2f");
				ImGui::SeparatorText("Glow");
				ImGui::ColorEdit3("Glow colour", &volume.glow.x);
				ImGui::DragFloat("Glow strength", &volume.glowStrength, 0.05f, 0.0f, 1000.0f, "%.2f");
				ImGui::SetItemTooltip("Radiance it gives off where it is (fire, embers); 0: none");
				ImGui::SeparatorText("Detail");
				ImGui::SliderFloat("Noise", &volume.noiseStrength, 0.0f, 1.0f, "%.2f");
				ImGui::SetItemTooltip("0: even density; 1: wisps and gaps");
				ImGui::DragFloat("Noise size (m)", &volume.noiseSize, 0.01f, 0.01f, 100.0f, "%.2f");
				ImGui::DragFloat3("Drift (m/s)", &volume.velocity.x, 0.01f, -50.0f, 50.0f, "%.2f");
				ImGui::SetItemTooltip("How its wisps move through it: rising smoke drifts up");
				ImGui::TextDisabled("At most 8 volumes render at a time");
			}

			void DrawNavAgent(Scene& scene, Entity entity, State&) {
				NavAgentComponent& nav = scene.GetSceneGraph().GetRegistry().Get<NavAgentComponent>(entity);
				bool replan = false;

				ImGui::Checkbox("On", &nav.enabled);
				ImGui::SeparatorText("Target");
				if (nav.hasTarget) {
					replan |= ImGui::DragFloat3("Target", &nav.target.x, 0.05f, -100000.0f, 100000.0f, "%.2f");
					if (ImGui::Button("Clear target")) {
						nav.hasTarget = false;
						nav.path.clear();
					}
					ImGui::SameLine();
					if (ImGui::Button("Plan again")) {
						replan = true;
					}
				}
				ImGui::TextDisabled("Shift + click the ground sends it there");
				if (!nav.status.empty()) {
					ImGui::TextWrapped("%s", nav.status.c_str());
				}

				ImGui::SeparatorText("Walking");
				ImGui::DragFloat("Speed (m/s)", &nav.speed, 0.05f, 0.0f, 50.0f, "%.2f");
				ImGui::DragFloat("Turn rate (deg/s)", &nav.turnRate, 5.0f, 1.0f, 3600.0f, "%.0f");
				int forward = static_cast<int>(nav.forward);
				const char* forwards[] = { "+Z", "-Z", "+X", "-X" };
				if (ImGui::Combo("Front", &forward, forwards, 4)) {
					nav.forward = static_cast<NavAgentComponent::Forward>(forward);
				}
				ImGui::SetItemTooltip("Which of its own axes points where it walks");
				ImGui::Checkbox("Tilt with the ground", &nav.alignToGround);

				ImGui::SeparatorText("Where it can go");
				replan |= ImGui::DragFloat("Radius (m)", &nav.radius, 0.01f, 0.0f, 20.0f, "%.2f");
				ImGui::SetItemTooltip("It keeps this far from obstacles");
				replan |= ImGui::DragFloat("Height (m)", &nav.height, 0.01f, 0.1f, 50.0f, "%.2f");
				ImGui::SetItemTooltip("Meshes higher above the ground than this do not block it (it walks under them)");
				replan |= ImGui::SliderFloat("Steepest slope", &nav.maxSlope, 1.0f, 89.0f, "%.0f deg");
				replan |= ImGui::DragFloat("Step (m)", &nav.maxStep, 0.01f, 0.0f, 5.0f, "%.2f");
				ImGui::SetItemTooltip("Climbs this small are allowed anywhere; meshes lower than this do not block it");
				replan |= ImGui::DragFloat("Wades to (m)", &nav.maxWaterDepth, 0.01f, 0.0f, 20.0f, "%.2f");
				ImGui::SetItemTooltip("Water deeper than this is in its way");
				replan |= ImGui::DragFloat("Grid cell (m)", &nav.cellSize, 0.01f, 0.1f, 5.0f, "%.2f");
				ImGui::SetItemTooltip("Finer finds narrower gaps, slower (at most 1024 cells a side)");

				if (replan) {
					nav.Invalidate();
					nav.replanTimer = 0.0f;
				}
				ImGui::TextDisabled(scene.IsPhysicsPlaying() ? "Playing: it walks" : "Play (F5) to walk it");
			}

			const ComponentEditor COMPONENT_EDITORS[] = {
				{
					"Transform", false,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().HasTransform(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Transform")) {
							scene.GetSceneGraph().GetRegistry().Add<TransformComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<TransformComponent>(entity); },
					DrawTransform,
				},
				{
					"Mesh", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<MeshComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::BeginMenu("Mesh")) {
							if (ImGui::MenuItem("Model (glTF)")) {
								scene.AddMeshComponent(entity, MeshComponent::Type::Model);
							}
							if (ImGui::MenuItem("Sphere")) {
								scene.AddMeshComponent(entity, MeshComponent::Type::Sphere);
							}
							if (ImGui::MenuItem("Tree")) {
								scene.AddMeshComponent(entity, MeshComponent::Type::Tree);
							}
							ImGui::EndMenu();
						}
					},
					[](Scene& scene, Entity entity) { scene.RemoveMeshComponent(entity); },
					DrawMesh,
				},
				{
					"Light", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<LightComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::BeginMenu("Light")) {
							if (ImGui::MenuItem("Directional (sun)")) {
								scene.AddLightComponent(entity, LightComponent::Type::Directional);
							}
							if (ImGui::MenuItem("Point")) {
								scene.AddLightComponent(entity, LightComponent::Type::Point);
							}
							if (ImGui::MenuItem("Spot")) {
								scene.AddLightComponent(entity, LightComponent::Type::Spot);
							}
							ImGui::EndMenu();
						}
					},
					[](Scene& scene, Entity entity) { scene.RemoveLightComponent(entity); },
					DrawLight,
				},
				{
					"Water", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<WaterComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Water")) {
							scene.GetSceneGraph().GetRegistry().Add<WaterComponent>(entity);
							scene.MarkStructureChanged();
						}
					},
					[](Scene& scene, Entity entity) {
						auto& registry = scene.GetSceneGraph().GetRegistry();
						if (WaterComponent* water = registry.TryGet<WaterComponent>(entity)) {
							water->enabled = false; // Taken out (and its surface hidden) before the component goes
						}
						scene.Update(0.0f);
						registry.Remove<WaterComponent>(entity);
						scene.MarkStructureChanged();
					},
					DrawWater,
				},
				{
					"Physics", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<PhysicsComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Physics")) {
							scene.GetSceneGraph().GetRegistry().Add<PhysicsComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<PhysicsComponent>(entity); },
					DrawPhysics,
				},
				{
					"Script", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<ScriptComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Script (C#)")) {
							scene.GetSceneGraph().GetRegistry().Add<ScriptComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<ScriptComponent>(entity); },
					DrawScript,
				},
				{
					"Camera", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<CameraComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Camera")) {
							scene.GetSceneGraph().GetRegistry().Add<CameraComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<CameraComponent>(entity); },
					DrawCamera,
				},
				{
					"Volume", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<VolumeComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Volume (smoke)")) {
							scene.GetSceneGraph().GetRegistry().Add<VolumeComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<VolumeComponent>(entity); },
					DrawVolume,
				},
				{
					"Nav Agent", true,
					[](Scene& scene, Entity entity) { return scene.GetSceneGraph().GetRegistry().Has<NavAgentComponent>(entity); },
					[](Scene& scene, Entity entity) {
						if (ImGui::MenuItem("Nav Agent")) {
							scene.GetSceneGraph().GetRegistry().Add<NavAgentComponent>(entity);
						}
					},
					[](Scene& scene, Entity entity) { scene.GetSceneGraph().GetRegistry().Remove<NavAgentComponent>(entity); },
					DrawNavAgent,
				},
			};
		}

		void SceneGraphEditor::ForgetLostSelection(Scene& scene, TransformGizmo& gizmo) {
			// A selection whose entity is gone (deleted, another scene loaded)
			if (gizmo.GetSelected() && !scene.GetSceneGraph().IsValid(gizmo.GetSelected())) {
				gizmo.Select({});
			}
		}

		// --- Hierarchy ---

		void SceneGraphEditor::DrawHierarchy(Scene& scene, TransformGizmo& gizmo, const glm::vec3& spawnPosition, bool* open, float uiScale) {
			ForgetLostSelection(scene, gizmo);
			(void)uiScale;
			if (!ImGui::Begin("Hierarchy", open)) {
				ImGui::End();
				ApplyPending(scene, gizmo, spawnPosition);
				return;
			}
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			const Entity selected = gizmo.GetSelected();

			if (ImGui::Button("+ Create")) {
				ImGui::OpenPopup("##create");
			}
			if (ImGui::BeginPopup("##create")) {
				if (ImGui::MenuItem("Empty")) {
					create = true;
					createUnder = {};
				}
				if (ImGui::MenuItem("Empty child of selected", nullptr, false, graph.IsValid(selected))) {
					create = true;
					createUnder = selected;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Sphere")) {
					gizmo.Select(scene.AddSphereObject(spawnPosition + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, SceneEditor::SpherePreset(0)));
				}
				if (ImGui::MenuItem("Tree")) {
					gizmo.Select(scene.AddTree(spawnPosition));
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Directional light")) {
					gizmo.Select(scene.AddLight(LightComponent::Type::Directional, spawnPosition + glm::vec3(0.0f, 20.0f, 0.0f)));
				}
				if (ImGui::MenuItem("Point light")) {
					gizmo.Select(scene.AddLight(LightComponent::Type::Point, spawnPosition + glm::vec3(0.0f, 2.0f, 0.0f)));
				}
				if (ImGui::MenuItem("Spot light")) {
					gizmo.Select(scene.AddLight(LightComponent::Type::Spot, spawnPosition + glm::vec3(0.0f, 4.0f, 0.0f)));
				}
				ImGui::TextDisabled("Models: drag one in from the Content Browser (Ctrl+Space)");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%zu entities", graph.GetEntityCount());

			ImGui::Separator();
			ImGui::BeginChild("##tree", ImVec2(0.0f, 0.0f));
			const std::vector<Entity> roots = graph.GetRoots(); // A copy: nothing changes while drawing, but to be safe
			for (Entity root : roots) {
				DrawNode(scene, gizmo, root);
			}

			// The empty space below: drop here to make a root, click to deselect, right-click to create
			const float rest = std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight());
			ImGui::InvisibleButton("##empty", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.0f), rest));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				gizmo.Select({});
			}
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD)) {
					moves.push_back({ *static_cast<const Entity*>(payload->Data), Entity{}, -1 });
				}
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ContentBrowser::MODEL_PAYLOAD)) {
					const auto* model = static_cast<const ContentBrowser::ModelPayload*>(payload->Data);
					modelDrops.push_back({ model->file, model->displacement, Entity{} });
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("##emptyMenu")) {
				if (ImGui::MenuItem("Create Empty")) {
					create = true;
					createUnder = {};
				}
				ImGui::EndPopup();
			}

			// Keys while the hierarchy has the focus
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && graph.IsValid(selected)) {
				// Delete is the renderer's key; it only gets it when the windows do not take the keyboard
				if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && ImGui::GetIO().WantCaptureKeyboard) {
					toDelete = selected;
				}
			}
			ImGui::EndChild();
			ImGui::End();
			ApplyPending(scene, gizmo, spawnPosition);
		}

		void SceneGraphEditor::DrawNode(Scene& scene, TransformGizmo& gizmo, Entity entity) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			const std::vector<Entity>& children = graph.GetChildren(entity);
			const bool leaf = children.empty();

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (children.size() <= 32) {
				flags |= ImGuiTreeNodeFlags_DefaultOpen; // Big groups (painted meshes) start folded
			}
			if (leaf) {
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (gizmo.GetSelected() == entity) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}

			ImGui::PushID(static_cast<int>(entity.index));
			ImGui::PushID(static_cast<int>(entity.generation));

			const bool inactive = !graph.IsActiveInHierarchy(entity);
			if (inactive) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			const std::string& name = graph.GetName(entity);
			const bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.empty() ? "(unnamed)" : name.c_str());
			if (inactive) {
				ImGui::PopStyleColor();
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
				gizmo.Select(entity);
			}

			// Drag it somewhere else in the tree
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload(ENTITY_PAYLOAD, &entity, sizeof(Entity));
				ImGui::Text("%s", name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget()) {
				// Top quarter: before it; bottom quarter: after it (unless its children show below); else: into it
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				const float y = ImGui::GetMousePos().y;
				const float height = max.y - min.y;
				enum { Before, Into, After } zone = Into;
				if (y < min.y + height * 0.25f) {
					zone = Before;
				}
				else if (y > max.y - height * 0.25f && (leaf || !open)) {
					zone = After;
				}

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD,
					ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
					const Entity dragged = *static_cast<const Entity*>(payload->Data);
					const bool allowed = dragged != entity && !graph.IsAncestorOf(dragged, entity);
					ImDrawList* draw = ImGui::GetWindowDrawList();
					const ImU32 color = allowed ? ImGui::GetColorU32(ImGuiCol_DragDropTarget) : IM_COL32(200, 70, 60, 255);
					if (zone == Into) {
						draw->AddRect(min, max, color, 2.0f, 0, 2.0f);
					}
					else {
						const float lineY = zone == Before ? min.y : max.y;
						draw->AddLine(ImVec2(min.x, lineY), ImVec2(max.x, lineY), color, 2.0f);
					}

					if (payload->IsDelivery() && allowed) {
						if (zone == Into) {
							moves.push_back({ dragged, entity, -1 });
						}
						else {
							const int index = graph.GetSiblingIndex(entity);
							moves.push_back({ dragged, graph.GetParent(entity), zone == Before ? index : index + 1 });
						}
					}
				}
				// A model from the Content Browser becomes its child
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ContentBrowser::MODEL_PAYLOAD)) {
					const auto* model = static_cast<const ContentBrowser::ModelPayload*>(payload->Data);
					modelDrops.push_back({ model->file, model->displacement, entity });
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::BeginPopupContextItem("##menu")) {
				gizmo.Select(entity);
				if (ImGui::MenuItem("Create Empty Child")) {
					create = true;
					createUnder = entity;
				}
				if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
					toDuplicate = entity;
				}
				if (ImGui::MenuItem("Duplicate as Instance")) {
					gizmo.Select(scene.DuplicateEntity(entity, true));
				}
				ImGui::SetItemTooltip("Trees in the copy stay the same trees: one geometry, edited together");
				if (ImGui::MenuItem("Drop to Ground", nullptr, false, graph.HasTransform(entity))) {
					MeshPainter::DropToGround(scene, entity);
				}
				if (ImGui::MenuItem("Unparent", nullptr, false, graph.IsValid(graph.GetParent(entity)))) {
					moves.push_back({ entity, Entity{}, -1 });
				}
				bool active = graph.IsActive(entity);
				if (ImGui::MenuItem("Active", nullptr, &active)) {
					graph.SetActive(entity, active);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Delete")) {
					toDelete = entity;
				}
				ImGui::EndPopup();
			}

			// What it is, faintly after the name
			if (const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s%s", MeshComponent::TypeName(mesh->type), mesh->enabled ? "" : " (off)");
			}
			if (const LightComponent* light = graph.GetRegistry().TryGet<LightComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s%s", LightComponent::TypeName(light->type), light->enabled ? "" : " (off)");
			}
			if (const WaterComponent* water = graph.GetRegistry().TryGet<WaterComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("Water%s", water->enabled ? "" : " (off)");
			}
			if (const PhysicsComponent* body = graph.GetRegistry().TryGet<PhysicsComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s %s%s", PhysicsComponent::MotionName(body->motion), body->trigger ? "trigger" : "body", body->enabled ? "" : " (off)");
			}
			if (const ScriptComponent* component = graph.GetRegistry().TryGet<ScriptComponent>(entity)) {
				std::string names;
				for (const auto& entry : component->scripts) {
					names += (names.empty() ? "" : ", ") + entry.className;
				}
				ImGui::SameLine();
				ImGui::TextDisabled("%s%s", names.empty() ? "Script" : names.c_str(), component->enabled ? "" : " (off)");
			}
			if (const CameraComponent* camera = graph.GetRegistry().TryGet<CameraComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("Camera%s", camera->enabled ? "" : " (off)");
			}
			if (const VolumeComponent* volume = graph.GetRegistry().TryGet<VolumeComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("Volume%s", volume->enabled ? "" : " (off)");
			}
			if (const NavAgentComponent* nav = graph.GetRegistry().TryGet<NavAgentComponent>(entity)) {
				ImGui::SameLine();
				ImGui::TextDisabled("Agent%s", nav->enabled ? "" : " (off)");
			}

			if (open && !leaf) {
				const std::vector<Entity> childList = children; // A copy: the menu above may have changed the graph
				for (Entity child : childList) {
					DrawNode(scene, gizmo, child);
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
			ImGui::PopID();
		}

		void SceneGraphEditor::ApplyPending(Scene& scene, TransformGizmo& gizmo, const glm::vec3& spawnPosition) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();

			for (const Move& move : moves) {
				if (graph.IsValid(move.entity)) {
					graph.SetParent(move.entity, move.parent, true, move.index);
				}
			}
			moves.clear();

			for (const ModelDrop& drop : modelDrops) {
				// Under an entity: where it is; a root: in front of the camera
				const bool child = graph.IsValid(drop.parent);
				const glm::vec3 position = child ? glm::vec3(graph.GetWorldMatrix(drop.parent)[3]) : spawnPosition;
				gizmo.Select(scene.AddModel(drop.file, drop.displacement, position, child ? drop.parent : Entity{}));
			}
			modelDrops.clear();

			if (create) {
				create = false;
				const bool child = graph.IsValid(createUnder);
				const Entity entity = scene.CreateEmpty(child ? "Empty child" : "Empty", child ? createUnder : Entity{});
				if (!child) {
					Fox::Scene::Transform transform;
					transform.position = spawnPosition;
					graph.SetWorldTransform(entity, transform);
				}
				gizmo.Select(entity);
			}

			if (graph.IsValid(toDuplicate)) {
				gizmo.Select(scene.DuplicateEntity(toDuplicate));
			}
			toDuplicate = {};

			if (graph.IsValid(toDelete)) {
				if (graph.IsAncestorOf(toDelete, gizmo.GetSelected())) {
					gizmo.Select({});
				}
				scene.DestroyEntity(toDelete);
			}
			toDelete = {};
		}

		// --- Inspector ---

		void SceneGraphEditor::DrawInspector(Scene& scene, TransformGizmo& gizmo, bool* open, float uiScale) {
			ForgetLostSelection(scene, gizmo);
			if (!ImGui::Begin("Inspector", open)) {
				ImGui::End();
				return;
			}

			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			const Entity entity = gizmo.GetSelected();
			if (!graph.IsValid(entity)) {
				ImGui::TextDisabled("Select an entity in the Hierarchy or in the view");
				ImGui::End();
				return;
			}

			// Active and name
			bool active = graph.IsActive(entity);
			if (ImGui::Checkbox("##active", &active)) {
				graph.SetActive(entity, active);
			}
			ImGui::SetItemTooltip("Active: off hides it and everything under it");
			ImGui::SameLine();
			if (!nameActive || nameEntity != entity) {
				std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", graph.GetName(entity).c_str());
				nameEntity = entity;
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer))) {
				graph.SetName(entity, nameBuffer);
			}
			nameActive = ImGui::IsItemActive();

			const Entity parent = graph.GetParent(entity);
			ImGui::TextDisabled("Entity %u.%u, %s%s", entity.index, entity.generation,
				graph.IsValid(parent) ? "child of " : "a root", graph.IsValid(parent) ? graph.GetName(parent).c_str() : "");
			ImGui::Separator();

			// Its components
			const ComponentEditor* toRemove = nullptr;
			for (const ComponentEditor& editor : COMPONENT_EDITORS) {
				if (!editor.has(scene, entity)) {
					continue;
				}
				ImGui::PushID(editor.name);
				bool keep = true;
				const bool open = editor.closeButton
					? ImGui::CollapsingHeader(editor.name, &keep, ImGuiTreeNodeFlags_DefaultOpen)
					: ImGui::CollapsingHeader(editor.name, ImGuiTreeNodeFlags_DefaultOpen);
				if (ImGui::BeginPopupContextItem("##component")) {
					if (ImGui::MenuItem("Remove Component")) {
						keep = false;
					}
					ImGui::EndPopup();
				}
				if (open) {
					ImGui::Indent(4.0f * uiScale);
					editor.draw(scene, entity, state);
					ImGui::Unindent(4.0f * uiScale);
					ImGui::Spacing();
				}
				if (!keep) {
					toRemove = &editor;
				}
				ImGui::PopID();
			}
			if (toRemove) {
				toRemove->remove(scene, entity);
			}

			// Add Component
			ImGui::Separator();
			const float width = 200.0f * uiScale;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max((ImGui::GetContentRegionAvail().x - width) * 0.5f, 0.0f));
			if (ImGui::Button("Add Component", ImVec2(width, 0.0f))) {
				ImGui::OpenPopup("##add");
			}
			if (ImGui::BeginPopup("##add")) {
				bool any = false;
				for (const ComponentEditor& editor : COMPONENT_EDITORS) {
					if (!editor.has(scene, entity)) {
						editor.addMenu(scene, entity);
						any = true;
					}
				}
				if (!any) {
					ImGui::TextDisabled("It has every kind of component");
				}
				ImGui::EndPopup();
			}

			ImGui::End();
		}
	}
}
