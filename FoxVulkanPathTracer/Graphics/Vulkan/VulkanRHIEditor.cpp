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
				if (shell.IsOpen(Window::Controls)) {
					shell.Place(Window::Controls, uiScale);
					DrawControls();
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
				hint += "   |   F1 find   Ctrl+Space content   E leave edit mode";
				shell.DrawStatusBar(status, isError, hint, uiScale);
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
				shell.Separator("Edit");
				shell.Add("Edit/Move Tool", [&] { RunKeyCommand(SDL_SCANCODE_Z); }, "Z", true, gizmo.GetMode() == Mode::Translate, "gizmo translate");
				shell.Add("Edit/Rotate Tool", [&] { RunKeyCommand(SDL_SCANCODE_X); }, "X", true, gizmo.GetMode() == Mode::Rotate, "gizmo turn");
				shell.Add("Edit/Scale Tool", [&] { RunKeyCommand(SDL_SCANCODE_C); }, "C", true, gizmo.GetMode() == Mode::Scale, "gizmo size");
				shell.Separator("Edit");
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
				windowItem(Window::Camera, "Camera Paths", "", "flight keys");
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
