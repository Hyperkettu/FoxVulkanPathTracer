#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>

namespace Fox {

    namespace Graphics
    {

        namespace Vulkan {

            class DescriptorSet {
            public:
                DescriptorSet() = default;

                DescriptorSet(VkDevice device,
                    VkDescriptorPool pool,
                    VkDescriptorSetLayout layout, std::vector<uint32_t> variablesCounts = std::vector<uint32_t>())
                    : device(device), layout(layout)
                {
                    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountAllocInfo{};
                    variableCountAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
                    variableCountAllocInfo.descriptorSetCount = 1; // Allocating 1 set
                    variableCountAllocInfo.pDescriptorCounts = variablesCounts.data(); // Pointer to our counts array

                    VkDescriptorSetAllocateInfo allocInfo{};
                    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    allocInfo.descriptorPool = pool;
                    allocInfo.descriptorSetCount = 1;
                    allocInfo.pSetLayouts = &layout;

                    if (variablesCounts.size() > 0) {
                        allocInfo.pNext = &variableCountAllocInfo;
                    }

                    if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to allocate Vulkan descriptor set!");
                    }
                }

                DescriptorSet(const DescriptorSet&) = delete;
                DescriptorSet& operator=(const DescriptorSet&) = delete;

                DescriptorSet(DescriptorSet&& other) noexcept
                    : device(other.device), layout(other.layout), set(other.set)
                {
                    other.set = VK_NULL_HANDLE;
                }

                DescriptorSet& operator=(DescriptorSet&& other) noexcept {
                    if (this != &other) {
                        device = other.device;
                        layout = other.layout;
                        set = other.set;
                        other.set = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~DescriptorSet() = default; // freed when pool is destroyed

                inline VkDescriptorSet Get() const { return set; }
                VkDescriptorSetLayout GetLayout() const { return layout; }

				void SetShaderResourceTexture(uint32_t slotIndex, const Fox::Graphics::Vulkan::Texture* texture) {
					VkWriteDescriptorSet descriptorWrites{};

					VkDescriptorImageInfo imageInfo{};
					imageInfo.imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageInfo.imageView = texture->GetView();
					imageInfo.sampler = texture->GetSampler();
                    imageInfos.push_back(imageInfo);

					descriptorWrites.sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					descriptorWrites.dstSet = set;
					descriptorWrites.dstBinding = slotIndex;
					descriptorWrites.dstArrayElement = 0;
					descriptorWrites.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					descriptorWrites.descriptorCount = 1;
					descriptorWrites.pImageInfo = &imageInfos[imageInfos.size() -1];

					writes.push_back(descriptorWrites);
				}

				void ClearWrites() {
					writes.clear();
                    bufferInfos.clear();
                    imageInfos.clear();
                    accelInfos.clear();
                    storageImageInfos.clear();
                }

				template<class T>
                void SetConstantBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<T>>& buffer) {
                    VkWriteDescriptorSet descriptorWrites{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo = buffer->DescriptorInfo();
                    bufferInfos.push_back(bufferInfo);

                    descriptorWrites.sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrites.dstSet = set;
                    descriptorWrites.dstBinding = slotIndex;
                    descriptorWrites.dstArrayElement = 0;
                    descriptorWrites.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    descriptorWrites.descriptorCount = 1;
                    descriptorWrites.pBufferInfo = &bufferInfos[bufferInfos.size() - 1];

					writes.push_back(descriptorWrites);
				}

                template<class T>
                void SetDynamicConstantBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::DynamicConstantBuffer<T>>& buffer) {
                    VkWriteDescriptorSet descriptorWrites{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo = buffer->DescriptorInfo();
                    bufferInfos.push_back(bufferInfo);

                    descriptorWrites.sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrites.dstSet = set;
                    descriptorWrites.dstBinding = slotIndex;
                    descriptorWrites.dstArrayElement = 0;
                    descriptorWrites.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    descriptorWrites.descriptorCount = 1;
                    descriptorWrites.pBufferInfo = &bufferInfos[bufferInfos.size() - 1];

                    writes.push_back(descriptorWrites);
                }

                void Reserve(uint32_t maxDescriptors) {
                    writes.reserve(maxDescriptors);
                    bufferInfos.reserve(maxDescriptors);
                    imageInfos.reserve(maxDescriptors);
                    accelInfos.reserve(maxDescriptors);
                    storageImageInfos.reserve(maxDescriptors);
                }

				template<class T>
                void SetDynamicStorageBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::DynamicBuffer<T>>& buffer) {
                    VkWriteDescriptorSet descriptorWrite{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo = buffer->DescriptorInfo();
					bufferInfos.push_back(bufferInfo);

                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = set;
                    descriptorWrite.dstBinding = slotIndex;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.pBufferInfo = &bufferInfos.back();
                    descriptorWrite.pImageInfo = nullptr;
                    descriptorWrite.pTexelBufferView = nullptr;

                    writes.push_back(descriptorWrite);
                }

                void SetAccelerationStructure(uint32_t slotIndex, VkAccelerationStructureKHR& tlas) { 
                    VkWriteDescriptorSetAccelerationStructureKHR asInfo{
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                        .accelerationStructureCount = 1,
                        .pAccelerationStructures = &tlas
                    };

                    accelInfos.push_back(asInfo);

                    VkWriteDescriptorSet asWrite{
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .pNext = &accelInfos.back(),
                        .dstSet = set,
                        .dstBinding = slotIndex,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
                    };

                    writes.push_back(asWrite);
                }

                void SetStorageImage(uint32_t slotIndex, VkImageView outputImageView) {
                    VkDescriptorImageInfo imageInfo{
                        .imageView = outputImageView,
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
                    };

                    storageImageInfos.push_back(imageInfo);

                    VkWriteDescriptorSet imageWrite{
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = set,
                        .dstBinding = slotIndex,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .pImageInfo = &storageImageInfos.back()
                    };

                    writes.push_back(imageWrite); 

                }

                void SetStorageBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::Buffer>& buffer) {
                    VkWriteDescriptorSet descriptorWrite{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = buffer->Get();
                    bufferInfo.offset = 0;
                    bufferInfo.range = buffer->GetSize();
                    bufferInfos.push_back(bufferInfo);

                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = set;
                    descriptorWrite.dstBinding = slotIndex;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.pBufferInfo = &bufferInfos.back();
                    descriptorWrite.pImageInfo = nullptr;
                    descriptorWrite.pTexelBufferView = nullptr;

                    writes.push_back(descriptorWrite);
                }

                template<class T>
                void SetVertexBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer>& buffer) {
                    VkWriteDescriptorSet descriptorWrite{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = buffer->Get();
                    bufferInfo.offset = 0;
                    bufferInfo.range = buffer->GetVertexCount() * sizeof(T);
                    bufferInfos.push_back(bufferInfo);

                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = set;
                    descriptorWrite.dstBinding = slotIndex;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.pBufferInfo = &bufferInfos[bufferInfos.size() - 1];;
                    descriptorWrite.pImageInfo = nullptr;
                    descriptorWrite.pTexelBufferView = nullptr;

                    writes.push_back(descriptorWrite);
                }

                template<class T>
                void SetIndexBuffer(uint32_t slotIndex, std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer>& buffer) {
                    VkWriteDescriptorSet descriptorWrite{};

                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = buffer->Get();
                    bufferInfo.offset = 0;
                    bufferInfo.range = buffer->GetIndexCount() * sizeof(T);
                    bufferInfos.push_back(bufferInfo);

                    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorWrite.dstSet = set;
                    descriptorWrite.dstBinding = slotIndex;
                    descriptorWrite.dstArrayElement = 0;
                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorWrite.descriptorCount = 1;
                    descriptorWrite.pBufferInfo = &bufferInfos[bufferInfos.size() - 1];;
                    descriptorWrite.pImageInfo = nullptr;
                    descriptorWrite.pTexelBufferView = nullptr;

                    writes.push_back(descriptorWrite);
                }

                void SetBindlessTexture(uint32_t slotIndex, uint32_t textureIndexInTheBindlessArray, Fox::Graphics::Vulkan::Texture* texture)
                {
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfo.imageView = texture->GetView();
                    imageInfo.sampler = texture->GetSampler();

                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = set; 
                    write.dstBinding = slotIndex;               
                    write.dstArrayElement = textureIndexInTheBindlessArray;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo = &imageInfo;

                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }

                void Update() {
                    Update(writes);
                }

                void Update(const std::vector<VkWriteDescriptorSet>& writes) const {
                    vkUpdateDescriptorSets(device,
                        static_cast<uint32_t>(writes.size()),
                        writes.data(),
                        0, nullptr);
                }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                VkDescriptorSet set = VK_NULL_HANDLE;

				std::vector<VkWriteDescriptorSet> writes;
                std::vector<VkDescriptorBufferInfo> bufferInfos;
                std::vector<VkDescriptorImageInfo> imageInfos;
                std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelInfos;
                std::vector<VkDescriptorImageInfo> storageImageInfos;
            };

        }
    }
}