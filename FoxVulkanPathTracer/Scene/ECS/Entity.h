#pragma once

#include <cstdint>
#include <functional>

namespace Fox {

	namespace ECS {

		// A handle to an entity of a Registry: a slot and the generation of that slot. When an entity is destroyed
		// its slot is reused with the next generation, so old handles to it stop being valid (Registry::IsValid).
		struct Entity {
			static constexpr uint32_t NULL_INDEX = UINT32_MAX;

			uint32_t index = NULL_INDEX;
			uint32_t generation = 0;

			bool IsNull() const { return index == NULL_INDEX; }
			explicit operator bool() const { return !IsNull(); }

			bool operator==(const Entity& other) const { return index == other.index && generation == other.generation; }
			bool operator!=(const Entity& other) const { return !(*this == other); }
			bool operator<(const Entity& other) const { return index != other.index ? index < other.index : generation < other.generation; }
		};
	}
}

template<> struct std::hash<Fox::ECS::Entity> {
	size_t operator()(const Fox::ECS::Entity& entity) const noexcept {
		return std::hash<uint64_t>()((static_cast<uint64_t>(entity.generation) << 32) | entity.index);
	}
};
