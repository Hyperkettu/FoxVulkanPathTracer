#include "FoxRenderer.h"

#include <cstring>

namespace Fox {

	namespace Editor {

		namespace {
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;
			using Entity = Fox::ECS::Entity;
			using Fox::Scene::MeshComponent;
			using Fox::Scene::TransformComponent;
			using Fox::Scene::LightComponent;
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
