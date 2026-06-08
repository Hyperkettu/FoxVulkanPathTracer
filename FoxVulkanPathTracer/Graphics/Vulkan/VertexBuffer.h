#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "Buffer.h" // your existing generic buffer class

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class VertexBuffer
            {
            public:
                VertexBuffer() = default;
                ~VertexBuffer() = default;

                VertexBuffer(const VertexBuffer&) = delete;
                VertexBuffer& operator=(const VertexBuffer&) = delete;

                VertexBuffer(VertexBuffer&& other) noexcept
                {
                    Move(std::move(other));
                }

                VertexBuffer& operator=(VertexBuffer&& other) noexcept
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
                    const std::vector<VertexType>& vertices,
                    const std::string& name = "")
                {
                    this->device = device;
                    vertexCount = static_cast<uint32_t>(vertices.size());
                    VkDeviceSize bufferSize = sizeof(VertexType) * vertices.size();
                    
                    std::unique_ptr<Fox::Graphics::Vulkan::Buffer> stagingBuffer =
                        std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                            device, physicalDevice, bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                    void* stagingData = stagingBuffer->Map();
                    memcpy(stagingData, vertices.data(), vertexCount * sizeof(VertexType)); 
                    stagingBuffer->Unmap();


                    buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>( 
                            device, physicalDevice, bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                        | VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                            VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 

                    CopyBuffer(device, commandPool, transferQueue, stagingBuffer->Get(), buffer->Get(), bufferSize);

#ifdef _DEBUG
                    if (name.size() > 0) {
                        Fox::Graphics::Vulkan::CommandList::SetName(name, reinterpret_cast<uint64_t>(buffer->Get()), VkObjectType::VK_OBJECT_TYPE_BUFFER, device);
                        Fox::Graphics::Vulkan::CommandList::PrintBufferNameAndAddress(device, buffer->Get(), name);
                    }
#endif
                }

               /*
                void Bind(VkCommandBuffer cmd)
                {
                    VkBuffer vertexBuffers[] = { buffer->Get() };
                    VkDeviceSize offsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                } */

                uint32_t GetVertexCount() const { return vertexCount; }

                inline VkBuffer Get() const {
                    return buffer->Get();
                }

                void Destroy()
                {
                    if (buffer) {
                        buffer = nullptr;
                    }
                    vertexCount = 0;
                }

            private:
                void Move(VertexBuffer&& other)
                {
                    device = other.device;
                    vertexCount = other.vertexCount;
                    buffer = std::move(other.buffer);

                    other.device = VK_NULL_HANDLE;
                    other.vertexCount = 0;
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
                uint32_t vertexCount = 0;
            };
        }
    }
}