#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <utility>

namespace Fox { 

    namespace Core {
    
        class Connection {
        public:
            Connection() = default;
            explicit Connection(std::function<void()> disconnect)
                : disconnect(std::move(disconnect)) {
            }

            ~Connection() { Disconnect(); }

            void Disconnect() {
                if (disconnect) {
                    disconnect();
                    disconnect = nullptr;
                }
            }

            Connection(Connection&& o) noexcept : disconnect(std::move(o.disconnect)) { o.disconnect = nullptr; }
            Connection& operator=(Connection&& o) noexcept {
                if (this != &o) {
                    Disconnect();
                    disconnect = std::move(o.disconnect);
                    o.disconnect = nullptr;
                }
                return *this;
            }

            Connection(const Connection&) = delete;
            Connection& operator=(const Connection&) = delete;

        private:
            std::function<void()> disconnect;
        };
    
    }
}

