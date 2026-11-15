#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				class SpatiotemporalVarianceGuidedFilter : public Fox::Graphics::Vulkan::RayTracing::Denoiser {
				public:

					SpatiotemporalVarianceGuidedFilter() = default;
					virtual ~SpatiotemporalVarianceGuidedFilter();

					void Initialize();

					Fox::Graphics::Vulkan::Texture* Denoise(Fox::Graphics::Vulkan::Texture* colorTexture) override;

				};
			}
		}
	}
}