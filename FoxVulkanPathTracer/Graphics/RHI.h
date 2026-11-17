#pragma once

namespace Fox {

	namespace Graphics {

		// Rendering backend interface used by the platform layer.
		class RHI {
		public:
			RHI() = default;
			virtual ~RHI() {}

			// Returns false if the backend could not be brought up; the error is logged.
			virtual bool Initialize(const Fox::Graphics::RendererConfig& config) = 0;
			virtual void Destroy() = 0;

			// Renders and presents one frame. deltaTime is the time since the previous frame in seconds.
			virtual void Render(float deltaTime) = 0;

			// Call after the window's drawable size changed.
			virtual void Resize(uint32_t width, uint32_t height) = 0;
			virtual void RegisterInput(Fox::Input::InputManager& input) = 0;

			// Gives the user interface the first look at a window event. Returns true when the interface takes it
			// (e.g. a click on an editor window), so the rest of the application should ignore it.
			virtual bool HandleEvent(const SDL_Event& event) { return false; }

		};


	}

}
