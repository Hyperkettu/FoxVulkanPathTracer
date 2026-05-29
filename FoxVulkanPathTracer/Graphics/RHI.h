#pragma once

namespace Fox {

	namespace Graphics {
	
		class RHI {
		public:
			RHI() = default;
			virtual ~RHI() {}

			virtual int32_t Initialize(const Fox::Graphics::RendererConfig& config) = 0;
			virtual int32_t Destroy() = 0;
			virtual void Render() = 0;
			virtual void RegisterInput(Fox::Input::InputManager& input) = 0;

		};

	
	}

}