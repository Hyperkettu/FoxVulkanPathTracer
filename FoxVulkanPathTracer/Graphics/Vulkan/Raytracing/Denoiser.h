#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				class Denoiser {
				public:
					Denoiser() {}
					virtual ~Denoiser() {}

					virtual Fox::Graphics::Vulkan::Texture* Denoise(Fox::Graphics::Vulkan::Texture* colorTexture) = 0;

				};
			}
		}

	}
}