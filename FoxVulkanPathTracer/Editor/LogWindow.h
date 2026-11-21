#pragma once

#include <streambuf>

namespace Fox {

	namespace Editor {

		// Window > Log: what the engine prints (std::cout, std::cerr), also in Editor.log and on the console
		namespace Log {

			// Collects what is written to it, line by line (main.cpp writes std::cout and std::cerr to it). Any thread.
			std::streambuf* Capture();

			// The window: the lines with their times, a filter, errors and warnings in colour
			void Draw(bool* open);
		}
	}
}
