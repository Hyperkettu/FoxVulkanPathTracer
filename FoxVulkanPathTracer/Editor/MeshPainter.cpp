#include "FoxRenderer.h"

#include <unordered_map>
#include <filesystem>

namespace Fox {

	namespace Editor {

		namespace {
			using Fox::Scene::MeshComponent;
			using Fox::Scene::Tree;

			const char* ROOT_NAME = "Painted";
			const glm::vec3 UP(0.0f, 1.0f, 0.0f);

			float DistanceXZ(const glm::vec3& a, const glm::vec3& b) {
				const glm::vec2 d(a.x - b.x, a.z - b.z);
				return std::sqrt(glm::dot(d, d));
			}

			// The ground's normal at a point, from its heights around it
			glm::vec3 GroundNormal(const MeshPainter::Scene& scene, float x, float z) {
				const float e = 0.5f;
				const float left = scene.GetGroundHeightAt(x - e, z), right = scene.GetGroundHeightAt(x + e, z);
				const float back = scene.GetGroundHeightAt(x, z - e), front = scene.GetGroundHeightAt(x, z + e);
				return glm::normalize(glm::vec3(left - right, 2.0f * e, back - front));
			}
		}

		// --- Where the brush is ---

		bool MeshPainter::PointUnderMouse(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, glm::vec3& point) const {
			glm::vec3 origin, direction;
			TransformGizmo::MouseRay(view, mouse, origin, direction);
			if (scene.IsTerrainPlaced()) {
				return scene.GetTerrain().Raycast(origin, direction, point);
			}
			if (!scene.GetEnvironment().ground || std::abs(direction.y) < 1e-6f) {
				return false;
			}
			const float t = (scene.GetEnvironment().groundHeight - origin.y) / direction.y;
			if (t <= 0.0f) {
				return false;
			}
			point = origin + direction * t;
			return true;
		}

		bool MeshPainter::MouseDown(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse) {
			if (!active) {
				return false;
			}
			glm::vec3 point;
			if (!PointUnderMouse(scene, view, mouse, point)) {
				return false; // Not over the ground: the click selects as usual
			}
			hoverPoint = point;
			hovering = true;
			stroking = true;
			GatherPainted(scene);
			return true;
		}

		void MeshPainter::Update(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, bool erase) {
			hovering = active && PointUnderMouse(scene, view, mouse, hoverPoint);
			erasing = erase;
			if (stroking && hovering) {
				if (erase) {
					Erase(scene);
				}
				else {
					Paint(scene);
				}
			}
		}

		void MeshPainter::MouseUp(Scene& scene) {
			if (!stroking) {
				return;
			}
			stroking = false;

			// One rebuild for the whole stroke
			for (Entity entity : erased) {
				scene.DestroyEntity(entity);
			}
			// Groups left empty go too
			if (!erased.empty()) {
				Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
				const Entity root = Root(scene, false);
				const std::vector<Entity> groups = graph.GetChildren(root);
				for (Entity group : groups) {
					if (graph.GetChildren(group).empty()) {
						scene.DestroyEntity(group);
					}
				}
				if (graph.IsValid(root) && graph.GetChildren(root).empty()) {
					scene.DestroyEntity(root);
				}
			}
			if (addedModels) {
				scene.MarkStructureChanged();
			}
			if (addedTrees) {
				scene.MarkTreesChanged();
			}
			const size_t removed = erased.size();
			size_t placed = 0;
			for (Entity entity : added) {
				placed += std::find(erased.begin(), erased.end(), entity) == erased.end() ? 1 : 0;
			}
			char text[128];
			std::snprintf(text, sizeof(text), "Stroke: %zu painted, %zu erased", placed, removed);
			status = text;

			added.clear();
			erased.clear();
			addedModels = false;
			addedTrees = false;
		}

		// --- What was painted ---

		MeshPainter::Entity MeshPainter::Root(Scene& scene, bool create) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			for (Entity root : graph.GetRoots()) {
				if (graph.GetName(root) == ROOT_NAME) {
					return root;
				}
			}
			return create ? scene.CreateEmpty(ROOT_NAME) : Entity{};
		}

		MeshPainter::Entity MeshPainter::GroupOf(Scene& scene, const std::string& name) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			const Entity root = Root(scene, true);
			for (Entity child : graph.GetChildren(root)) {
				if (graph.GetName(child) == name) {
					return child;
				}
			}
			return scene.CreateEmpty(name, root);
		}

		void MeshPainter::GatherPainted(Scene& scene) {
			painted.clear();
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			const Entity root = Root(scene, false);
			if (!graph.IsValid(root)) {
				return;
			}
			for (Entity entity : graph.Subtree(root)) {
				const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
				if (!mesh || !mesh->enabled) {
					continue;
				}
				painted.push_back({ entity, glm::vec3(graph.GetWorldMatrix(entity)[3]), mesh->type == MeshComponent::Type::Tree ? mesh->tree.get() : nullptr });
			}
		}

		float MeshPainter::Spacing() const {
			// Without a spacing of its own: a little under the average distance the density gives
			return minSpacing > 0.0f ? minSpacing : 0.6f * std::sqrt(100.0f / std::max(density, 0.001f));
		}

		// --- Painting ---

		void MeshPainter::MakeVariants(Entry& entry) {
			if (entry.kind != Entry::Kind::Tree || !entry.source) {
				return;
			}
			const size_t count = static_cast<size_t>(std::clamp(entry.variantCount, 1, 16));
			if (entry.variants.empty() || entry.variants[0] != entry.source) {
				entry.variants.assign(1, entry.source);
			}
			entry.variants.resize(count);
			for (size_t i = 1; i < count; i++) {
				if (!entry.variants[i]) {
					// The same tree grown from another seed: another shape, the same kind
					entry.variants[i] = entry.source->CloneSettings();
					entry.variants[i]->shape.seed = entry.source->shape.seed + 7919u * static_cast<uint32_t>(i);
				}
			}
		}

		void MeshPainter::Paint(Scene& scene) {
			// The meshes to choose from, by weight
			std::vector<size_t> choices;
			float totalWeight = 0.0f;
			for (size_t i = 0; i < entries.size(); i++) {
				if (entries[i].enabled && entries[i].weight > 0.0f && (entries[i].kind == Entry::Kind::Model || entries[i].source)) {
					choices.push_back(i);
					totalWeight += entries[i].weight;
				}
			}
			if (choices.empty()) {
				status = "Nothing to paint: add a model or a tree below (and check it)";
				return;
			}

			// How many the brush should hold, and how many it does
			const float area = glm::pi<float>() * radius * radius;
			const int target = static_cast<int>(std::floor(density * area / 100.0f + 0.5f));
			int inside = 0;
			for (const Painted& p : painted) {
				inside += DistanceXZ(p.position, hoverPoint) <= radius ? 1 : 0;
			}
			const int needed = target - inside;
			if (needed <= 0) {
				return;
			}

			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			const float spacing = Spacing();
			int crowded = 0; // Spots left empty: every tree variant already stands too near
			const int perFrame = std::min(needed, 12);
			int placed = 0;
			for (int attempt = 0; attempt < std::min(needed * 6, 80) && placed < perFrame; attempt++) {
				// A spot in the brush, not too close to anything painted
				const float r = radius * std::sqrt(unit(random));
				const float a = glm::two_pi<float>() * unit(random);
				glm::vec3 spot = hoverPoint + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
				spot.y = scene.GetGroundHeightAt(spot.x, spot.z);
				bool free = true;
				for (const Painted& p : painted) {
					if (DistanceXZ(p.position, spot) < spacing) {
						free = false;
						break;
					}
				}
				if (!free) {
					continue;
				}

				// Which mesh
				float pick = unit(random) * totalWeight;
				size_t chosen = choices.back();
				for (size_t i : choices) {
					pick -= entries[i].weight;
					if (pick <= 0.0f) {
						chosen = i;
						break;
					}
				}
				Entry& entry = entries[chosen];

				Fox::Scene::Transform transform;
				const float scale = entry.minScale + (entry.maxScale - entry.minScale) * unit(random);
				transform.scale = glm::vec3(scale);
				transform.rotation = entry.randomRotation ? glm::angleAxis(glm::two_pi<float>() * unit(random), UP) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				const Entity group = GroupOf(scene, entry.name);
				const std::string name = entry.name + " " + std::to_string(scene.GetSceneGraph().GetChildren(group).size() + 1);

				Entity entity;
				const Tree* variant = nullptr;
				if (entry.kind == Entry::Kind::Model) {
					// Standing on its lowest point; tilted with the slope if it should be
					const glm::vec3 normal = entry.alignToGround ? GroundNormal(scene, spot.x, spot.z) : UP;
					if (entry.alignToGround) {
						transform.rotation = glm::rotation(UP, normal) * transform.rotation;
					}
					transform.position = spot + normal * (-entry.bottom * scale - entry.sink);
					entity = scene.AddModelInstance(entry.file, entry.displacement, transform, name, group, false);
					addedModels = true;
				}
				else {
					// Which variant: one with no instance of itself near; failing that, the one whose nearest is farthest
					MakeVariants(entry);
					std::vector<size_t> distant;
					size_t farthest = 0;
					float farthestDistance = -1.0f;
					for (size_t v = 0; v < entry.variants.size(); v++) {
						float nearest = std::numeric_limits<float>::max();
						for (const Painted& p : painted) {
							if (p.tree == entry.variants[v].get()) {
								nearest = std::min(nearest, DistanceXZ(p.position, spot));
							}
						}
						if (nearest >= treeSeparation) {
							distant.push_back(v);
						}
						if (nearest > farthestDistance) {
							farthestDistance = nearest;
							farthest = v;
						}
					}
					// Every variant has an instance too near: grow another one, while there is room for its geometry
					if (distant.empty() && entry.growVariants && entry.variants.size() < MAX_VARIANTS && HasRoomForVariant(scene, entry)) {
						entry.variantCount = static_cast<int>(entry.variants.size()) + 1;
						MakeVariants(entry);
						distant.push_back(entry.variants.size() - 1);
					}
					// Still none: no twin goes this near another, so this spot stays empty
					if (distant.empty() && treeSeparation > 0.0f) {
						crowded++;
						continue;
					}
					(void)farthest;
					const size_t v = distant[static_cast<size_t>(unit(random) * distant.size()) % distant.size()];
					transform.position = spot - UP * entry.sink;
					entity = scene.AddTreeInstance(entry.variants[v], transform, name, group, false);
					variant = entry.variants[v].get();
					addedTrees = true;
				}

				painted.push_back({ entity, transform.position, variant });
				added.push_back(entity);
				placed++;
			}
			if (crowded > 0 && placed == 0) {
				status = "Same tree apart keeps this spot empty: every tree variant stands too near. More variants (if there is room) "
					"or a shorter distance fill it.";
			}
		}

		bool MeshPainter::HasRoomForVariant(Scene& scene, const Entry& entry) const {
			// Each variant is one more tree geometry in the room reserved for dynamic meshes. Its size: the source's
			// (generated by now if it was painted before; otherwise assume a large tree)
			size_t perTree = entry.source && entry.source->mesh ? entry.source->mesh->vertices.size() : 0;
			if (perTree == 0) {
				// Not generated yet: about as large as the trees that are
				for (Entity entity : scene.GetTreeEntities()) {
					if (const Fox::Scene::Tree* tree = scene.GetTree(entity); tree && tree->mesh) {
						perTree = std::max(perTree, tree->mesh->vertices.size());
					}
				}
				perTree = perTree > 0 ? perTree : 400'000;
			}
			size_t pending = 0;
			for (const Entry& other : entries) {
				for (const auto& variant : other.variants) {
					pending += variant && !variant->mesh ? perTree : 0; // Not generated yet
				}
			}
			return scene.GetDynamicVertexCount() + pending + perTree < static_cast<size_t>(Scene::DYNAMIC_VERTEX_CAPACITY * 0.9);
		}

		void MeshPainter::Erase(Scene& scene) {
			auto& registry = scene.GetSceneGraph().GetRegistry();
			for (auto it = painted.begin(); it != painted.end();) {
				if (DistanceXZ(it->position, hoverPoint) > radius) {
					++it;
					continue;
				}
				if (MeshComponent* mesh = registry.TryGet<MeshComponent>(it->entity)) {
					mesh->enabled = false; // Out of sight now; out of the scene when the stroke ends
				}
				erased.push_back(it->entity);
				it = painted.erase(it);
			}
		}

		void MeshPainter::DropToGround(Scene& scene, Entity entity) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			if (!graph.IsValid(entity) || !graph.HasTransform(entity)) {
				return;
			}
			Fox::Scene::Transform transform = graph.GetWorldTransform(entity);
			float y = scene.GetGroundHeightAt(transform.position.x, transform.position.z);
			const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
			if (mesh && mesh->type == MeshComponent::Type::Model) {
				y -= scene.GetModelBottom(mesh->file, mesh->displacement) * transform.scale.y;
			}
			else if (mesh && mesh->type == MeshComponent::Type::Sphere) {
				y += mesh->radius * transform.scale.y;
			}
			transform.position.y = y;
			graph.SetWorldTransform(entity, transform);
		}

		// --- The window ---

		void MeshPainter::AddModel(Scene& scene, const std::string& file, bool displacement) {
			Entry entry;
			entry.kind = Entry::Kind::Model;
			entry.file = file;
			entry.displacement = displacement;
			entry.name = std::filesystem::path(file).stem().string();
			entry.bottom = scene.GetModelBottom(file, displacement); // Loads it now, once
			entry.minScale = entry.maxScale = 1.0f;
			entries.push_back(std::move(entry));
		}

		void MeshPainter::AddTree(const std::shared_ptr<Tree>& tree, const std::string& name) {
			Entry entry;
			entry.kind = Entry::Kind::Tree;
			entry.source = tree;
			entry.name = name;
			entry.sink = 0.1f; // The root flare a little into the ground
			MakeVariants(entry);
			entries.push_back(std::move(entry));
		}

		void MeshPainter::DrawPalette(Scene& scene) {
			int remove = -1;
			for (size_t i = 0; i < entries.size(); i++) {
				Entry& entry = entries[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::Checkbox("##use", &entry.enabled);
				ImGui::SetItemTooltip("Paint this one");
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) {
					remove = static_cast<int>(i);
				}
				ImGui::SameLine();
				const bool open = ImGui::TreeNodeEx("##entry", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%s)", entry.name.c_str(),
					entry.kind == Entry::Kind::Model ? "model" : "tree");
				if (open) {
					ImGui::DragFloat("Weight", &entry.weight, 0.05f, 0.0f, 100.0f, "%.2f");
					ImGui::SetItemTooltip("How often it is chosen against the other checked meshes");
					ImGui::DragFloatRange2("Scale", &entry.minScale, &entry.maxScale, 0.01f, 0.05f, 20.0f, "%.2f", "%.2f");
					ImGui::Checkbox("Random rotation", &entry.randomRotation);
					ImGui::DragFloat("Into the ground (m)", &entry.sink, 0.01f, -10.0f, 10.0f, "%.2f");
					if (entry.kind == Entry::Kind::Model) {
						ImGui::Checkbox("Tilt with the slope", &entry.alignToGround);
						ImGui::TextDisabled("%s, lowest point %.2f m", entry.file.c_str(), entry.bottom);
					}
					else {
						if (ImGui::SliderInt("Variants", &entry.variantCount, 1, static_cast<int>(MAX_VARIANTS))) {
							MakeVariants(entry);
						}
						ImGui::SetItemTooltip("Trees grown from other seeds; instances of one variant keep %.0f m apart", treeSeparation);
						ImGui::Checkbox("Grow variants when needed", &entry.growVariants);
						ImGui::SetItemTooltip("When every variant has an instance nearer than Same tree apart, add another (up to %d, while there is room)",
							static_cast<int>(MAX_VARIANTS));
						ImGui::TextDisabled("Each variant is one geometry for all its instances");
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (remove >= 0) {
				entries.erase(entries.begin() + remove);
			}
			if (entries.empty()) {
				ImGui::TextDisabled("No meshes yet: add models or trees below, or drag a model here from the Content Browser");
			}

			// Add a model
			ImGui::Spacing();
			if (!modelsListed) {
				modelFiles = SceneEditor::ListModels("Models");
				modelsListed = true;
			}
			modelChoice = std::clamp(modelChoice, 0, std::max(static_cast<int>(modelFiles.size()) - 1, 0));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::BeginCombo("##model", modelFiles.empty() ? "(no models in Models/)" : modelFiles[modelChoice].c_str())) {
				for (int i = 0; i < static_cast<int>(modelFiles.size()); i++) {
					if (ImGui::Selectable(modelFiles[i].c_str(), i == modelChoice)) {
						modelChoice = i;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(modelFiles.empty());
			if (ImGui::Button("Add Model") && !modelFiles.empty()) {
				AddModel(scene, modelFiles[modelChoice], false);
			}
			ImGui::EndDisabled();

			// Add a tree of the scene (the painter's own variants left out)
			std::vector<std::pair<std::shared_ptr<Tree>, std::string>> trees;
			std::unordered_map<const Tree*, bool> seen;
			for (const Entry& entry : entries) {
				for (size_t v = 1; v < entry.variants.size(); v++) {
					seen[entry.variants[v].get()] = true;
				}
			}
			auto& graph = scene.GetSceneGraph();
			for (Entity entity : scene.GetTreeEntities()) {
				const MeshComponent& mesh = graph.GetRegistry().Get<MeshComponent>(entity);
				if (mesh.tree && !seen[mesh.tree.get()]) {
					seen[mesh.tree.get()] = true;
					trees.emplace_back(mesh.tree, graph.GetName(entity));
				}
			}
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::BeginCombo("##tree", "Add a tree of the scene...")) {
				for (size_t i = 0; i < trees.size(); i++) {
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(trees[i].second.c_str())) {
						AddTree(trees[i].first, trees[i].second);
					}
					ImGui::PopID();
				}
				if (trees.empty()) {
					ImGui::TextDisabled("No trees: Create > Tree makes one");
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("New Tree")) {
				AddTree(std::make_shared<Tree>(), "Tree");
			}
			ImGui::SetItemTooltip("A tree with the default settings; edit it in the Trees window once it is painted");
		}

		void MeshPainter::Draw(Scene& scene, const TransformGizmo::View& view, bool* open, float uiScale) {
			if (!ImGui::Begin("Mesh Paint", open)) {
				ImGui::End();
				return;
			}

			ImGui::Checkbox("Paint (M)", &active);
			ImGui::SetItemTooltip("Left mouse on the ground paints, Ctrl + left erases, [ and ] change the radius");
			ImGui::SameLine();
			ImGui::TextDisabled(scene.IsTerrainPlaced() ? "on the terrain" : scene.GetEnvironment().ground ? "on the ground plane" : "no ground to paint on");

			ImGui::SliderFloat("Radius (m)", &radius, 0.5f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Density", &density, 0.05f, 100.0f, "%.2f per 100 m2", ImGuiSliderFlags_Logarithmic);
			ImGui::SetItemTooltip("How many meshes the brush fills 100 square metres with");
			ImGui::SliderFloat("Min spacing (m)", &minSpacing, 0.0f, 50.0f, minSpacing > 0.0f ? "%.1f" : "from the density");
			ImGui::SetItemTooltip("No two painted meshes closer than this (0: %.1f m, from the density)", 0.6f * std::sqrt(100.0f / std::max(density, 0.001f)));
			ImGui::SliderFloat("Same tree apart (m)", &treeSeparation, 0.0f, 100.0f, "%.1f");
			ImGui::SetItemTooltip("Instances of the same tree variant keep this far apart; a closer spot gets another variant");

			const float area = glm::pi<float>() * radius * radius;
			if (density * area / 100.0f < 0.5f) {
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "The brush is too small to hold one mesh at this density");
			}

			ImGui::SeparatorText("Meshes");
			DrawPalette(scene);

			// Models dropped here from the Content Browser
			ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ContentBrowser::MODEL_PAYLOAD)) {
					const auto* model = static_cast<const ContentBrowser::ModelPayload*>(payload->Data);
					AddModel(scene, model->file, model->displacement);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Separator();
			const Entity root = Root(scene, false);
			const size_t count = scene.GetSceneGraph().IsValid(root) ? scene.GetSceneGraph().Subtree(root).size() : 0;
			ImGui::TextDisabled("%zu entities under \"%s\"", count, ROOT_NAME);
			if (!status.empty()) {
				ImGui::TextDisabled("%s", status.c_str());
			}

			ImGui::End();

			DrawBrush(scene, view, uiScale);
		}

		void MeshPainter::DrawBrush(Scene& scene, const TransformGizmo::View& view, float uiScale) const {
			if (!active || !hovering) {
				return;
			}
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			const ImU32 color = erasing ? IM_COL32(255, 110, 90, 230) : IM_COL32(130, 235, 120, 230);

			const int segments = 64;
			ImVec2 points[segments];
			int count = 0;
			for (int i = 0; i < segments; i++) {
				const float angle = glm::two_pi<float>() * i / segments;
				glm::vec3 p = hoverPoint + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
				p.y = scene.GetGroundHeightAt(p.x, p.z) + 0.05f;
				glm::vec2 pixel;
				if (!TransformGizmo::Project(view, p, pixel)) {
					count = 0;
					break;
				}
				points[count++] = ImVec2(pixel.x, pixel.y);
			}
			if (count > 2) {
				draw->AddPolyline(points, count, color, ImDrawFlags_Closed, 2.0f * uiScale);
			}

			// This stroke's meshes, until the scene rebuilds with them
			const auto& graph = const_cast<Scene&>(scene).GetSceneGraph();
			for (Entity entity : added) {
				if (!graph.IsValid(entity)) {
					continue;
				}
				glm::vec2 pixel;
				if (TransformGizmo::Project(view, glm::vec3(graph.GetWorldMatrix(entity)[3]), pixel)) {
					draw->AddCircleFilled(ImVec2(pixel.x, pixel.y), 3.5f * uiScale, IM_COL32(130, 235, 120, 255));
				}
			}
		}
	}
}
