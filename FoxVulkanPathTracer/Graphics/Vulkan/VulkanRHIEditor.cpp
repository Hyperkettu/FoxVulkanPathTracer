// The editor's user interface (edit mode, E key): the menu bar and its commands, the windows it opens, the content
// browser and the status bar. See Editor/EditorShell.h for the frame around the windows.

#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace Managers = Fox::Graphics::Managers::Vulkan;

			namespace {
				using Window = Fox::Editor::EditorShell::Window;
				using Fox::Scene::LightComponent;
			}

			void VulkanRHI::RunDeferredCommands() {
				// Menu commands that replace rendering resources run here, between frames
				std::vector<std::function<void()>> commands;
				commands.swap(deferredCommands);
				for (auto& command : commands) {
					command();
				}
			}

			void VulkanRHI::Defer(std::function<void()> command) {
				deferredCommands.push_back(std::move(command));
			}

			void VulkanRHI::BuildEditor() {
				using Scene = Fox::Scene::RayTracing::MainRayTracingScene;
				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
				Scene& scene = *raytracingScene;
				Fox::Scene::SceneGraph& graph = scene.GetSceneGraph();

				Scene::CameraPose pose;
				pose.position = camera->GetPosition();
				pose.target = camera->GetPosition() + camera->GetForward();

				// New entities stand on the ground in front of the camera
				glm::vec3 forward = camera->GetForward();
				forward.y = 0.0f;
				forward = glm::dot(forward, forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(1.0f, 0.0f, 0.0f);
				glm::vec3 spawn = camera->GetPosition() + forward * 12.0f;
				spawn.y = scene.GetGroundHeightAt(spawn.x, spawn.z);

				// Selecting something shows it in the Inspector
				const auto selected = gizmo.GetSelected();
				if (selected != lastSelection && graph.IsValid(selected)) {
					shell.SetOpen(Window::Inspector, true);
				}
				lastSelection = selected;

				// Shortcuts the menus show (letters stay with the camera and the renderer's keys)
				if (!ImGui::GetIO().WantTextInput) {
					if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
						sceneEditor.Save(scene, pose);
					}
					if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && graph.IsValid(selected)) {
						gizmo.Select(scene.DuplicateEntity(selected));
					}
				}

				BuildMenus(pose, spawn);
				shell.DrawMenuBar(uiScale);

				// --- Windows ---

				if (shell.IsOpen(Window::Hierarchy)) {
					shell.Place(Window::Hierarchy, uiScale);
					sceneGraphEditor.DrawHierarchy(scene, gizmo, spawn, shell.OpenFlag(Window::Hierarchy), uiScale);
				}
				if (shell.IsOpen(Window::Inspector)) {
					shell.Place(Window::Inspector, uiScale);
					sceneGraphEditor.DrawInspector(scene, gizmo, shell.OpenFlag(Window::Inspector), uiScale);
				}
				if (shell.IsOpen(Window::ContentBrowser)) {
					Fox::Editor::ContentBrowser::Actions actions;
					actions.addModel = [&](const std::string& file, bool displacement) {
						// Models carry their own height above the ground plane (e.g. the cabin's foundation)
						glm::vec3 position = spawn;
						position.y = spawn.y - scene.GetEnvironment().groundHeight;
						gizmo.Select(scene.AddModel(file, displacement, position));
						sceneEditor.SetStatus("Loading " + file + "...", false);
					};
					actions.openScene = [&](const std::string& file) {
						sceneEditor.Open(scene, gizmo, file);
					};
					shell.Place(Window::ContentBrowser, uiScale);
					contentBrowser.Draw(shell.OpenFlag(Window::ContentBrowser), actions, uiScale);
				}
				if (shell.IsOpen(Window::Environment)) {
					shell.Place(Window::Environment, uiScale);
					sceneEditor.Draw(scene, shell.OpenFlag(Window::Environment), uiScale);
				}
				if (shell.IsOpen(Window::Trees)) {
					shell.Place(Window::Trees, uiScale);
					treeEditor.Draw(scene, gizmo, camera->GetPosition(), camera->GetForward(), shell.OpenFlag(Window::Trees), uiScale);
				}
				if (shell.IsOpen(Window::Camera)) {
					shell.Place(Window::Camera, uiScale);
					if (cameraEditor.Draw(scene.GetCameraManager(), *camera, GizmoView(), shell.OpenFlag(Window::Camera), uiScale)) {
						ResetHistory(); // The camera jumped
					}
				}
				if (shell.IsOpen(Window::Terrain)) {
					shell.Place(Window::Terrain, uiScale);
					terrainEditor.Draw(scene, GizmoView(), MouseInDisplayPixels(), ImGui::GetIO().WantCaptureMouse, shell.OpenFlag(Window::Terrain), uiScale);
				}
				else {
					terrainEditor.SetBrushActive(false); // The brush shows in its window: no painting without it
				}
				if (shell.IsOpen(Window::MeshPaint)) {
					shell.Place(Window::MeshPaint, uiScale);
					meshPainter.Draw(scene, GizmoView(), shell.OpenFlag(Window::MeshPaint), uiScale);
				}
				else {
					meshPainter.SetActive(false); // The brush shows in its window: no painting without it
				}

				// One brush at a time: turning one on turns the other off
				if (meshPainter.IsActive() && !painterWasActive) {
					terrainEditor.SetBrushActive(false);
				}
				else if (terrainEditor.IsBrushActive() && !terrainBrushWasActive) {
					meshPainter.SetActive(false);
				}
				painterWasActive = meshPainter.IsActive();
				terrainBrushWasActive = terrainEditor.IsBrushActive();

				if (showAgentPaths) {
					DrawAgentPaths();
				}
				DrawVolumeBounds();
				DrawCameraFrustum();
				DrawPlayToolbar();

				if (shell.IsOpen(Window::Controls)) {
					shell.Place(Window::Controls, uiScale);
					DrawControls();
				}
				if (shell.IsOpen(Window::Input)) {
					shell.Place(Window::Input, uiScale);
					DrawInputActions();
				}
				else {
					inputCapture = InputCapture{};
				}
				sceneEditor.DrawSaveAs(scene, pose, uiScale);

				// --- Status bar ---
				bool isError = false;
				std::string status = sceneEditor.GetStatus(isError);
				if (status.empty()) {
					status = sceneEditor.GetPath().empty() ? "New scene (not saved yet)" : sceneEditor.GetPath();
				}
				std::string hint = graph.IsValid(selected) ? graph.GetName(selected) + "   |   " : std::string();
				hint += Fox::Editor::TransformGizmo::ModeName(gizmo.GetMode());
				hint += raytracingScene->IsPlaying() ? "   |   Playing: F5 stop  F6 pause  F7 step  F8 camera" : "   |   F5 play";
				if (graph.IsValid(selected) && graph.GetRegistry().Has<Fox::Scene::NavAgentComponent>(selected)) {
					hint += "   |   Shift+click: send it there";
				}
				hint += "   |   F1 find   Ctrl+Space content   E leave edit mode";
				shell.DrawStatusBar(status, isError, hint, uiScale);
			}

			void VulkanRHI::DrawPlayToolbar() {
				// Above the view, in the middle: play / stop, pause, step, speed, game time and which camera
				auto& scene = *raytracingScene;
				const bool playing = scene.IsPlaying();
				const ImGuiViewport* viewport = ImGui::GetMainViewport();
				ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 4.0f * uiScale),
					ImGuiCond_Always, ImVec2(0.5f, 0.0f));
				ImGui::SetNextWindowBgAlpha(0.85f);
				const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
					| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
				if (!ImGui::Begin("##play", nullptr, flags)) {
					ImGui::End();
					return;
				}
				if (playing) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.2f, 0.18f, 1.0f));
				}
				if (ImGui::Button(playing ? "Stop" : "Play")) {
					Defer([this] { TogglePlay(); });
				}
				if (playing) {
					ImGui::PopStyleColor();
				}
				ImGui::SetItemTooltip(playing ? "F5: stop; the scene goes back as it was" : "F5: play the game on a copy of the scene");
				ImGui::BeginDisabled(!playing);
				ImGui::SameLine();
				const bool paused = playing && scene.GetClock().paused;
				if (ImGui::Button(paused ? "Resume" : "Pause")) {
					scene.SetPaused(!paused);
				}
				ImGui::SetItemTooltip("F6");
				ImGui::SameLine();
				if (ImGui::Button("Step")) {
					scene.StepFrame();
				}
				ImGui::SetItemTooltip("F7: one frame (1/60 s), paused");
				ImGui::EndDisabled();
				ImGui::SameLine();
				float speed = scene.GetClock().timeScale;
				ImGui::SetNextItemWidth(90.0f * uiScale);
				if (ImGui::SliderFloat("##speed", &speed, 0.0f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
					scene.SetTimeScale(speed);
				}
				ImGui::SetItemTooltip("Game speed (double click to type)");
				if (playing) {
					const auto& clock = scene.GetClock();
					ImGui::SameLine();
					ImGui::Text("%.1f s  frame %llu", clock.time, static_cast<unsigned long long>(clock.frame));
					ImGui::SameLine();
					const bool hasCamera = static_cast<bool>(scene.FindGameCamera());
					ImGui::BeginDisabled(!hasCamera);
					if (ImGui::Button(LooksThroughGameCamera() ? "Game camera" : "Free camera")) {
						RunKeyCommand(SDL_SCANCODE_F8);
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip(hasCamera ? "F8: look through the game's camera, or fly freely"
						: "No game camera: Create > Game Camera (an entity with a Camera component)");
				}
				ImGui::End();
			}

			void VulkanRHI::DrawCameraFrustum() {
				// The selected game camera: where it looks and how wide (not while looking through it)
				auto& graph = raytracingScene->GetSceneGraph();
				const auto selected = gizmo.GetSelected();
				const auto* component = graph.IsValid(selected) ? graph.GetRegistry().TryGet<Fox::Scene::CameraComponent>(selected) : nullptr;
				if (!component || LooksThroughGameCamera()) {
					return;
				}
				const glm::mat4 world = graph.GetWorldMatrix(selected);
				const glm::vec3 origin(world[3]);
				const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
				const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
				const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
				const float length = 2.0f;
				const float halfHeight = std::tan(glm::radians(component->fov) * 0.5f) * length;
				const float aspect = static_cast<float>(gbuffer->extent.width) / std::max(static_cast<float>(gbuffer->extent.height), 1.0f);
				const float halfWidth = halfHeight * aspect;
				glm::vec3 corners[4] = {
					origin + forward * length - right * halfWidth - up * halfHeight,
					origin + forward * length + right * halfWidth - up * halfHeight,
					origin + forward * length + right * halfWidth + up * halfHeight,
					origin + forward * length - right * halfWidth + up * halfHeight,
				};
				glm::vec2 apex, pixels[4];
				if (!Fox::Editor::TransformGizmo::Project(GizmoView(), origin, apex)) {
					return;
				}
				for (int i = 0; i < 4; i++) {
					if (!Fox::Editor::TransformGizmo::Project(GizmoView(), corners[i], pixels[i])) {
						return;
					}
				}
				ImDrawList* draw = ImGui::GetBackgroundDrawList();
				const ImU32 color = IM_COL32(255, 220, 120, 200);
				for (int i = 0; i < 4; i++) {
					draw->AddLine(ImVec2(apex.x, apex.y), ImVec2(pixels[i].x, pixels[i].y), color, 1.2f * uiScale);
					draw->AddLine(ImVec2(pixels[i].x, pixels[i].y), ImVec2(pixels[(i + 1) % 4].x, pixels[(i + 1) % 4].y), color, 1.2f * uiScale);
				}
				// Its up side
				const glm::vec3 top = origin + forward * length + up * halfHeight * 1.4f;
				glm::vec2 topPixel;
				if (Fox::Editor::TransformGizmo::Project(GizmoView(), top, topPixel)) {
					draw->AddTriangleFilled(ImVec2(topPixel.x, topPixel.y), ImVec2(pixels[2].x, pixels[2].y), ImVec2(pixels[3].x, pixels[3].y), IM_COL32(255, 220, 120, 90));
				}
			}

			void VulkanRHI::DrawVolumeBounds() {
				// The selected smoke volume's box, so its extent shows where it is thin
				auto& graph = raytracingScene->GetSceneGraph();
				const auto selected = gizmo.GetSelected();
				const auto* volume = graph.IsValid(selected) ? graph.GetRegistry().TryGet<Fox::Scene::VolumeComponent>(selected) : nullptr;
				if (!volume) {
					return;
				}
				const glm::mat4 box = graph.GetWorldMatrix(selected) * glm::scale(glm::mat4(1.0f), volume->size);
				glm::vec2 corners[8];
				for (int i = 0; i < 8; i++) {
					const glm::vec3 local((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
					if (!Fox::Editor::TransformGizmo::Project(GizmoView(), glm::vec3(box * glm::vec4(local, 1.0f)), corners[i])) {
						return;
					}
				}
				ImDrawList* draw = ImGui::GetBackgroundDrawList();
				const ImU32 color = IM_COL32(200, 200, 255, 160);
				for (int i = 0; i < 8; i++) {
					for (int axis = 1; axis < 8; axis <<= 1) {
						if (!(i & axis)) {
							draw->AddLine(ImVec2(corners[i].x, corners[i].y), ImVec2(corners[i | axis].x, corners[i | axis].y), color, 1.2f * uiScale);
						}
					}
				}
			}

			void VulkanRHI::DrawAgentPaths() {
				// Every agent's way: its path along the ground, the waypoint it walks to, and its target
				auto& scene = *raytracingScene;
				auto& graph = scene.GetSceneGraph();
				const auto view = GizmoView();
				const auto selected = gizmo.GetSelected();
				ImDrawList* draw = ImGui::GetBackgroundDrawList();
				graph.GetRegistry().Each<Fox::Scene::NavAgentComponent>([&](Fox::ECS::Entity entity, Fox::Scene::NavAgentComponent& nav) {
					if (!nav.enabled || !nav.hasTarget || !graph.IsActiveInHierarchy(entity)) {
						return;
					}
					const bool isSelected = entity == selected;

					// Around the selected agent: the cells it may not stand on (obstacles grown by its radius, slopes
					// too steep to stand on are not marked: they block only steps between cells)
					if (isSelected && nav.grid) {
						const auto& grid = *nav.grid;
						const glm::vec3 at(graph.GetWorldMatrix(entity)[3]);
						const float reach = 15.0f;
						int c0, r0, c1, r1;
						grid.CellAt(at.x - reach, at.z - reach, c0, r0);
						grid.CellAt(at.x + reach, at.z + reach, c1, r1);
						c0 = std::max(c0, 0);
						r0 = std::max(r0, 0);
						c1 = std::min(c1, grid.Columns() - 1);
						r1 = std::min(r1, grid.Rows() - 1);
						for (int r = r0; r <= r1; r++) {
							for (int c = c0; c <= c1; c++) {
								if (!grid.IsBlocked(c, r)) {
									continue;
								}
								const glm::vec2 p = grid.CellCentre(c, r);
								glm::vec2 pixel;
								if (Fox::Editor::TransformGizmo::Project(view, glm::vec3(p.x, grid.Height(c, r) + 0.05f, p.y), pixel)) {
									draw->AddCircleFilled(ImVec2(pixel.x, pixel.y), 1.6f * uiScale, IM_COL32(255, 70, 60, 150), 6);
								}
							}
						}
					}

					const ImU32 color = !nav.reachable ? IM_COL32(255, 150, 70, isSelected ? 240 : 140)
						: IM_COL32(90, 220, 255, isSelected ? 240 : 140);
					const float width = (isSelected ? 2.5f : 1.5f) * uiScale;

					// Along the ground (split into short pieces, so it follows the terrain), from where it is now
					std::vector<glm::vec3> way;
					if (scene.IsPhysicsPlaying() && nav.next < nav.path.size()) {
						way.push_back(glm::vec3(graph.GetWorldMatrix(entity)[3]));
						way.insert(way.end(), nav.path.begin() + nav.next, nav.path.end());
					}
					else if (!scene.IsPhysicsPlaying()) {
						way = nav.path;
					}
					std::vector<ImVec2> points;
					auto flush = [&] {
						if (points.size() > 1) {
							draw->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, width);
						}
						points.clear();
					};
					for (size_t i = 0; i + 1 < way.size(); i++) {
						const glm::vec3 a = way[i], b = way[i + 1];
						const int pieces = std::max(1, static_cast<int>(glm::distance(glm::vec2(a.x, a.z), glm::vec2(b.x, b.z)) / 0.5f));
						for (int k = (i == 0 ? 0 : 1); k <= pieces; k++) {
							glm::vec3 p = glm::mix(a, b, static_cast<float>(k) / pieces);
							p.y = scene.GetGroundHeightAt(p.x, p.z) + 0.08f;
							glm::vec2 pixel;
							if (Fox::Editor::TransformGizmo::Project(view, p, pixel)) {
								points.emplace_back(pixel.x, pixel.y);
							}
							else {
								flush();
							}
						}
					}
					flush();
					// Turns
					for (size_t i = 1; i + 1 < way.size(); i++) {
						glm::vec3 p = way[i];
						p.y = scene.GetGroundHeightAt(p.x, p.z) + 0.08f;
						glm::vec2 pixel;
						if (Fox::Editor::TransformGizmo::Project(view, p, pixel)) {
							draw->AddCircleFilled(ImVec2(pixel.x, pixel.y), 2.5f * uiScale, color);
						}
					}
					// The target: a ring on the ground
					const int segments = 24;
					ImVec2 ring[segments];
					int count = 0;
					for (int i = 0; i < segments; i++) {
						const float angle = glm::two_pi<float>() * i / segments;
						glm::vec3 p = nav.target + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * 0.4f;
						p.y = scene.GetGroundHeightAt(p.x, p.z) + 0.08f;
						glm::vec2 pixel;
						if (!Fox::Editor::TransformGizmo::Project(view, p, pixel)) {
							count = 0;
							break;
						}
						ring[count++] = ImVec2(pixel.x, pixel.y);
					}
					if (count > 2) {
						draw->AddPolyline(ring, count, color, ImDrawFlags_Closed, width);
					}
				});
			}

			void VulkanRHI::BuildMenus(const Fox::Scene::RayTracing::MainRayTracingScene::CameraPose& pose, const glm::vec3& spawn) {
				using Mode = Fox::Editor::TransformGizmo::Mode;
				auto& scene = *raytracingScene;
				auto& graph = scene.GetSceneGraph();
				const auto selected = gizmo.GetSelected();
				const bool hasSelection = graph.IsValid(selected);
				auto& shell = this->shell;

				shell.BeginFrame();

				// --- File ---
				shell.Add("File/New Scene", [&] { sceneEditor.NewScene(scene, gizmo); }, "", true, -1, "clear empty");
				shell.Add("File/Open Scene...", [&] {
					contentBrowser.ShowFolder("Scenes");
					shell.SetOpen(Window::ContentBrowser, true);
				}, "", true, -1, "load file json");
				shell.Add("File/Save Scene", [&scene, pose, this] { sceneEditor.Save(scene, pose); }, "Ctrl+S", true, -1,
					sceneEditor.GetPath());
				shell.Add("File/Save Scene As...", [&] { sceneEditor.RequestSaveAs(); }, "", true, -1, "file path");
				shell.Separator("File");
				shell.Add("File/Quit", [] {
					SDL_Event quit{};
					quit.type = SDL_QUIT;
					SDL_PushEvent(&quit);
				}, "Esc", true, -1, "exit close");

				// --- Edit ---
				// (Commands run later this frame, from the menu bar: what they need is captured by value or lives on)
				shell.Add("Edit/Duplicate", [this, &scene, selected] { gizmo.Select(scene.DuplicateEntity(selected)); }, "Ctrl+D", hasSelection, -1, "copy entity");
				shell.Add("Edit/Delete", [&] { RunKeyCommand(SDL_SCANCODE_DELETE); }, "Delete", hasSelection, -1, "remove entity");
				shell.Add("Edit/Deselect", [&] { gizmo.Select({}); }, "", hasSelection, -1, "selection");
				{
					const auto owner = hasSelection ? scene.PrefabRootOf(selected) : Fox::ECS::Entity{};
					const bool isInstance = hasSelection && owner == selected;
					shell.Add("Edit/Prefab/Create Prefab", [&scene, selected] {
						std::string file, error;
						if (!scene.CreatePrefab(selected, scene.GetSceneGraph().GetName(selected), file, error)) {
							std::cout << "Prefab: " << error << "\n";
						}
					}, "", hasSelection && (!graph.IsValid(owner) || isInstance), -1, "prefab save template reusable");
					shell.Add("Edit/Prefab/Apply to Prefab", [&scene, selected] {
						std::string error;
						if (!scene.ApplyPrefab(selected, error)) std::cout << "Prefab: " << error << "\n";
					}, "", isInstance, -1, "prefab update all instances");
					shell.Add("Edit/Prefab/Revert to Prefab", [&scene, selected] {
						std::string error;
						scene.RevertPrefab(selected, "", error);
					}, "", isInstance, -1, "prefab overrides reset");
					shell.Add("Edit/Prefab/Unpack Prefab", [&scene, selected] { scene.UnpackPrefab(selected); }, "", isInstance, -1, "prefab unlink");
				}
				shell.Add("Edit/Duplicate as Instance", [this, &scene, selected] { gizmo.Select(scene.DuplicateEntity(selected, true)); }, "",
					hasSelection, -1, "copy tree instance shared");
				shell.Add("Edit/Drop to Ground", [this, &scene, selected] { Fox::Editor::MeshPainter::DropToGround(scene, selected); }, "",
					hasSelection, -1, "snap floor terrain height");
				shell.Separator("Edit");
				shell.Add("Edit/Move Tool", [&] { RunKeyCommand(SDL_SCANCODE_Z); }, "Z", true, gizmo.GetMode() == Mode::Translate, "gizmo translate");
				shell.Add("Edit/Rotate Tool", [&] { RunKeyCommand(SDL_SCANCODE_X); }, "X", true, gizmo.GetMode() == Mode::Rotate, "gizmo turn");
				shell.Add("Edit/Scale Tool", [&] { RunKeyCommand(SDL_SCANCODE_C); }, "C", true, gizmo.GetMode() == Mode::Scale, "gizmo size");
				shell.Separator("Edit");
				shell.Add("Edit/Mesh Paint/Brush On", [&] { RunKeyCommand(SDL_SCANCODE_M); }, "M", true, meshPainter.IsActive() ? 1 : 0,
					"paint scatter foliage forest trees models");
				shell.Add("Edit/Mesh Paint/Mesh Paint Window", [&shell] { shell.SetOpen(Window::MeshPaint, true); }, "", true, -1, "paint scatter");
				shell.Add("Edit/Terrain Brush/Brush On", [&] {
					shell.SetOpen(Window::Terrain, true);
					RunKeyCommand(SDL_SCANCODE_B);
				}, "B", scene.GetEnvironment().terrain, terrainEditor.IsBrushActive(), "sculpt paint terrain");
				shell.Add("Edit/Terrain Brush/Bigger Brush", [&] { RunKeyCommand(SDL_SCANCODE_RIGHTBRACKET); }, "]", true, -1, "terrain radius");
				shell.Add("Edit/Terrain Brush/Smaller Brush", [&] { RunKeyCommand(SDL_SCANCODE_LEFTBRACKET); }, "[", true, -1, "terrain radius");
				shell.Separator("Edit");
				shell.Add("Edit/Leave Edit Mode", [&] { SetEditMode(false); }, "E", true, -1, "play exit");

				// --- Create ---
				shell.Add("Create/Empty", [&] {
					const auto entity = scene.CreateEmpty("Empty");
					Fox::Scene::Transform transform;
					transform.position = spawn;
					graph.SetWorldTransform(entity, transform);
					gizmo.Select(entity);
				}, "", true, -1, "entity group");
				shell.Add("Create/Empty Child", [this, &scene, selected] { gizmo.Select(scene.CreateEmpty("Empty child", selected)); }, "", hasSelection, -1, "entity group");
				shell.Separator("Create");
				shell.Add("Create/Model...", [&] {
					contentBrowser.ShowFolder("Models");
					shell.SetOpen(Window::ContentBrowser, true);
				}, "", true, -1, "gltf glb mesh content");
				for (int i = 0; i < Fox::Editor::SceneEditor::SPHERE_PRESET_COUNT; i++) {
					shell.Add(std::string("Create/Sphere/") + Fox::Editor::SceneEditor::SPHERE_PRESETS[i], [&, i] {
						gizmo.Select(scene.AddSphereObject(spawn + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, Fox::Editor::SceneEditor::SpherePreset(i)));
					}, "", true, -1, "ball mesh");
				}
				shell.Add("Create/Tree", [&] { gizmo.Select(scene.AddTree(spawn)); }, "", true, -1, "plant mesh");
				shell.Add("Create/Water", [&] { gizmo.Select(scene.AddWater(spawn + glm::vec3(0.0f, 0.8f, 0.0f))); }, "", true, -1,
					"lake pond river sea ocean fluid simulation");
				shell.Add("Create/Physics/Ball", [&] { gizmo.Select(scene.AddPhysicsBall(spawn + glm::vec3(-1.0f, 3.0f, 0.0f))); }, "", true, -1,
					"rigid body sphere drop");
				shell.Add("Create/Physics/Box", [&] { gizmo.Select(scene.AddPhysicsCrate(spawn + glm::vec3(1.0f, 3.0f, 0.0f))); }, "", true, -1,
					"rigid body crate cube drop");
				shell.Add("Create/Game Camera", [&] {
					gizmo.Select(scene.AddGameCamera(pose.position, glm::normalize(pose.target - pose.position), glm::degrees(Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera()->GetFov())));
				}, "", true, -1, "camera play view look through");
				prefabListAge += ImGui::GetIO().DeltaTime;
				if (prefabListAge > 2.0f) {
					prefabFiles = scene.ListPrefabs();
					prefabListAge = 0.0f;
				}
				for (const std::string& file : prefabFiles) {
					const std::string name = std::filesystem::path(file).stem().stem().string();
					shell.Add("Create/Prefab/" + name, [&scene, this, file, spawn] {
						std::string error;
						const auto entity = scene.InstantiatePrefab(file, spawn, {}, error);
						if (entity) {
							gizmo.Select(entity);
						}
						else {
							std::cout << "Prefab: " << error << "\n";
						}
					}, "", true, -1, "prefab instance place " + name);
				}
				if (prefabFiles.empty()) {
					shell.Add("Create/Prefab/(none yet: right-click an entity > Create Prefab)", [] {}, "", false, -1, "prefab");
				}
				auto addSound = [&scene, this](const glm::vec3& at, bool music) {
					const auto entity = scene.CreateEmpty(music ? "Music" : "Sound");
					Fox::Scene::Transform transform;
					transform.position = at;
					scene.GetSceneGraph().SetWorldTransform(entity, transform);
					auto& sound = scene.GetSceneGraph().GetRegistry().Add<Fox::Scene::SoundComponent>(entity);
					const auto files = scene.ListSounds();
					if (!files.empty()) sound.file = files.front();
					sound.music = music;
					sound.loop = music;
					gizmo.Select(entity);
				};
				shell.Add("Create/Audio/Sound", [addSound, spawn] { addSound(spawn + glm::vec3(0.0f, 1.0f, 0.0f), false); }, "", true, -1,
					"audio sfx effect 3d emitter speaker");
				shell.Add("Create/Audio/Background Music", [addSound, spawn] { addSound(spawn, true); }, "", true, -1, "audio song soundtrack loop");
				shell.Add("Create/Effects/Smoke", [&] { gizmo.Select(scene.AddSmoke(spawn + glm::vec3(0.0f, 2.5f, 0.0f))); }, "", true, -1,
					"volume volumetric smoke steam haze");
				shell.Add("Create/Effects/Glowing Smoke", [&] { gizmo.Select(scene.AddSmoke(spawn + glm::vec3(0.0f, 2.5f, 0.0f), true)); }, "", true, -1,
					"volume volumetric fire embers glow");
				shell.Add("Create/Effects/Fog", [&] { scene.GetEnvironment().fog.enabled = !scene.GetEnvironment().fog.enabled; }, "", true,
					scene.GetEnvironment().fog.enabled ? 1 : 0, "volumetric height fog mist haze atmosphere");
				shell.Add("Create/Effects/Clouds", [&] { scene.GetEnvironment().clouds.enabled = !scene.GetEnvironment().clouds.enabled; }, "", true,
					scene.GetEnvironment().clouds.enabled ? 1 : 0, "volumetric cloud layer sky weather");
				shell.Add("Create/Navigation/Agent", [&] { gizmo.Select(scene.AddNavAgent(spawn)); }, "", true, -1,
					"nav agent walker pathfinding a star character");
				shell.Separator("Create");
				shell.Add("Create/Light/Directional Light", [&] {
					gizmo.Select(scene.AddLight(LightComponent::Type::Directional, spawn + glm::vec3(0.0f, 20.0f, 0.0f)));
				}, "", true, -1, "sun");
				shell.Add("Create/Light/Point Light", [&] {
					gizmo.Select(scene.AddLight(LightComponent::Type::Point, spawn + glm::vec3(0.0f, 2.0f, 0.0f)));
				}, "", true, -1, "lamp bulb");
				shell.Add("Create/Light/Spot Light", [&] {
					gizmo.Select(scene.AddLight(LightComponent::Type::Spot, spawn + glm::vec3(0.0f, 4.0f, 0.0f)));
				}, "", true, -1, "lamp cone");

				// --- Play ---
				const bool playing = scene.IsPlaying();
				shell.Add("Play/Play", [this] { Defer([this] { TogglePlay(); }); }, "F5", true, playing ? 1 : 0,
					"start stop game simulate physics run");
				shell.Add("Play/Pause", [this] { RunKeyCommand(SDL_SCANCODE_F6); }, "F6", playing, playing && scene.GetClock().paused ? 1 : 0,
					"freeze game time resume");
				shell.Add("Play/Step Frame", [this] { RunKeyCommand(SDL_SCANCODE_F7); }, "F7", playing, -1, "advance one frame paused");
				shell.Add("Play/Free Camera", [this] { RunKeyCommand(SDL_SCANCODE_F8); }, "F8", playing, playing && cameraEjected ? 1 : 0,
					"eject possess game camera fly");
				shell.Add("Play/Reload Scripts", [&scene] {
					if (auto* host = scene.GetScripts()) {
						host->Reload();
					}
				}, "", scene.GetScripts() != nullptr, -1, "csharp c# compile hot reload");
				for (float speed : { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f }) {
					char label[32];
					std::snprintf(label, sizeof(label), "Play/Speed/%gx", speed);
					shell.Add(label, [&scene, speed] { scene.SetTimeScale(speed); }, "", true,
						scene.GetClock().timeScale == speed ? 1 : 0, "time scale slow motion fast forward");
				}

				// --- Physics ---
				shell.Add("Physics/Add Body to Selection", [&scene, selected] {
					auto& registry = scene.GetSceneGraph().GetRegistry();
					if (!registry.Has<Fox::Scene::PhysicsComponent>(selected)) {
						registry.Add<Fox::Scene::PhysicsComponent>(selected);
					}
				}, "", hasSelection, -1, "rigid body component");
				shell.Add("Physics/Add Nav Agent to Selection", [&scene, selected] {
					auto& registry = scene.GetSceneGraph().GetRegistry();
					if (!registry.Has<Fox::Scene::NavAgentComponent>(selected)) {
						registry.Add<Fox::Scene::NavAgentComponent>(selected);
					}
				}, "", hasSelection, -1, "navigation pathfinding a star walk agent component");
				shell.Add("Physics/Show Agent Paths", [this] { showAgentPaths = !showAgentPaths; }, "", true, showAgentPaths ? 1 : 0,
					"navigation pathfinding a star route overlay");

				// --- Window ---
				auto windowItem = [&](Window window, const std::string& label, const std::string& shortcut, const std::string& keywords) {
					shell.Add("Window/" + label, [&shell, window] { shell.Toggle(window); }, shortcut, true, shell.IsOpen(window) ? 1 : 0,
						"show hide panel " + keywords);
				};
				windowItem(Window::Hierarchy, "Hierarchy", "", "entities tree scene graph");
				windowItem(Window::Inspector, "Inspector", "", "components properties");
				windowItem(Window::ContentBrowser, "Content Browser", "Ctrl+Space", "files assets models scenes textures");
				shell.Separator("Window");
				windowItem(Window::Environment, "Environment", "", "ground plane");
				windowItem(Window::Trees, "Trees", "", "tree editor");
				windowItem(Window::Terrain, "Terrain", "", "sculpt paint");
				windowItem(Window::MeshPaint, "Mesh Paint", "", "scatter paint trees models density foliage");
				windowItem(Window::Camera, "Camera Paths", "", "flight keys");
				windowItem(Window::Input, "Input Actions", "", "keys bindings controls jump axis rebind");
				shell.Separator("Window");
				shell.Add("Window/Close All", [&shell] {
					for (int i = 0; i < static_cast<int>(Window::Count); i++) {
						shell.SetOpen(static_cast<Window>(i), false);
					}
				}, "", true, -1, "hide");
				shell.Add("Window/Reset Layout", [&shell] { shell.ResetLayout(); }, "", true, -1, "arrange tidy positions");

				// --- Render (between frames: these replace rendering resources) ---
				for (size_t i = 0; i < denoisers.size(); i++) {
					shell.Add(std::string("Render/Denoiser/") + denoisers[i]->GetName(), [this, i] { Defer([this, i] { SelectDenoiser(i); }); },
						"", true, i == activeDenoiser ? 1 : 0, "denoise");
				}
				shell.Add("Render/Denoiser/Next Denoiser", [this] { Defer([this] { RunKeyCommand(SDL_SCANCODE_N); }); }, "N", true, -1, "denoise");
				for (int i = 0; i < static_cast<int>(RayTracing::UpscalingMode::Count); i++) {
					const auto mode = static_cast<RayTracing::UpscalingMode>(i);
					shell.Add(std::string("Render/Resolution/") + RayTracing::UpscalingModeName(mode), [this, mode] { Defer([this, mode] { SetUpscalingMode(mode); }); },
						"", true, mode == upscalingMode ? 1 : 0, "upscaling quality dlss");
				}
				shell.Add("Render/Resolution/Next", [this] { Defer([this] { RunKeyCommand(SDL_SCANCODE_U); }); }, "U", true, -1, "upscaling");
				for (float pixels : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f }) {
					char label[64];
					std::snprintf(label, sizeof(label), "Render/Tree Detail/%g pixel%s of error", pixels, pixels == 1.0f ? "" : "s");
					shell.Add(label, [this, pixels] { raytracingScene->SetClusterLodThreshold(pixels); }, "",
						raytracingScene->HasClusterMeshes(), raytracingScene->GetClusterLodThreshold() == pixels ? 1 : 0, "lod level of detail clusters nanite quality");
				}
				for (RenderPreset p : { RenderPreset::Auto, RenderPreset::Quality, RenderPreset::Balanced, RenderPreset::Performance }) {
					std::string label = std::string("Render/Preset/") + PresetName(p);
					if (p == RenderPreset::Auto) {
						label += std::string(" (") + PresetName(modestGpu ? RenderPreset::Balanced : RenderPreset::Quality) + " here)";
					}
					shell.Add(label, [this, p] { Defer([this, p] { SetRenderPreset(p); }); }, "", true, preset == p ? 1 : 0,
						"render speed laptop bounces frame cap fps");
				}
				shell.Add("Render/Vsync", [this] { Defer([this] { RunKeyCommand(SDL_SCANCODE_V); }); }, "V", true, vsync ? 1 : 0, "frame rate");
				shell.Separator("Render");
				char samples[64];
				std::snprintf(samples, sizeof(samples), "Render/More Samples per Pixel (now %u)", samplesPerPixel);
				shell.Add(samples, [this] { RunKeyCommand(SDL_SCANCODE_4); }, "4", samplesPerPixel < 16, -1, "spp quality");
				std::snprintf(samples, sizeof(samples), "Render/Fewer Samples per Pixel (now %u)", samplesPerPixel);
				shell.Add(samples, [this] { RunKeyCommand(SDL_SCANCODE_3); }, "3", samplesPerPixel > 1, -1, "spp speed");
				shell.Add("Render/Brighter Exposure", [this] { RunKeyCommand(SDL_SCANCODE_2); }, "2", true, -1, "ev light");
				shell.Add("Render/Darker Exposure", [this] { RunKeyCommand(SDL_SCANCODE_1); }, "1", true, -1, "ev dark");
				shell.Add("Render/Brighter Sky", [this] { RunKeyCommand(SDL_SCANCODE_0); }, "0", true, -1, "environment map intensity");
				shell.Add("Render/Darker Sky", [this] { RunKeyCommand(SDL_SCANCODE_9); }, "9", true, -1, "environment map intensity");

				// --- Camera ---
				auto& cameraManager = scene.GetCameraManager();
				shell.Add("Camera/Fly the Path", [this] { RunKeyCommand(SDL_SCANCODE_P); }, "P", true, cameraManager.IsPlaying() ? 1 : 0, "play stop animation");
				shell.Add("Camera/Add Key from the View", [this] { RunKeyCommand(SDL_SCANCODE_K); }, "K", true, -1, "path keyframe");
				shell.Add("Camera/Camera Paths Window", [&shell] { shell.SetOpen(Window::Camera, true); }, "", true, -1, "edit paths");

				// --- Help ---
				shell.Add("Help/Find a Command", [&shell] { shell.FocusFind(); }, "F1", true, -1, "search menu");
				shell.Add("Help/Controls", [&shell] { shell.Toggle(Window::Controls); }, "", true, shell.IsOpen(Window::Controls) ? 1 : 0,
					"keys keyboard mouse shortcuts help");
			}

			void VulkanRHI::DrawInputActions() {
				using Actions = Fox::Input::InputActions;
				auto& scene = *raytracingScene;
				Actions& input = scene.GetInputActions();
				if (!ImGui::Begin("Input Actions", shell.OpenFlag(Window::Input))) {
					ImGui::End();
					return;
				}
				bool changed = false;

				// Listening: the first key pressed becomes the binding (Escape: never mind)
				if (CapturingInput()) {
					const auto& keys = scene.GetScriptInput();
					for (int code = 1; code < 512; code++) {
						if (!keys.keys[code] || keys.previousKeys[code]) {
							continue;
						}
						if (code != SDL_SCANCODE_ESCAPE) {
							Actions::Binding binding;
							binding.code = code;
							std::vector<Actions::Binding>* list = nullptr;
							if (inputCapture.action >= 0 && inputCapture.action < static_cast<int>(input.actions.size())) {
								list = &input.actions[inputCapture.action].bindings;
							}
							else if (inputCapture.axis >= 0 && inputCapture.axis < static_cast<int>(input.axes.size())) {
								auto& axis = input.axes[inputCapture.axis];
								list = inputCapture.negative ? &axis.negative : &axis.positive;
							}
							if (list && std::find(list->begin(), list->end(), binding) == list->end()) {
								list->push_back(binding);
								changed = true;
							}
						}
						inputCapture = InputCapture{};
						break;
					}
				}

				ImGui::TextWrapped("Scripts ask for these by name: Input.GetAction(\"Jump\"), Input.GetAxis(\"MoveX\"). Saved to %s.", Actions::FILE);

				// A row of bindings: each a button that removes it, then + to listen for a key, and the mouse buttons
				auto bindings = [&](std::vector<Actions::Binding>& list, bool listening, const std::function<void()>& startListening) {
					int remove = -1;
					for (size_t i = 0; i < list.size(); i++) {
						ImGui::PushID(static_cast<int>(i));
						if (ImGui::SmallButton(Actions::BindingName(list[i]).c_str())) {
							remove = static_cast<int>(i);
						}
						ImGui::SetItemTooltip("Click to remove");
						ImGui::PopID();
						ImGui::SameLine();
					}
					if (remove >= 0) {
						list.erase(list.begin() + remove);
						changed = true;
					}
					if (listening) {
						ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "press a key... (Esc: cancel)");
					}
					else {
						if (ImGui::SmallButton("+ key")) {
							startListening();
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("+ mouse")) {
							ImGui::OpenPopup("mouse");
						}
						if (ImGui::BeginPopup("mouse")) {
							for (int button = 0; button < 3; button++) {
								Actions::Binding binding;
								binding.kind = Actions::Binding::Kind::Mouse;
								binding.code = button;
								if (ImGui::Selectable(Actions::BindingName(binding).c_str())
									&& std::find(list.begin(), list.end(), binding) == list.end()) {
									list.push_back(binding);
									changed = true;
								}
							}
							ImGui::EndPopup();
						}
					}
				};

				auto nameField = [&](std::string& name) {
					char text[64];
					std::snprintf(text, sizeof(text), "%s", name.c_str());
					ImGui::SetNextItemWidth(130.0f * uiScale);
					if (ImGui::InputText("##name", text, sizeof(text), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)
						|| (ImGui::IsItemDeactivatedAfterEdit())) {
						name = text;
						changed = true;
					}
				};

				ImGui::SeparatorText("Actions (buttons)");
				int removeAction = -1;
				for (size_t i = 0; i < input.actions.size(); i++) {
					auto& action = input.actions[i];
					ImGui::PushID(static_cast<int>(i));
					nameField(action.name);
					ImGui::SameLine();
					const bool listening = inputCapture.action == static_cast<int>(i);
					bindings(action.bindings, listening, [&] { inputCapture = InputCapture{}; inputCapture.action = static_cast<int>(i); });
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) {
						removeAction = static_cast<int>(i);
					}
					ImGui::SetItemTooltip("Remove the action");
					ImGui::PopID();
				}
				if (removeAction >= 0) {
					input.actions.erase(input.actions.begin() + removeAction);
					inputCapture = InputCapture{};
					changed = true;
				}
				if (ImGui::Button("Add action")) {
					input.actions.push_back({ "Action" + std::to_string(input.actions.size() + 1), {} });
					changed = true;
				}

				ImGui::SeparatorText("Axes (-1..1)");
				int removeAxis = -1;
				for (size_t i = 0; i < input.axes.size(); i++) {
					auto& axis = input.axes[i];
					ImGui::PushID(1000 + static_cast<int>(i));
					nameField(axis.name);
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) {
						removeAxis = static_cast<int>(i);
					}
					ImGui::SetItemTooltip("Remove the axis");
					ImGui::Indent();
					ImGui::PushID("positive");
					ImGui::TextDisabled("+1");
					ImGui::SameLine();
					bindings(axis.positive, inputCapture.axis == static_cast<int>(i) && !inputCapture.negative,
						[&] { inputCapture = InputCapture{}; inputCapture.axis = static_cast<int>(i); });
					ImGui::PopID();
					ImGui::PushID("negative");
					ImGui::TextDisabled("-1");
					ImGui::SameLine();
					bindings(axis.negative, inputCapture.axis == static_cast<int>(i) && inputCapture.negative,
						[&] { inputCapture = InputCapture{}; inputCapture.axis = static_cast<int>(i); inputCapture.negative = true; });
					ImGui::PopID();
					ImGui::Unindent();
					ImGui::PopID();
				}
				if (removeAxis >= 0) {
					input.axes.erase(input.axes.begin() + removeAxis);
					inputCapture = InputCapture{};
					changed = true;
				}
				if (ImGui::Button("Add axis")) {
					input.axes.push_back({ "Axis" + std::to_string(input.axes.size() + 1), {}, {} });
					changed = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset to defaults")) {
					input = Actions::Defaults();
					inputCapture = InputCapture{};
					changed = true;
				}

				if (changed) {
					std::string error;
					if (!scene.SaveInputActions(error)) {
						std::cout << "Input: " << error << "\n";
					}
				}
				ImGui::End();
			}

			void VulkanRHI::DrawControls() {
				if (!ImGui::Begin("Controls", shell.OpenFlag(Window::Controls))) {
					ImGui::End();
					return;
				}
				const char* rows[][2] = {
					{ "E", "Edit mode on / off (the mouse pointer and these windows)" },
					{ "F1", "Find a command: type part of its name" },
					{ "Ctrl+Space", "Content Browser" },
					{ "W A S D, R F", "Fly (right mouse button + drag looks around in edit mode)" },
					{ "Left click", "Select what is under the pointer" },
					{ "Z / X / C", "Move / rotate / scale gizmo; Ctrl while dragging snaps" },
					{ "Delete", "Delete the selected entity and everything under it" },
					{ "Ctrl+D", "Duplicate the selected entity" },
					{ "Ctrl+S", "Save the scene" },
					{ "B, [ ]", "Terrain brush on / off, its radius" },
					{ "M, [ ]", "Mesh paint brush on / off, its radius; Ctrl + left mouse erases" },
					{ "K, P", "Camera path: add a key from the view, fly the path" },
					{ "N, U, V", "Next denoiser, render resolution, vsync" },
					{ "1 / 2", "Exposure down / up" },
					{ "3 / 4", "Samples per pixel down / up" },
					{ "9 / 0", "Sky darker / brighter" },
					{ "T", "Fullscreen" },
					{ "Esc", "Quit" },
				};
				if (ImGui::BeginTable("##controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
					for (const auto& row : rows) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(row[0]);
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(row[1]);
					}
					ImGui::EndTable();
				}
				ImGui::End();
			}
		}
	}
}
