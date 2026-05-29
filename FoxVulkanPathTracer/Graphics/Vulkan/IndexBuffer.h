#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "Buffer.h" // your existing generic buffer class

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class IndexBuffer
            {
            public:
                IndexBuffer() = default;
                ~IndexBuffer() = default;

                IndexBuffer(const IndexBuffer&) = delete;
                IndexBuffer& operator=(const IndexBuffer&) = delete;

                IndexBuffer(IndexBuffer&& other) noexcept
                {
                    Move(std::move(other));
                }

                IndexBuffer& operator=(IndexBuffer&& other) noexcept
                {
                    if (this != &other)
                        Move(std::move(other));
                    return *this;
                }

                template<typename VertexType>
                void Create(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool,
                    VkQueue transferQueue,
                    const std::vector<VertexType>& vertices)
                {
                    this->device = device;
                    indexCount = static_cast<uint32_t>(vertices.size());
                    VkDeviceSize bufferSize = sizeof(VertexType) * vertices.size();

                    std::unique_ptr<Fox::Graphics::Vulkan::Buffer> stagingBuffer =
                        std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                            device, physicalDevice, bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                    void* stagingData = stagingBuffer->Map();
                    memcpy(stagingData, vertices.data(), indexCount * sizeof(VertexType));
                    stagingBuffer->Unmap();


                    buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                        device, physicalDevice, bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                        VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                    CopyBuffer(device, commandPool, transferQueue, stagingBuffer->Get(), buffer->Get(), bufferSize);

                }

                inline VkBuffer Get() const {
                    return buffer->Get();
                }

                /*
                 void Bind(VkCommandBuffer cmd)
                 {
                     VkBuffer IndexBuffers[] = { buffer->Get() };
                     VkDeviceSize offsets[] = { 0 };
                     vkCmdBindIndexBuffers(cmd, 0, 1, IndexBuffers, offsets);
                 } */

                uint32_t GetIndexCount() const { return indexCount; }

                void Destroy()
                {
                    if (buffer) {
                        buffer = nullptr;
                    }
                    indexCount = 0;
                }

            private:
                void Move(IndexBuffer&& other)
                {
                    device = other.device;
                    indexCount = other.indexCount;
                    buffer = std::move(other.buffer);

                    other.device = VK_NULL_HANDLE;
                    other.indexCount = 0;
                }

                static void CopyBuffer(VkDevice device,
                    VkCommandPool commandPool,
                    VkQueue queue,
                    VkBuffer src,
                    VkBuffer dst,
                    VkDeviceSize size) {

                    std::unique_ptr<Fox::Graphics::Vulkan::CommandList> cmdList = std::make_unique<Fox::Graphics::Vulkan::CommandList>(device, commandPool);

                    VkBufferCopy copyRegion{};
                    copyRegion.size = size;

                    cmdList->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                        .CopyBuffer(src, dst, { copyRegion })
                        .End()
                        .SubmitAndWait(queue);
                }

            private:
                VkDevice device = VK_NULL_HANDLE;
                std::unique_ptr<Fox::Graphics::Vulkan::Buffer> buffer;
                uint32_t indexCount = 0;
            };
        }
    }
}