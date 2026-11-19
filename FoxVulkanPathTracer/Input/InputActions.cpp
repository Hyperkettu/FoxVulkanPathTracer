#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <filesystem>
#include <fstream>

namespace Fox {

	namespace Input {

		namespace {
			const char* MOUSE_NAMES[3] = { "Mouse Left", "Mouse Middle", "Mouse Right" };

			InputActions::Binding Key(SDL_Scancode code) {
				InputActions::Binding binding;
				binding.kind = InputActions::Binding::Kind::Key;
				binding.code = static_cast<int>(code);
				return binding;
			}

			InputActions::Binding Mouse(int button) {
				InputActions::Binding binding;
				binding.kind = InputActions::Binding::Kind::Mouse;
				binding.code = button;
				return binding;
			}

			nlohmann::json Names(const std::vector<InputActions::Binding>& bindings) {
				nlohmann::json list = nlohmann::json::array();
				for (const auto& binding : bindings) {
					list.push_back(InputActions::BindingName(binding));
				}
				return list;
			}

			std::vector<InputActions::Binding> Parse(const nlohmann::json& list, const std::string& what) {
				std::vector<InputActions::Binding> bindings;
				for (const auto& entry : list) {
					InputActions::Binding binding;
					const std::string name = entry.is_string() ? entry.get<std::string>() : std::string();
					if (InputActions::ParseBinding(name, binding)) {
						bindings.push_back(binding);
					}
					else {
						std::cout << "Input: " << what << ": no key called \"" << name << "\"\n";
					}
				}
				return bindings;
			}
		}

		InputActions InputActions::Defaults() {
			InputActions input;
			input.actions = {
				{ "Jump", { Key(SDL_SCANCODE_SPACE) } },
				{ "Fire", { Mouse(0), Key(SDL_SCANCODE_RETURN) } },
				{ "Interact", { Key(SDL_SCANCODE_F) } },
				{ "Sprint", { Key(SDL_SCANCODE_LSHIFT) } },
				{ "Blast", { Key(SDL_SCANCODE_Q) } },
				{ "Drop", { Key(SDL_SCANCODE_B) } },
			};
			input.axes = {
				{ "MoveX", { Key(SDL_SCANCODE_D), Key(SDL_SCANCODE_RIGHT) }, { Key(SDL_SCANCODE_A), Key(SDL_SCANCODE_LEFT) } },
				{ "MoveY", { Key(SDL_SCANCODE_W), Key(SDL_SCANCODE_UP) }, { Key(SDL_SCANCODE_S), Key(SDL_SCANCODE_DOWN) } },
			};
			return input;
		}

		bool InputActions::Load(const std::string& path, std::string& error) {
			if (!std::filesystem::exists(path)) {
				*this = Defaults();
				std::string ignored;
				Save(path, ignored);
				return true;
			}
			try {
				std::ifstream in(path);
				const nlohmann::json file = nlohmann::json::parse(in, nullptr, true, true);
				InputActions loaded;
				for (const auto& entry : file.value("actions", nlohmann::json::array())) {
					Action action;
					action.name = entry.value("name", std::string());
					action.bindings = Parse(entry.value("bindings", nlohmann::json::array()), action.name);
					if (!action.name.empty()) loaded.actions.push_back(std::move(action));
				}
				for (const auto& entry : file.value("axes", nlohmann::json::array())) {
					Axis axis;
					axis.name = entry.value("name", std::string());
					axis.positive = Parse(entry.value("positive", nlohmann::json::array()), axis.name);
					axis.negative = Parse(entry.value("negative", nlohmann::json::array()), axis.name);
					if (!axis.name.empty()) loaded.axes.push_back(std::move(axis));
				}
				*this = std::move(loaded);
				return true;
			}
			catch (const std::exception& e) {
				error = path + ": " + e.what();
				return false;
			}
		}

		bool InputActions::Save(const std::string& path, std::string& error) const {
			nlohmann::json file;
			file["actions"] = nlohmann::json::array();
			for (const Action& action : actions) {
				file["actions"].push_back({ { "name", action.name }, { "bindings", Names(action.bindings) } });
			}
			file["axes"] = nlohmann::json::array();
			for (const Axis& axis : axes) {
				file["axes"].push_back({ { "name", axis.name }, { "positive", Names(axis.positive) }, { "negative", Names(axis.negative) } });
			}
			try {
				const std::filesystem::path target(path);
				if (target.has_parent_path()) {
					std::filesystem::create_directories(target.parent_path());
				}
				std::ofstream out(path);
				if (!out) {
					error = "Cannot write " + path;
					return false;
				}
				out << file.dump(2) << "\n";
				return true;
			}
			catch (const std::exception& e) {
				error = path + ": " + e.what();
				return false;
			}
		}

		const InputActions::Action* InputActions::FindAction(const std::string& name) const {
			for (const Action& action : actions) {
				if (action.name == name) return &action;
			}
			return nullptr;
		}

		const InputActions::Axis* InputActions::FindAxis(const std::string& name) const {
			for (const Axis& axis : axes) {
				if (axis.name == name) return &axis;
			}
			return nullptr;
		}

		std::string InputActions::BindingName(const Binding& binding) {
			if (binding.kind == Binding::Kind::Mouse) {
				return binding.code >= 0 && binding.code < 3 ? MOUSE_NAMES[binding.code] : "Mouse " + std::to_string(binding.code);
			}
			const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.code));
			return name && name[0] ? name : "Key " + std::to_string(binding.code);
		}

		bool InputActions::ParseBinding(const std::string& name, Binding& binding) {
			for (int i = 0; i < 3; i++) {
				if (name == MOUSE_NAMES[i]) {
					binding = Mouse(i);
					return true;
				}
			}
			const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
			if (code == SDL_SCANCODE_UNKNOWN) {
				return false;
			}
			binding = Key(code);
			return true;
		}

		bool InputActions::AnyHeld(const std::vector<Binding>& bindings, const uint8_t* keys, const uint8_t* buttons) {
			for (const Binding& binding : bindings) {
				if (binding.kind == Binding::Kind::Key ? (binding.code >= 0 && binding.code < 512 && keys[binding.code])
					: (binding.code >= 0 && binding.code < 3 && buttons[binding.code])) {
					return true;
				}
			}
			return false;
		}
	}
}
