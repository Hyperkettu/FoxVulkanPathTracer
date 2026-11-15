#pragma once

namespace Fox {

	namespace Platform {

		class IApplication {

		public:
			IApplication() = default;
			virtual ~IApplication() {}

			// Returns false if the application could not be started; the error is logged.
			virtual bool Initialize() = 0;

			// Runs the main loop until the user quits.
			virtual void Run() = 0;
		};

	}

}
