#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fox {

	namespace Input {

		// Named input for the game (scripts: Input.GetAction("Jump"), Input.GetAxis("MoveX")), mapped to keys and mouse
		// buttons in Settings/Input.json (the Input Actions window edits it). Scripts ask for what the player means,
		// not which key it is, and the keys can change without touching them.
		//
		//   action  a button: held while any of its bindings is held
		//   axis    -1..1: +1 while a positive binding is held, -1 while a negative one is (0 for both or neither)
		class InputActions {
		public:
			struct Binding {
				enum class Kind { Key, Mouse } kind = Kind::Key;
				int code = 0; // SDL scancode, or mouse button 0 left, 1 middle, 2 right
				bool operator==(const Binding& other) const { return kind == other.kind && code == other.code; }
			};
			struct Action {
				std::string name;
				std::vector<Binding> bindings;
			};
			struct Axis {
				std::string name;
				std::vector<Binding> positive, negative;
			};

			std::vector<Action> actions;
			std::vector<Axis> axes;

			static constexpr const char* FILE = "Settings/Input.json";

			// Jump, Fire, Interact, ... and MoveX / MoveY on W A S D and the arrows
			static InputActions Defaults();

			// From the file; the defaults (written to it) when there is none. False with error when it is unreadable.
			bool Load(const std::string& path, std::string& error);
			bool Save(const std::string& path, std::string& error) const;

			const Action* FindAction(const std::string& name) const;
			const Axis* FindAxis(const std::string& name) const;

			// "Space", "Left Shift", "Mouse Left": what the file and the window show
			static std::string BindingName(const Binding& binding);
			static bool ParseBinding(const std::string& name, Binding& binding);

			// Whether any of the bindings is held, given which keys (512 scancodes) and mouse buttons (3) are down
			static bool AnyHeld(const std::vector<Binding>& bindings, const uint8_t* keys, const uint8_t* buttons);
		};
	}
}
