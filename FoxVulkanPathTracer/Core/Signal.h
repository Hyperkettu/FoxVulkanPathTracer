#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <utility>

namespace Fox {

    namespace Core {

        template<typename... PrefixArgs>
        class Signal
        {
        public:
            using SlotType = std::function<void(PrefixArgs...)>;

            Connection Connect(SlotType&& slot)
            {
                const auto id = ++nextId;
                slots.emplace_back(id, std::move(slot));
                return Connection([this, id]() { Disconnect(id); });
            }

            template<typename T>
            Connection ConnectMember(T* instance, void (T::* func)(PrefixArgs...))
            {
                return Connect([instance, func](PrefixArgs... args) {
                    (instance->*func)(std::forward<PrefixArgs>(args)...);
                    });
            }

            template<typename... Args>
            void Emit(Args&&... args)
            {
                static_assert(sizeof...(Args) == sizeof...(PrefixArgs),
                    "Signal::Emit argument count mismatch");
                for (auto& [id, slot] : slots)
                    if (slot)
                        slot(std::forward<Args>(args)...);
            }

            template<typename... Args>
            void operator()(Args&&... args) { Emit(std::forward<Args>(args)...); }

            void Clear() { slots.clear(); }

        private:
            void Disconnect(size_t id)
            {
                slots.erase(std::remove_if(slots.begin(), slots.end(),
                    [id](auto& p) { return p.first == id; }), slots.end());
            }

            std::vector<std::pair<size_t, SlotType>> slots;
            size_t nextId = 0;
        };
    }
}

