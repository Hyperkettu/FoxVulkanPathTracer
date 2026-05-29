#pragma once 

namespace Fox {

	namespace Platform {
	
		class IApplication {

		public: 
			IApplication() = default;
			IApplication(const std::string& title, uint32_t width, uint32_t height) {};

			virtual ~IApplication() {}

			virtual int32_t Initialize() = 0;
			virtual void Run() = 0;

		protected:

		};

	}

}