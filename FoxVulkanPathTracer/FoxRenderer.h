#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <array>
#include <iomanip>
#include <functional>

#define SAFE_DELETE(ptr) do { delete (ptr); (ptr) = nullptr; } while(0)

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include<glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Dear ImGui (ThirdParty/imgui): the editor windows
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

#include "STB_image/stb_image.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Util.h"

#include "FoxVulkanPathTracer/Core/Singleton.h"
#include "FoxVulkanPathTracer/Core/Connection.h"
#include "FoxVulkanPathTracer/Core/Signal.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Vertex.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Material.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Light.h"
#include "FoxVulkanPathTracer/Input/InputManager.h"
#include "FoxVulkanPathTracer/Input/InputActions.h"
#include "FoxVulkanPathTracer/Audio/AudioEngine.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Camera.h"
#include "FoxVulkanPathTracer/Core/FileSystem.h"
#include "FoxVulkanPathTracer/Graphics/RendererConfig.h"
#include "FoxVulkanPathTracer/Graphics/RHI.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Buffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/ConstantBuffers.h"


#include "FoxVulkanPathTracer/Graphics/Vulkan/CommandPool.h"

#include "FoxVulkanPathTracer/Graphics/Geometry/Submesh.h"
#include "FoxVulkanPathTracer/Graphics/Geometry/Vulkan/Mesh.h"
#include "FoxVulkanPathTracer/Graphics/Geometry/SkinnedMesh.h"
#include "FoxVulkanPathTracer/Graphics/Geometry/ClusterLod.h"
#include "FoxVulkanPathTracer/Graphics/Geometry/Spline.h"
#include "FoxVulkanPathTracer/Graphics/Geometry/TreeGenerator.h"
#include "FoxVulkanPathTracer/Animation/WindSkeleton.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/CommandList.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/VertexBuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/IndexBuffer.h"


#include "FoxVulkanPathTracer/Graphics/Vulkan/DynamicBuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/ShaderStorageBuffer.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/AccelerationStructure.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/OpacityMicromap.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/ClusterLodPass.h"
#include "FoxVulkanPathTracer/Scene/Raytracing/RaytracingScene.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/RaytracingPipeline.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/RaytracingPipelineBuilder.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/ConstantBuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DynamicConstantBuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Fence.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Semaphore.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Texture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/StorageTexture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/ShaderResourceTexture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/RenderTargetTexture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DepthTexture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/SwapchainTexture.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Framebuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Swapchain.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DescriptorPool.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DescriptorSetLayout.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DescriptorSet.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/DescriptorSetBuilder.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/RenderPass.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/RenderPassBuilder.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/PipelineLayout.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Pipeline.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/PipelineBuilder.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/DeviceContext.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/ComputePass.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/SkinningPass.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/GpuProfiler.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/GBuffer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/Denoiser.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/AccumulationDenoiser.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/SpatiotemporalVarianceGuidedFilter.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/DlssRayReconstruction.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/PostProcess.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/OverlayRenderer.h"

#include "FoxVulkanPathTracer/Graphics/Geometry/GeometryGenerator.h"

#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/DescriptorManager.h"
#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/RenderPassManager.h"
#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/PipelineManager.h"
#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/TextureManager.h"
#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/MeshManager.h"

#include "FoxVulkanPathTracer/Scene/Scene.h"
#include "FoxVulkanPathTracer/Scene/MainScene.h"
#include "FoxVulkanPathTracer/Scene/TextureLibrary.h"
#include "FoxVulkanPathTracer/Text/VectorText.h"
#include "FoxVulkanPathTracer/Scene/CameraManager.h"
#include "FoxVulkanPathTracer/Scene/Terrain.h"
#include "FoxVulkanPathTracer/Scene/SceneContent.h"
#include "FoxVulkanPathTracer/Scene/ECS/Entity.h"
#include "FoxVulkanPathTracer/Scene/ECS/Registry.h"
#include "FoxVulkanPathTracer/Scene/ECS/Components.h"
#include "FoxVulkanPathTracer/Scene/SceneGraph.h"
#include "FoxVulkanPathTracer/Scene/WaterSimulation.h"
#include "FoxVulkanPathTracer/Scene/Physics.h"
#include "FoxVulkanPathTracer/Scene/Navigation.h"
#include "FoxVulkanPathTracer/Scripting/ScriptHost.h"
#include "FoxVulkanPathTracer/Scene/Raytracing/MainRayTracingScene.h"
#include "FoxVulkanPathTracer/UI/GameUI.h"
#include "FoxVulkanPathTracer/Editor/TransformGizmo.h"
#include "FoxVulkanPathTracer/Editor/SceneGraphEditor.h"
#include "FoxVulkanPathTracer/Editor/EditorShell.h"
#include "FoxVulkanPathTracer/Editor/ContentBrowser.h"
#include "FoxVulkanPathTracer/Editor/MeshPainter.h"
#include "FoxVulkanPathTracer/Editor/TreeEditor.h"
#include "FoxVulkanPathTracer/Editor/SceneEditor.h"
#include "FoxVulkanPathTracer/Editor/CameraEditor.h"
#include "FoxVulkanPathTracer/Editor/TerrainEditor.h"

#include "FoxVulkanPathTracer/Graphics/Managers/Vulkan/SceneManager.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/FrameResource.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/VulkanRHI.h"
#include "FoxVulkanPathTracer/Platform/IApplication.h"
#include "FoxVulkanPathTracer/Platform/Vulkan/Application.h"
