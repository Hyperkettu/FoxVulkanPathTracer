#pragma once

#include <SDL2/SDL.h>
#include <unordered_map>

namespace Fox {
    
    namespace Input {
        
            class InputManager
            {
            public:
                Fox::Core::Signal<SDL_Scancode, bool> OnKeyChanged;   // (key, pressed)
                Fox::Core::Signal<SDL_Scancode> OnKeyPressed;
                Fox::Core::Signal<SDL_Scancode> OnKeyReleased;

                Fox::Core::Signal<int, int> OnMouseMoved;             // (deltaX, deltaY)
                Fox::Core::Signal<int, int> OnMouseScrolled;          // (scrollX, scrollY)
                Fox::Core::Signal<Uint8, bool> OnMouseButtonChanged;  // (button, pressed)
                Fox::Core::Signal<Uint8> OnMouseButtonPressed;
                Fox::Core::Signal<Uint8> OnMouseButtonReleased;
                Fox::Core::Signal<bool> OnRelativeModeChanged;        // (enabled/disabled)

                void ProcessEvent(const SDL_Event& e);

                // -------------------------------------------------------------
                // 🔹 Real-time queries
                // -------------------------------------------------------------
                bool IsKeyDown(SDL_Scancode scancode) const
                {
                    auto it = m_keyStates.find(scancode);
                    return it != m_keyStates.end() && it->second;
                }

                bool IsMouseDown(Uint8 button) const
                {
                    auto it = m_mouseButtonStates.find(button);
                    return it != m_mouseButtonStates.end() && it->second;
                }

                int GetMouseX() const { return m_mouseX; }
                int GetMouseY() const { return m_mouseY; }
                int GetDeltaX() const { return m_deltaX; }
                int GetDeltaY() const { return m_deltaY; }

                void ResetDeltas() { m_deltaX = m_deltaY = 0; }

                // -------------------------------------------------------------
                // 🔹 Relative mode control
                // -------------------------------------------------------------
                void SetRelativeMode(bool enabled)
                {
                    if (enabled != m_relativeMode)
                    {
                        SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
                        m_relativeMode = enabled;
                        OnRelativeModeChanged.Emit(enabled);
                    }
                }

                bool IsRelativeMode() const { return m_relativeMode; }

                void ToggleRelativeMode()
                {
                    SetRelativeMode(!m_relativeMode);
                }

            private:
                std::unordered_map<SDL_Scancode, bool> m_keyStates;
                std::unordered_map<Uint8, bool> m_mouseButtonStates;
                int m_mouseX = 0, m_mouseY = 0;
                int m_deltaX = 0, m_deltaY = 0;
                bool m_relativeMode = false;
            };
     }
}