#pragma once 

namespace Fox {

	namespace Graphics {
	
		struct RendererConfig {
			RendererConfig() = default;
			~RendererConfig() {};

			uint32_t MAX_FRAMES_IN_FLIGHT;

			uint32_t extensionCount;
			std::vector<const char*> instanceExtensions;

			uint32_t layerCount;
			std::vector<const char*> layers;

			void* windowHandle;

		};
	
	}
}