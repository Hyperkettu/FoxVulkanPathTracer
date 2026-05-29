#pragma once
#include <memory>
#include <mutex>

namespace Fox {
    
    namespace Core {

        template <typename T>
        class Singleton
        {
        public:
            Singleton(const Singleton&) = delete;
            Singleton& operator=(const Singleton&) = delete;

            // Access the single instance (lazy-initialized)
            static T& Get()
            {
                std::call_once(initFlag, []() {
                    instance.reset(new T()); 
                });
                return *instance;
            }

            static void Destroy()
            {
                instance.reset();
            }

        protected:
            Singleton() = default;
            virtual ~Singleton() = default;

        private:
            static std::unique_ptr<T> instance;
            static std::once_flag initFlag;
        };

    }
}