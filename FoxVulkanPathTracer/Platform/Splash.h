#pragma once

#include <string>

namespace Fox {

	namespace Platform {

		// The splash screen shown while the engine loads: the logo, what is being loaded, and a moving bar. A window of
		// its own on a thread of its own, so it keeps drawing while the main thread is busy loading (the main window
		// stays hidden until its first frame is ready). Windows only; elsewhere these do nothing.
		namespace Splash {

			// Opens it with this image (a file path, relative to the working directory). Does nothing if it is open.
			void Show(const std::string& imageFile);

			// The line under the logo ("Loading Models/cabin.glb"). Any thread.
			void SetStatus(const std::string& status);

			// Fades it out and closes it (waits until it is gone). Safe to call when it is not open.
			void Close();
		}
	}
}
