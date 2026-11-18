#include "FoxRenderer.h"

namespace Fox {

	namespace Editor {

		namespace {
			using Fox::Graphics::Geometry::ClosedSpline;
			using Fox::Graphics::Geometry::OpenSpline;

			const ImU32 GRID_COLOR = IM_COL32(90, 90, 90, 255);
			const ImU32 REFERENCE_COLOR = IM_COL32(120, 120, 120, 160);
			const ImU32 CURVE_COLOR = IM_COL32(235, 235, 235, 255);
			const ImU32 POINT_COLOR = IM_COL32(80, 170, 255, 255);
			const ImU32 SELECTED_COLOR = IM_COL32(255, 210, 50, 255);
			const ImU32 BACKGROUND_COLOR = IM_COL32(25, 25, 28, 255);

			// A closed outline with lobes, like a buttressed trunk
			ClosedSpline Lobed(int lobes, float depth) {
				ClosedSpline spline;
				const int count = lobes * 2;
				for (int i = 0; i < count; i++) {
					const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(count);
					const float radius = i % 2 == 0 ? 1.0f + depth : 1.0f - depth * 0.5f;
					spline.points.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius);
				}
				return spline;
			}

			// The curve's offsets resampled to a new number of evenly spaced points
			std::vector<glm::vec2> Resampled(const std::vector<glm::vec2>& points, int count) {
				OpenSpline<glm::vec2> spline;
				spline.points = points;
				std::vector<glm::vec2> result(count);
				for (int i = 0; i < count; i++) {
					result[i] = spline.Evaluate(static_cast<float>(i) / static_cast<float>(std::max(count - 1, 1)));
				}
				return result;
			}
		}

		void TreeEditor::Draw(Scene& scene, TransformGizmo& gizmo, const glm::vec3& cameraPosition, const glm::vec3& cameraForward, bool* open, float uiScale) {
			const std::vector<Fox::ECS::Entity> trees = scene.GetTreeEntities();

			// A tree selected (in the scene or the hierarchy) becomes the one being edited
			if (scene.GetTree(gizmo.GetSelected())) {
				selected = gizmo.GetSelected();
			}
			if (!scene.GetTree(selected)) {
				selected = trees.empty() ? Fox::ECS::Entity{} : trees.front();
			}

			if (!ImGui::Begin("Trees", open)) {
				ImGui::End();
				return;
			}

			DrawTreeList(scene, gizmo, cameraPosition, cameraForward);

			if (Scene::Tree* edited = scene.GetTree(selected)) {
				Scene::Tree& tree = *edited;
				auto& shape = tree.shape;
				bool shapeChanged = false;   // Needs new geometry
				bool lookChanged = false;    // Materials only

				ImGui::Separator();
				ImGui::Text("%s", scene.GetSceneGraph().GetName(selected).c_str());
				if (tree.mesh) {
					ImGui::SameLine();
					ImGui::TextDisabled("%zu triangles, %zu bones, generated in %.0f ms",
						tree.mesh->indices.size() / 3, tree.mesh->bones.size(), tree.generationSeconds * 1000.0f);
				}

				if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
					DrawPlacement(scene, selected);
				}

				if (ImGui::CollapsingHeader("Trunk", ImGuiTreeNodeFlags_DefaultOpen)) {
					shapeChanged |= ImGui::DragFloat("Height (m)", &shape.trunkLength, 0.05f, 1.0f, 30.0f, "%.2f");
					shapeChanged |= ImGui::DragFloat("Radius (m)", &shape.trunkRadius, 0.005f, 0.05f, 1.5f, "%.3f");
					shapeChanged |= ImGui::SliderFloat("Taper", &shape.trunkTaper, 0.05f, 1.0f, "%.2f");
					shapeChanged |= ImGui::SliderInt("Segments", &shape.trunkCylinders, 2, 24);

					ImGui::SeparatorText("Cross-section");
					shapeChanged |= DrawCrossSection(shape, uiScale);
					shapeChanged |= ImGui::SliderFloat("Shape at base", &shape.crossSectionBase, 0.0f, 1.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Shape at top", &shape.crossSectionTop, 0.0f, 1.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Shape on branches", &shape.branchCrossSection, 0.0f, 1.0f, "%.2f");

					ImGui::SeparatorText("Curve (centre line, base to top)");
					shapeChanged |= DrawTrunkCurve(shape, uiScale);
				}

				if (ImGui::CollapsingHeader("Branches")) {
					shapeChanged |= ImGui::SliderInt("Levels", &shape.branchLevels, 0, 3);
					shapeChanged |= ImGui::SliderFloat("First branch at", &shape.firstBranchHeight, 0.0f, 0.95f, "%.2f of height");
					shapeChanged |= ImGui::SliderInt("Per trunk segment", &shape.trunkBranches, 0, 6);
					shapeChanged |= ImGui::SliderInt("Per branch segment", &shape.branchBranches, 0, 6);
					shapeChanged |= ImGui::SliderFloat("Angle", &shape.branchAngle, 5.0f, 110.0f, "%.0f deg");
					shapeChanged |= ImGui::SliderFloat("Length ratio", &shape.branchLengthRatio, 0.1f, 1.2f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Thickness ratio", &shape.branchRadiusRatio, 0.1f, 0.9f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Towards light", &shape.upwardTendency, -0.3f, 0.5f, "%.2f");
				}

				if (ImGui::CollapsingHeader("Randomness")) {
					int seed = static_cast<int>(shape.seed);
					if (ImGui::InputInt("Seed", &seed)) {
						shape.seed = static_cast<uint32_t>(std::max(seed, 0));
						shapeChanged = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("New")) {
						shape.seed = static_cast<uint32_t>(std::rand()) * 7919u + static_cast<uint32_t>(std::rand());
						shapeChanged = true;
					}
					shapeChanged |= ImGui::SliderFloat("Branch bending", &shape.bendiness, 0.0f, 0.6f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Trunk wander", &shape.trunkWander, 0.0f, 0.3f, "%.3f");
					shapeChanged |= ImGui::SliderFloat("Angle variation", &shape.angleVariation, 0.0f, 1.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Length variation", &shape.lengthVariation, 0.0f, 0.9f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Position variation", &shape.positionVariation, 0.0f, 1.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Spiral variation", &shape.azimuthVariation, 0.0f, 3.0f, "%.2f rad");
					shapeChanged |= ImGui::SliderFloat("Leaf size variation", &shape.leafSizeVariation, 0.0f, 0.9f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Leaf roll", &shape.leafRollVariation, 0.0f, 90.0f, "%.0f deg");
				}

				if (ImGui::CollapsingHeader("Leaves")) {
					shapeChanged |= ImGui::Checkbox("Leaves", &shape.leaves);
					shapeChanged |= ImGui::SliderInt("From branch level", &shape.leafLevel, 0, 3);
					shapeChanged |= ImGui::SliderInt("Per segment", &shape.leavesPerCylinder, 0, 8);
					shapeChanged |= ImGui::SliderFloat("Card size (m)", &shape.leafSize, 0.1f, 2.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Droop (m)", &shape.leafCurl, -0.2f, 0.4f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Leaf relief (m)", &shape.leafDisplacementScale, 0.0f, 0.15f, "%.3f");
				}

				if (ImGui::CollapsingHeader("Bark relief")) {
					shapeChanged |= ImGui::SliderFloat("Relief (m)", &shape.displacementScale, 0.0f, 0.4f, "%.3f");
					shapeChanged |= ImGui::SliderFloat("Texture size (m)", &shape.textureSize, 0.1f, 4.0f, "%.2f");
					shapeChanged |= ImGui::SliderFloat("Detail (m)", &shape.detailEdgeLength, 0.01f, 0.2f, "%.3f");
					shapeChanged |= ImGui::SliderFloat("Smooth below radius", &shape.minDisplacedRadius, 0.0f, 0.3f, "%.3f m");
				}

				if (ImGui::CollapsingHeader("Materials")) {
					auto& look = tree.look;
					ImGui::SeparatorText("Bark");
					lookChanged |= DrawTextures("bark", look.bark, shapeChanged);
					lookChanged |= ImGui::ColorEdit3("Bark tint", &look.barkTint.x);
					lookChanged |= ImGui::SliderFloat("Bark roughness", &look.barkRoughness, 0.0f, 1.0f, "%.2f");

					ImGui::SeparatorText("Leaves");
					lookChanged |= DrawTextures("leaf", look.leaf, shapeChanged);
					lookChanged |= ImGui::ColorEdit3("Leaf tint", &look.leafTint.x);
					lookChanged |= ImGui::SliderFloat("Leaf roughness", &look.leafRoughness, 0.0f, 1.0f, "%.2f");
					lookChanged |= ImGui::SliderFloat("Alpha cutoff", &look.alphaCutoff, 0.05f, 0.95f, "%.2f");
				}

				if (ImGui::CollapsingHeader("Wind")) {
					auto& wind = tree.wind;
					// Live: the springs pick these up every frame
					float direction = glm::degrees(std::atan2(wind.windDirection.z, wind.windDirection.x));
					if (ImGui::SliderFloat("Direction", &direction, -180.0f, 180.0f, "%.0f deg")) {
						wind.windDirection = glm::vec3(std::cos(glm::radians(direction)), 0.0f, std::sin(glm::radians(direction)));
					}
					ImGui::SliderFloat("Speed (m/s)", &wind.windSpeed, 0.0f, 30.0f, "%.1f");
					ImGui::SliderFloat("Gusts", &wind.gustStrength, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Gust speed (m/s)", &wind.gustSpeed, 0.5f, 20.0f, "%.1f");
					ImGui::SliderFloat("Flutter", &wind.turbulence, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Response", &wind.response, 0.0f, 30.0f, "%.1f");
					ImGui::SliderFloat("Damping", &wind.dampingRatio, 0.01f, 1.0f, "%.2f");
					ImGui::SliderFloat("Trunk sway (Hz)", &wind.trunkFrequency, 0.1f, 2.0f, "%.2f");
					ImGui::SliderFloat("Twig sway (Hz)", &wind.maxFrequency, 0.5f, 8.0f, "%.2f");
					ImGui::SliderFloat("Max bend", &wind.maxAngle, 0.05f, 1.0f, "%.2f rad");
				}

				if (shapeChanged) {
					tree.regenerate = true;
				}
				if (shapeChanged || lookChanged) {
					scene.MarkTreesChanged();
				}
			}

			if (!status.empty()) {
				ImGui::Separator();
				if (statusIsError) {
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", status.c_str());
				}
				else {
					ImGui::TextDisabled("%s", status.c_str());
				}
			}

			ImGui::End();
		}

		void TreeEditor::DrawTreeList(Scene& scene, TransformGizmo& gizmo, const glm::vec3& cameraPosition, const glm::vec3& cameraForward) {
			const std::vector<Fox::ECS::Entity> trees = scene.GetTreeEntities();
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();

			// New trees stand on the ground in front of the camera
			auto inFront = [&]() {
				glm::vec3 forward(cameraForward.x, 0.0f, cameraForward.z);
				forward = glm::dot(forward, forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(1.0f, 0.0f, 0.0f);
				glm::vec3 position = cameraPosition + forward * 12.0f;
				position.y = scene.GetGroundHeightAt(position.x, position.z);
				return position;
			};

			const Scene::Tree* tree = scene.GetTree(selected);
			if (ImGui::Button("Add tree")) {
				selected = scene.AddTree(inFront());
				gizmo.Select(selected);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!tree);
			if (ImGui::Button("Duplicate") && tree) {
				selected = scene.AddTree(inFront(), tree);
				gizmo.Select(selected);
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove") && tree) {
				if (graph.IsAncestorOf(selected, gizmo.GetSelected())) {
					gizmo.Select({});
				}
				scene.DestroyEntity(selected);
				selected = {};
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Checkbox("Live update", &liveUpdate);
			ImGui::SetItemTooltip("Rebuild while dragging a control (otherwise when it is released)");

			// Room left for trees on the GPU
			const float used = static_cast<float>(scene.GetDynamicVertexCount()) / static_cast<float>(Scene::DYNAMIC_VERTEX_CAPACITY);
			const float usedIndices = static_cast<float>(scene.GetDynamicIndexCount()) / static_cast<float>(Scene::DYNAMIC_INDEX_CAPACITY);
			char overlay[64];
			std::snprintf(overlay, sizeof(overlay), "Tree geometry: %.0f%% of the room", std::max(used, usedIndices) * 100.0f);
			ImGui::ProgressBar(std::max(used, usedIndices), ImVec2(-1.0f, 0.0f), overlay);

			if (ImGui::BeginListBox("##trees", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * std::clamp(static_cast<float>(trees.size()), 2.0f, 6.0f)))) {
				for (Fox::ECS::Entity entity : trees) {
					ImGui::PushID(static_cast<int>(entity.index));
					if (ImGui::Selectable(graph.GetName(entity).c_str(), entity == selected)) {
						selected = entity;
						gizmo.Select(entity);
					}
					ImGui::PopID();
				}
				ImGui::EndListBox();
			}
		}

		void TreeEditor::DrawPlacement(Scene& scene, Fox::ECS::Entity entity) {
			Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();
			char name[64];
			std::snprintf(name, sizeof(name), "%s", graph.GetName(entity).c_str());
			if (ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
				graph.SetName(entity, name);
			}

			// Its entity, in the world: changes here take effect right away, as with the gizmos
			auto transform = graph.GetWorldTransform(entity);
			bool moved = ImGui::DragFloat3("Position", &transform.position.x, 0.05f, 0.0f, 0.0f, "%.2f");

			const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
			float yaw = euler.y;
			if (ImGui::SliderFloat("Turn", &yaw, -180.0f, 180.0f, "%.0f deg")) {
				transform.rotation = glm::quat(glm::radians(glm::vec3(euler.x, yaw, euler.z)));
				moved = true;
			}

			float scale = transform.scale.y;
			if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.05f, 20.0f, "%.2f")) {
				transform.scale = glm::vec3(scale);
				moved = true;
			}
			if (ImGui::Button("Stand on the ground")) {
				transform.position.y = scene.GetGroundHeightAt(transform.position.x, transform.position.z);
				moved = true;
			}
			if (moved) {
				graph.SetWorldTransform(entity, transform);
			}
		}

		bool TreeEditor::DrawCrossSection(Fox::Graphics::Geometry::TreeSettings& shape, float uiScale) {
			auto& points = shape.crossSection.points;
			bool changed = false;

			const float size = 220.0f * uiScale;
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##crossSection", ImVec2(size, size));
			ImDrawList* draw = ImGui::GetWindowDrawList();

			const ImVec2 center(corner.x + size * 0.5f, corner.y + size * 0.5f);
			const float unit = size * 0.33f; // Radius 1 (the trunk radius)
			auto toScreen = [&](glm::vec2 p) { return ImVec2(center.x + p.x * unit, center.y - p.y * unit); };
			auto fromScreen = [&](ImVec2 s) { return glm::vec2((s.x - center.x) / unit, (center.y - s.y) / unit); };

			draw->AddRectFilled(corner, ImVec2(corner.x + size, corner.y + size), BACKGROUND_COLOR);
			draw->AddLine(ImVec2(corner.x, center.y), ImVec2(corner.x + size, center.y), GRID_COLOR);
			draw->AddLine(ImVec2(center.x, corner.y), ImVec2(center.x, corner.y + size), GRID_COLOR);
			draw->AddCircle(center, unit, REFERENCE_COLOR, 64);

			// Grab the nearest control point, then drag it
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			if (ImGui::IsItemActivated()) {
				sectionPoint = -1;
				float best = 10.0f * uiScale;
				for (int i = 0; i < static_cast<int>(points.size()); i++) {
					const ImVec2 p = toScreen(points[i]);
					const float distance = std::hypot(p.x - mouse.x, p.y - mouse.y);
					if (distance < best) {
						best = distance;
						sectionPoint = i;
					}
				}
			}
			if (ImGui::IsItemActive() && sectionPoint >= 0 && sectionPoint < static_cast<int>(points.size())) {
				const glm::vec2 p = glm::clamp(fromScreen(mouse), glm::vec2(-1.45f), glm::vec2(1.45f));
				if (p != points[sectionPoint]) {
					points[sectionPoint] = p;
					changed = true;
				}
			}

			// The outline, as the generator evaluates it
			const int samples = 128;
			ImVec2 previous = toScreen(shape.crossSection.Evaluate(0.0f));
			for (int k = 1; k <= samples; k++) {
				const ImVec2 next = toScreen(shape.crossSection.Evaluate(static_cast<float>(k) / samples));
				draw->AddLine(previous, next, CURVE_COLOR, 2.0f * uiScale);
				previous = next;
			}
			for (int i = 0; i < static_cast<int>(points.size()); i++) {
				draw->AddCircleFilled(toScreen(points[i]), 5.0f * uiScale, i == sectionPoint ? SELECTED_COLOR : POINT_COLOR);
			}

			// Editing the set of points
			ImGui::SameLine();
			ImGui::BeginGroup();
			if (ImGui::Button("Add point")) {
				// Halfway along the outline after the selected point
				const int after = sectionPoint >= 0 ? sectionPoint : static_cast<int>(points.size()) - 1;
				const float t = (static_cast<float>(after) + 0.5f) / static_cast<float>(points.size());
				points.insert(points.begin() + after + 1, shape.crossSection.Evaluate(t));
				sectionPoint = after + 1;
				changed = true;
			}
			ImGui::BeginDisabled(points.size() <= 3 || sectionPoint < 0);
			if (ImGui::Button("Remove point") && sectionPoint >= 0 && sectionPoint < static_cast<int>(points.size())) {
				points.erase(points.begin() + sectionPoint);
				sectionPoint = -1;
				changed = true;
			}
			ImGui::EndDisabled();
			ImGui::Spacing();
			ImGui::TextDisabled("Presets");
			if (ImGui::Button("Round")) { shape.crossSection = ClosedSpline::Circle(8); changed = true; }
			if (ImGui::Button("Oval")) {
				shape.crossSection = ClosedSpline::Circle(8);
				for (auto& p : shape.crossSection.points) p *= glm::vec2(1.25f, 0.8f);
				changed = true;
			}
			if (ImGui::Button("Ribbed")) { shape.crossSection = Lobed(7, 0.12f); changed = true; }
			if (ImGui::Button("Buttressed")) { shape.crossSection = Lobed(5, 0.35f); changed = true; }
			ImGui::EndGroup();

			return changed;
		}

		bool TreeEditor::DrawTrunkCurve(Fox::Graphics::Geometry::TreeSettings& shape, float uiScale) {
			auto& points = shape.trunkCurve;
			if (points.size() < 2) {
				points = { glm::vec2(0.0f), glm::vec2(0.0f) };
			}
			bool changed = false;

			OpenSpline<glm::vec2> curve;
			curve.points = points;

			// Two views: front (x offsets) and side (z offsets); height runs up, offsets across (±range metres)
			const float width = 105.0f * uiScale;
			const float height = 220.0f * uiScale;
			float range = 1.0f;
			for (const auto& p : points) {
				range = std::max({ range, std::abs(p.x) * 1.25f, std::abs(p.y) * 1.25f });
			}

			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const int count = static_cast<int>(points.size());

			for (int axis = 0; axis < 2; axis++) {
				if (axis == 1) {
					ImGui::SameLine();
				}
				const ImVec2 corner = ImGui::GetCursorScreenPos();
				ImGui::InvisibleButton(axis == 0 ? "##curveFront" : "##curveSide", ImVec2(width, height));

				const float margin = 10.0f * uiScale;
				auto toScreen = [&](float offset, float t) {
					return ImVec2(corner.x + width * 0.5f + offset / range * (width * 0.5f - margin), corner.y + height - margin - t * (height - 2.0f * margin));
				};

				draw->AddRectFilled(corner, ImVec2(corner.x + width, corner.y + height), BACKGROUND_COLOR);
				draw->AddLine(ImVec2(corner.x + width * 0.5f, corner.y), ImVec2(corner.x + width * 0.5f, corner.y + height), GRID_COLOR);
				draw->AddText(ImVec2(corner.x + 4.0f, corner.y + 2.0f), REFERENCE_COLOR, axis == 0 ? "front (x)" : "side (z)");

				if (ImGui::IsItemActivated()) {
					curvePoint = -1;
					float best = 10.0f * uiScale;
					for (int i = 0; i < count; i++) {
						const float t = static_cast<float>(i) / static_cast<float>(count - 1);
						const ImVec2 p = toScreen(points[i][axis], t);
						const float distance = std::hypot(p.x - mouse.x, p.y - mouse.y);
						if (distance < best) {
							best = distance;
							curvePoint = i;
							curveAxis = axis;
						}
					}
				}
				if (ImGui::IsItemActive() && curvePoint >= 0 && curvePoint < count && curveAxis == axis) {
					const float offset = (mouse.x - (corner.x + width * 0.5f)) / (width * 0.5f - margin) * range;
					const float clamped = glm::clamp(offset, -range, range);
					if (clamped != points[curvePoint][axis]) {
						points[curvePoint][axis] = clamped;
						changed = true;
					}
				}

				const int samples = 64;
				ImVec2 previous = toScreen(curve.Evaluate(0.0f)[axis], 0.0f);
				for (int k = 1; k <= samples; k++) {
					const float t = static_cast<float>(k) / samples;
					const ImVec2 next = toScreen(curve.Evaluate(t)[axis], t);
					draw->AddLine(previous, next, CURVE_COLOR, 2.0f * uiScale);
					previous = next;
				}
				for (int i = 0; i < count; i++) {
					const float t = static_cast<float>(i) / static_cast<float>(count - 1);
					draw->AddCircleFilled(toScreen(points[i][axis], t), 5.0f * uiScale, i == curvePoint && curveAxis == axis ? SELECTED_COLOR : POINT_COLOR);
				}
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			if (ImGui::Button("More points") && count < 16) {
				points = Resampled(points, count + 1);
				changed = true;
			}
			ImGui::BeginDisabled(count <= 2);
			if (ImGui::Button("Fewer points") && count > 2) {
				points = Resampled(points, count - 1);
				curvePoint = -1;
				changed = true;
			}
			ImGui::EndDisabled();
			if (ImGui::Button("Straight")) {
				for (auto& p : points) p = glm::vec2(0.0f);
				changed = true;
			}
			if (ImGui::Button("Lean")) {
				for (int i = 0; i < count; i++) {
					const float t = static_cast<float>(i) / static_cast<float>(count - 1);
					points[i] = glm::vec2(1.2f * t * t, 0.0f);
				}
				changed = true;
			}
			if (ImGui::Button("S-curve")) {
				for (int i = 0; i < count; i++) {
					const float t = static_cast<float>(i) / static_cast<float>(count - 1);
					points[i] = glm::vec2(0.5f * std::sin(t * glm::two_pi<float>()), 0.25f * std::sin(t * glm::pi<float>()));
				}
				changed = true;
			}
			ImGui::TextDisabled("±%.1f m", range);
			ImGui::EndGroup();

			return changed;
		}

		bool TreeEditor::TextureCombo(const char* label, std::string& file) {
			if (!imagesListed) {
				images = Fox::Scene::TextureLibrary::ListImages("Textures");
				imagesListed = true;
			}

			bool changed = false;
			const char* preview = file.empty() ? "(none)" : file.c_str();
			if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
				if (ImGui::Selectable("(none)", file.empty())) {
					file.clear();
					changed = true;
				}
				for (const std::string& image : images) {
					if (ImGui::Selectable(image.c_str(), image == file)) {
						file = image;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		bool TreeEditor::DrawTextures(const char* id, Scene::SurfaceTextures& textures, bool& geometryChanged) {
			ImGui::PushID(id);
			bool changed = false;
			changed |= TextureCombo("Colour", textures.albedo);
			changed |= TextureCombo("Normal", textures.normal);
			changed |= TextureCombo("Roughness", textures.roughness);
			changed |= TextureCombo("Metallic", textures.metallic);
			changed |= TextureCombo("Occlusion", textures.occlusion);

			// These also shape the geometry: relief, card proportions and the tangent frame
			bool geometry = false;
			geometry |= TextureCombo("Displacement", textures.displacement);
			geometry |= ImGui::Checkbox("Normal map green up (OpenGL)", &textures.normalGreenUp);
			geometry |= ImGui::Checkbox("Invert displacement", &textures.invertDisplacement);
			if (ImGui::SmallButton("Rescan Textures folder")) {
				imagesListed = false;
			}
			ImGui::PopID();

			// The leaf colour's alpha and size shape the cards too
			if (changed && std::string(id) == "leaf") {
				geometry = true;
			}
			geometryChanged |= geometry;
			return changed || geometry;
		}

		void TreeEditor::Applied(Scene& scene, TransformGizmo& gizmo, bool succeeded, const std::string& error, float seconds) {
			if (!succeeded) {
				status = error;
				statusIsError = true;
				return;
			}

			char text[128];
			std::snprintf(text, sizeof(text), "%zu trees rebuilt in %.0f ms", scene.GetTreeEntities().size(), seconds * 1000.0f);
			status = text;
			statusIsError = false;
			(void)gizmo; // Selections are entities: they survive the rebuild
		}
	}
}
