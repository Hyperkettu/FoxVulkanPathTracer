#include "FoxRenderer.h"

#ifdef FOX_ENABLE_DLSS

#include <filesystem>

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_params_dlssd.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				namespace {
					// Identifies this application to NGX. Any GUID works for a custom engine.
					const char* const PROJECT_ID = "7c3f0a52-9b1e-4d6a-8f24-5e0b6c1d9a37";
					const char* const ENGINE_VERSION = "1.0";

					const VkFormat OUTPUT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					const VkFormat NORMAL_ROUGHNESS_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					const VkFormat DEPTH_FORMAT = VK_FORMAT_R32_SFLOAT;
					const VkFormat MOTION_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					const VkFormat HIT_DISTANCE_FORMAT = VK_FORMAT_R32_SFLOAT;

					// Must match the push constant block in Shaders/dlss_prepare.slang
					struct PrepareConstants {
						float farPlane;
					};

					// NGX writes its logs here
					const std::wstring& ApplicationDataPath() {
						static const std::wstring path = [] {
							std::error_code error;
							std::filesystem::path directory = std::filesystem::temp_directory_path(error) / "FoxVulkanPathTracer";
							std::filesystem::create_directories(directory, error);
							return directory.wstring();
						}();
						return path;
					}

					NVSDK_NGX_FeatureDiscoveryInfo MakeDiscoveryInfo() {
						static const NVSDK_NGX_FeatureCommonInfo commonInfo{};

						NVSDK_NGX_FeatureDiscoveryInfo info{};
						info.SDKVersion = NVSDK_NGX_Version_API;
						info.FeatureID = NVSDK_NGX_Feature_RayReconstruction;
						info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
						info.Identifier.v.ProjectDesc.ProjectId = PROJECT_ID;
						info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
						info.Identifier.v.ProjectDesc.EngineVersion = ENGINE_VERSION;
						info.ApplicationDataPath = ApplicationDataPath().c_str();
						info.FeatureInfo = &commonInfo;
						return info;
					}

					std::vector<std::string> ToNames(const VkExtensionProperties* properties, uint32_t count) {
						std::vector<std::string> names;
						for (uint32_t i = 0; i < count; i++) {
							names.emplace_back(properties[i].extensionName);
						}
						return names;
					}

					std::string Describe(NVSDK_NGX_Result result) {
						// The messages are plain ASCII
						std::string text;
						for (const wchar_t* c = GetNGXResultAsString(result); c && *c; c++) {
							text.push_back(static_cast<char>(*c));
						}
						return text;
					}

					void ThrowIfFailed(NVSDK_NGX_Result result, const char* what) {
						if (NVSDK_NGX_FAILED(result)) {
							throw std::runtime_error(std::string(what) + " failed: " + Describe(result));
						}
					}

					NVSDK_NGX_Resource_VK Wrap(const Texture& texture, VkFormat format, VkExtent2D extent, bool writable) {
						const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						return NVSDK_NGX_Create_ImageView_Resource_VK(texture.GetView(), texture.GetImage(), range, format, extent.width, extent.height, writable);
					}
				}

				std::vector<std::string> DlssRayReconstruction::GetInstanceExtensions() {
					const NVSDK_NGX_FeatureDiscoveryInfo info = MakeDiscoveryInfo();

					uint32_t count = 0;
					VkExtensionProperties* properties = nullptr;
					if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &properties))) {
						return {};
					}

					return ToNames(properties, count);
				}

				std::vector<std::string> DlssRayReconstruction::GetDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice) {
					const NVSDK_NGX_FeatureDiscoveryInfo info = MakeDiscoveryInfo();

					uint32_t count = 0;
					VkExtensionProperties* properties = nullptr;
					if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physicalDevice, &info, &count, &properties))) {
						return {};
					}

					return ToNames(properties, count);
				}

				DlssRayReconstruction::DlssRayReconstruction(const DeviceContext& context)
					: context(context)
					, preparePass(context.device, "Shaders/dlss_prepare.spv", 7, 1, sizeof(PrepareConstants))
				{
					ThrowIfFailed(
						NVSDK_NGX_VULKAN_Init_with_ProjectID(
							PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, ENGINE_VERSION, ApplicationDataPath().c_str(),
							context.instance, context.physicalDevice, context.device,
							vkGetInstanceProcAddr, vkGetDeviceProcAddr),
						"NGX initialization");

					NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&parameters);
					if (NVSDK_NGX_FAILED(result)) {
						NVSDK_NGX_VULKAN_Shutdown1(context.device);
						throw std::runtime_error("Querying NGX capabilities failed: " + Describe(result));
					}

					int available = 0;
					NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);

					if (!available) {
						int needsUpdatedDriver = 0;
						NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsUpdatedDriver);

						NVSDK_NGX_VULKAN_DestroyParameters(parameters);
						NVSDK_NGX_VULKAN_Shutdown1(context.device);

						throw std::runtime_error(needsUpdatedDriver
							? "the graphics driver is too old"
							: "not supported on this system (RTX GPU and nvngx_dlssd.dll next to the executable required)");
					}
				}

				DlssRayReconstruction::~DlssRayReconstruction() {
					vkDeviceWaitIdle(context.device);

					ReleaseFeature();

					if (parameters) {
						NVSDK_NGX_VULKAN_DestroyParameters(parameters);
					}

					NVSDK_NGX_VULKAN_Shutdown1(context.device);
				}

				void DlssRayReconstruction::ReleaseFeature() {
					if (feature) {
						NVSDK_NGX_VULKAN_ReleaseFeature(feature);
						feature = nullptr;
					}
				}

				void DlssRayReconstruction::Resize(const GBuffer& newGBuffer, const OutputSettings& outputSettings) {
					gbuffer = &newGBuffer;
					extent = newGBuffer.extent;
					outputExtent = outputSettings.displayExtent;

					ReleaseFeature();

					// Guide buffers at render resolution, the reconstructed image at display resolution
					normalRoughness = CreateStorageImage(context, extent, NORMAL_ROUGHNESS_FORMAT);
					depth = CreateStorageImage(context, extent, DEPTH_FORMAT);
					motion = CreateStorageImage(context, extent, MOTION_FORMAT);
					specularHitDistance = CreateStorageImage(context, extent, HIT_DISTANCE_FORMAT);
					output = CreateStorageImage(context, outputExtent, OUTPUT_FORMAT);

					// Binding order mirrors the declarations in Shaders/dlss_prepare.slang
					preparePass.BindImages(0, {
						newGBuffer.normalDepth->GetView(),
						newGBuffer.specular->GetView(),
						newGBuffer.motion->GetView(),
						normalRoughness->GetView(),
						depth->GetView(),
						motion->GetView(),
						specularHitDistance->GetView(),
					});

					// The network denoises, anti-aliases and (outside DLAA) upscales to the display resolution
					NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_DLAA;
					switch (outputSettings.mode) {
					case UpscalingMode::Quality: quality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break;
					case UpscalingMode::Balanced: quality = NVSDK_NGX_PerfQuality_Value_Balanced; break;
					case UpscalingMode::Performance: quality = NVSDK_NGX_PerfQuality_Value_MaxPerf; break;
					case UpscalingMode::UltraPerformance: quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance; break;
					default: break;
					}

					NVSDK_NGX_DLSSD_Create_Params createParams{};
					createParams.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
					createParams.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed; // Roughness lives in normals.w
					createParams.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
					createParams.InWidth = extent.width;
					createParams.InHeight = extent.height;
					createParams.InTargetWidth = outputExtent.width;
					createParams.InTargetHeight = outputExtent.height;
					createParams.InPerfQualityValue = quality;
					// The input is linear HDR radiance; motion vectors are at render resolution and exclude the jitter
					createParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

					// Preset F is the newest Ray Reconstruction model; presets are chosen per quality mode
					for (const char* presetParameter : {
						NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
						NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
						NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
						NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
						NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance }) {
						NVSDK_NGX_Parameter_SetUI(parameters, presetParameter, NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F);
					}

					// Creating the feature records setup commands that must run before the first evaluation
					CommandList commandList(context.device, context.setupCommandPool);
					commandList.Begin();

					const NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSSD_EXT1(context.device, commandList.Get(), 1, 1, &feature, parameters, &createParams);

					commandList.End().SubmitAndWait(context.graphicsQueue);

					// The capability query can pass and the feature still be refused: this DLSS is newer than the driver
					if (result == NVSDK_NGX_Result_FAIL_OutOfDate) {
						throw std::runtime_error("the NVIDIA driver is too old for this DLSS version (update the driver)");
					}
					ThrowIfFailed(result, "Creating the DLSS Ray Reconstruction feature");
				}

				void DlssRayReconstruction::Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) {
					// 1. Repack the G-buffer
					const PrepareConstants prepareConstants{ frame.farPlane };
					preparePass.Dispatch(commandList, extent, 0, &prepareConstants);

					commandList.MemoryBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

					// 2. Evaluate the network
					NVSDK_NGX_Resource_VK colorResource = Wrap(*gbuffer->radiance, GBuffer::RADIANCE_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK diffuseAlbedoResource = Wrap(*gbuffer->albedo, GBuffer::ALBEDO_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK specularAlbedoResource = Wrap(*gbuffer->specular, GBuffer::SPECULAR_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK normalResource = Wrap(*normalRoughness, NORMAL_ROUGHNESS_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK depthResource = Wrap(*depth, DEPTH_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK motionResource = Wrap(*motion, MOTION_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK hitDistanceResource = Wrap(*specularHitDistance, HIT_DISTANCE_FORMAT, extent, false);
					NVSDK_NGX_Resource_VK outputResource = Wrap(*output, OUTPUT_FORMAT, outputExtent, true);

					// NGX expects Direct3D style matrices: left-handed view space (+Z forward), row vectors.
					// GLM stores column vectors in column-major order, which is the same memory layout as
					// row vectors in row-major order, so only the handedness needs converting.
					glm::mat4 worldToView = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) * frame.view;
					glm::mat4 viewToClip = glm::perspectiveLH_ZO(frame.verticalFov, frame.aspectRatio, frame.nearPlane, frame.farPlane);

					NVSDK_NGX_VK_DLSSD_Eval_Params eval{};
					eval.pInColor = &colorResource;
					eval.pInOutput = &outputResource;
					eval.pInDiffuseAlbedo = &diffuseAlbedoResource;
					eval.pInSpecularAlbedo = &specularAlbedoResource;
					eval.pInNormals = &normalResource;
					eval.pInRoughness = &normalResource; // Packed into the alpha channel of the normals
					eval.pInDepth = &depthResource;
					eval.pInMotionVectors = &motionResource;
					eval.pInSpecularHitDistance = &hitDistanceResource;
					eval.pInWorldToViewMatrix = glm::value_ptr(worldToView);
					eval.pInViewToClipMatrix = glm::value_ptr(viewToClip);

					// The renderer reports where inside the pixel the rays were shot. DLSS wants the jitter the
					// way a rasterizer applies it, as a shift of the image, which is the opposite direction.
					// (Checked empirically: with the other sign fine texture detail is visibly blurred.)
					eval.InJitterOffsetX = -frame.jitter.x;
					eval.InJitterOffsetY = -frame.jitter.y;
					eval.InMVScaleX = 1.0f; // Motion vectors are already in pixels
					eval.InMVScaleY = 1.0f;
					eval.InRenderSubrectDimensions = { extent.width, extent.height };
					eval.InReset = frame.resetHistory ? 1 : 0;
					eval.InPreExposure = 1.0f;
					eval.InExposureScale = 1.0f;
					eval.InFrameTimeDeltaInMsec = frame.deltaTime * 1000.0f;

					const NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSSD_EXT(commandList.Get(), feature, parameters, &eval);
					if (NVSDK_NGX_FAILED(result)) {
						// Not fatal: the output keeps its previous contents. Report once to avoid flooding the log.
						static bool reported = false;
						if (!reported) {
							std::cerr << "DLSS Ray Reconstruction evaluation failed: " << Describe(result) << "\n";
							reported = true;
						}
					}
				}

			}
		}
	}
}

#endif // FOX_ENABLE_DLSS
