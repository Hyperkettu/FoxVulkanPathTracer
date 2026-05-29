#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class DescriptorSetBuilder {
            public:

                explicit DescriptorSetBuilder(VkDevice device)
                    : device(device)
                {}

                // Add a binding to the layout
                DescriptorSetBuilder& AddBinding(uint32_t binding,
                                                 VkDescriptorType type,
                                                 VkShaderStageFlags stages,
                                                 uint32_t count = 1,
                                                 const VkSampler* immutableSamplers = nullptr)
                {
                    VkDescriptorSetLayoutBinding layoutBinding{};
                    layoutBinding.binding = binding;
                    layoutBinding.descriptorType = type;
                    layoutBinding.descriptorCount = count;
                    layoutBinding.stageFlags = stages;
                    layoutBinding.pImmutableSamplers = immutableSamplers;
                    bindings.push_back(layoutBinding);
                    return *this;
                }

                // Set number of descriptor sets (for pool creation)
                DescriptorSetBuilder& SetMaxSets(uint32_t maxSets) {
                    this->maxSets = maxSets;
                    return *this;
                }

                // Optional: set descriptor pool create flags
                DescriptorSetBuilder& SetPoolFlags(VkDescriptorPoolCreateFlags flags) {
                    poolFlags = flags;
                    return *this;
                }

                // Build both layout and pool
                void Build() {
                    if (bindings.empty()) {
                        throw std::runtime_error("DescriptorSetBuilder: No bindings added before build()");
                    }

                    layout = std::make_unique<DescriptorSetLayout>(device, bindings);

                    std::vector<VkDescriptorPoolSize> poolSizes;
                    poolSizes.reserve(bindings.size());
                    for (const auto& b : bindings) {
                        bool found = false;
                        for (auto& ps : poolSizes) {
                            if (ps.type == b.descriptorType) {
                                ps.descriptorCount += b.descriptorCount * maxSets;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            poolSizes.push_back({ b.descriptorType, b.descriptorCount * maxSets });
                        }
                    }

                    pool = std::make_unique<DescriptorPool>(device, poolSizes, maxSets, poolFlags);
                }

                // Allocate a single descriptor set
                DescriptorSet AllocateSet() const {
                    if (!layout || !pool)
                        throw std::runtime_error("DescriptorSetBuilder: build() must be called before allocateSet()");
                    return DescriptorSet(device, pool->Get(), layout->Get());
                }

                // Allocate multiple descriptor sets
                std::vector<DescriptorSet> AllocateSets(uint32_t count) const {
                    if (!layout || !pool)
                        throw std::runtime_error("DescriptorSetBuilder: build() must be called before allocateSets()");
                    std::vector<DescriptorSet> sets;
                    sets.reserve(count);
                    for (uint32_t i = 0; i < count; ++i)
                        sets.emplace_back(device, pool->Get(), layout->Get());
                    return sets;
                }

                const DescriptorSetLayout& GetLayout() const {
                    if (!layout)
                        throw std::runtime_error("DescriptorSetBuilder: layout not built yet");
                    return *layout;
                }

                const DescriptorPool& GetPool() const {
                    if (!pool)
                        throw std::runtime_error("DescriptorSetBuilder: pool not built yet");
                    return *pool;
                }

            private:
                VkDevice device = VK_NULL_HANDLE;
                std::vector<VkDescriptorSetLayoutBinding> bindings;

                uint32_t maxSets = 1;
                VkDescriptorPoolCreateFlags poolFlags = 0;

                std::unique_ptr<DescriptorSetLayout> layout;
                std::unique_ptr<DescriptorPool> pool;
            };

        }
    }
}