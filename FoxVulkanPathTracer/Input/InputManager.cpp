#include "FoxRenderer.h"

namespace Fox {

	namespace Input {
	
        void InputManager::ProcessEvent(const SDL_Event& e)
        {
            switch (e.type)
            {
                // --- Keyboard ----------------------------------------
            case SDL_KEYDOWN:
            {
                const auto key = e.key.keysym.scancode;
                if (!e.key.repeat && !m_keyStates[key]) {
                    m_keyStates[key] = true;
                    OnKeyChanged(key, true);
                    OnKeyPressed(key);
                }
                break;
            }
            case SDL_KEYUP:
            {
                const auto key = e.key.keysym.scancode;
                if (m_keyStates[key]) {
                    m_keyStates[key] = false;
                    OnKeyChanged(key, false);
                    OnKeyReleased(key);
                }
                break;
            }

            // --- Mouse -------------------------------------------
            case SDL_MOUSEMOTION:
            {
                int dx = e.motion.xrel;
                int dy = e.motion.yrel;

                // Update the state first, so listeners see the new position
                if (!m_relativeMode)
                {
                    m_mouseX = e.motion.x;
                    m_mouseY = e.motion.y;
                } else
                {
                    m_deltaX += dx;
                    m_deltaY += dy;
                }

                OnMouseMoved(dx, dy);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            {
                Uint8 button = e.button.button;
                if (!m_relativeMode) {
                    // Where the click happened, even without a motion event before it
                    m_mouseX = e.button.x;
                    m_mouseY = e.button.y;
                }
                if (!m_mouseButtonStates[button]) {
                    m_mouseButtonStates[button] = true;
                    OnMouseButtonChanged(button, true);
                    OnMouseButtonPressed(button);
                }
                break;
            }
            case SDL_MOUSEBUTTONUP:
            {
                Uint8 button = e.button.button;
                if (m_mouseButtonStates[button]) {
                    m_mouseButtonStates[button] = false;
                    OnMouseButtonChanged(button, false);
                    OnMouseButtonReleased(button);
                }
                break;
            }
            case SDL_MOUSEWHEEL:
            {
                OnMouseScrolled(e.wheel.x, e.wheel.y);
                break;
            }

            default: break;
            }
        }
	
	}
}