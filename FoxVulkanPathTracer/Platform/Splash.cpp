#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Platform/Splash.h"

#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace Fox {

	namespace Platform {

		namespace Splash {

#ifdef _WIN32
			namespace {
				constexpr UINT WM_SPLASH_CLOSE = WM_APP + 1;
				constexpr UINT_PTR TIMER_ID = 1;
				constexpr int FOOTER = 58;           // Pixels under the logo: the status line and the bar
				constexpr COLORREF BACKGROUND = RGB(4, 8, 20);
				constexpr COLORREF TEXT = RGB(170, 182, 205);
				constexpr COLORREF TRACK = RGB(22, 32, 56);
				constexpr COLORREF BAR = RGB(58, 141, 255); // The logo's blue glow

				struct State {
					std::thread thread;
					std::mutex mutex;
					std::string status = "Starting";
					std::atomic<HWND> window{ nullptr };
					std::atomic<bool> open{ false };

					// The logo, scaled to the window once (BGRA, top-down)
					std::vector<uint8_t> pixels;
					int imageWidth = 0, imageHeight = 0;
					std::vector<uint8_t> source;
					int sourceWidth = 0, sourceHeight = 0;

					DWORD started = 0;
					int alpha = 0;           // Fading in, then out
					bool closing = false;
					HFONT font = nullptr;

					// Still open at exit (start-up failed before Close): closed with the program
					~State() {
						if (thread.joinable()) {
							if (HWND hwnd = window) PostMessageA(hwnd, WM_SPLASH_CLOSE, 0, 0);
							thread.join();
						}
					}
				};

				State& Get() {
					static State state;
					return state;
				}

				// Box filter downscale (the logo is larger than the splash): smooth, no aliasing
				void ScaleLogo(State& s, int width, int height) {
					s.imageWidth = width;
					s.imageHeight = height;
					s.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
					for (int y = 0; y < height; y++) {
						const int sy0 = y * s.sourceHeight / height, sy1 = std::max(sy0 + 1, (y + 1) * s.sourceHeight / height);
						for (int x = 0; x < width; x++) {
							const int sx0 = x * s.sourceWidth / width, sx1 = std::max(sx0 + 1, (x + 1) * s.sourceWidth / width);
							uint32_t sum[3] = {}, count = 0;
							for (int sy = sy0; sy < sy1; sy++) {
								for (int sx = sx0; sx < sx1; sx++) {
									const uint8_t* p = &s.source[(static_cast<size_t>(sy) * s.sourceWidth + sx) * 4];
									sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
									count++;
								}
							}
							uint8_t* d = &s.pixels[(static_cast<size_t>(y) * width + x) * 4];
							d[0] = static_cast<uint8_t>(sum[2] / count); // BGR
							d[1] = static_cast<uint8_t>(sum[1] / count);
							d[2] = static_cast<uint8_t>(sum[0] / count);
							d[3] = 255;
						}
					}
				}

				void Paint(HWND hwnd, State& s) {
					PAINTSTRUCT ps;
					HDC target = BeginPaint(hwnd, &ps);
					RECT client;
					GetClientRect(hwnd, &client);
					const int width = client.right, height = client.bottom;

					// Drawn off screen, then shown at once (no flicker)
					HDC dc = CreateCompatibleDC(target);
					HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
					HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

					HBRUSH background = CreateSolidBrush(BACKGROUND);
					FillRect(dc, &client, background);
					DeleteObject(background);

					if (!s.pixels.empty()) {
						BITMAPINFO info{};
						info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
						info.bmiHeader.biWidth = s.imageWidth;
						info.bmiHeader.biHeight = -s.imageHeight; // Top-down
						info.bmiHeader.biPlanes = 1;
						info.bmiHeader.biBitCount = 32;
						info.bmiHeader.biCompression = BI_RGB;
						SetDIBitsToDevice(dc, 0, 0, s.imageWidth, s.imageHeight, 0, 0, 0, s.imageHeight, s.pixels.data(), &info, DIB_RGB_COLORS);
					}

					// What is loading
					std::string status;
					{
						std::lock_guard<std::mutex> lock(s.mutex);
						status = s.status;
					}
					const int margin = 24;
					SetBkMode(dc, TRANSPARENT);
					SetTextColor(dc, TEXT);
					HGDIOBJ oldFont = SelectObject(dc, s.font);
					RECT line{ margin, s.imageHeight + 8, width - margin, s.imageHeight + 32 };
					DrawTextA(dc, status.c_str(), static_cast<int>(status.size()), &line, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
					SelectObject(dc, oldFont);

					// A bar sweeping along its track (how long loading takes is not known ahead)
					const RECT track{ margin, height - 18, width - margin, height - 14 };
					HBRUSH trackBrush = CreateSolidBrush(TRACK);
					FillRect(dc, &track, trackBrush);
					DeleteObject(trackBrush);
					const int trackWidth = track.right - track.left, segment = trackWidth / 4;
					const float phase = static_cast<float>((GetTickCount() - s.started) % 1600) / 1600.0f;
					const float eased = phase * phase * (3.0f - 2.0f * phase); // Slow at the ends
					const int start = track.left - segment + static_cast<int>(static_cast<float>(trackWidth + segment) * eased);
					RECT bar{ std::max(start, static_cast<int>(track.left)), track.top, std::min(start + segment, static_cast<int>(track.right)), track.bottom };
					if (bar.right > bar.left) {
						HBRUSH barBrush = CreateSolidBrush(BAR);
						FillRect(dc, &bar, barBrush);
						DeleteObject(barBrush);
					}

					BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
					SelectObject(dc, oldBitmap);
					DeleteObject(bitmap);
					DeleteDC(dc);
					EndPaint(hwnd, &ps);
				}

				LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
					State& s = Get();
					switch (message) {
					case WM_TIMER: {
						// Fading in at the start, out when closing
						if (s.closing) {
							s.alpha = std::max(0, s.alpha - 40);
							if (s.alpha == 0) {
								DestroyWindow(hwnd);
								return 0;
							}
						}
						else if (s.alpha < 255) {
							s.alpha = std::min(255, s.alpha + 32);
						}
						SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(s.alpha), LWA_ALPHA);
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					case WM_SPLASH_CLOSE:
						s.closing = true;
						return 0;
					case WM_PAINT:
						Paint(hwnd, s);
						return 0;
					case WM_ERASEBKGND:
						return 1;
					case WM_NCHITTEST:
						return HTCAPTION; // It can be dragged aside
					case WM_DESTROY:
						KillTimer(hwnd, TIMER_ID);
						PostQuitMessage(0);
						return 0;
					default:
						return DefWindowProcA(hwnd, message, wParam, lParam);
					}
				}

				void Run(State& s) {
					HINSTANCE instance = GetModuleHandleA(nullptr);
					WNDCLASSA windowClass{};
					windowClass.lpfnWndProc = WindowProc;
					windowClass.hInstance = instance;
					windowClass.hCursor = LoadCursor(nullptr, IDC_APPSTARTING);
					windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
					windowClass.lpszClassName = "FoxEngineSplash";
					RegisterClassA(&windowClass);

					// About a third of the screen's width, the logo's proportions, the status and the bar under it
					const int screenWidth = GetSystemMetrics(SM_CXSCREEN), screenHeight = GetSystemMetrics(SM_CYSCREEN);
					int width = std::clamp(screenWidth * 2 / 5, 480, 900);
					int imageHeight = s.sourceWidth > 0 ? width * s.sourceHeight / s.sourceWidth : width * 2 / 3;
					if (imageHeight + FOOTER > screenHeight * 4 / 5) {
						imageHeight = screenHeight * 4 / 5 - FOOTER;
						width = s.sourceHeight > 0 ? imageHeight * s.sourceWidth / s.sourceHeight : width;
					}
					if (!s.source.empty()) {
						ScaleLogo(s, width, imageHeight);
					}
					else {
						s.imageWidth = width;
						s.imageHeight = imageHeight;
					}
					s.source.clear();
					s.source.shrink_to_fit();
					const int height = imageHeight + FOOTER;

					s.font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
						CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
					s.started = GetTickCount();
					s.alpha = 0;
					s.closing = false;

					HWND hwnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_APPWINDOW, windowClass.lpszClassName, "Fox Engine",
						WS_POPUP, (screenWidth - width) / 2, (screenHeight - height) / 2, width, height, nullptr, nullptr, instance, nullptr);
					if (!hwnd) {
						DeleteObject(s.font);
						s.font = nullptr;
						return;
					}
					// Rounded corners on Windows 11 (ignored elsewhere)
					const DWORD round = 2; // DWMWCP_ROUND
					DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
					SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
					ShowWindow(hwnd, SW_SHOWNOACTIVATE);
					SetTimer(hwnd, TIMER_ID, 16, nullptr);
					s.window = hwnd;

					MSG message;
					while (GetMessageA(&message, nullptr, 0, 0) > 0) {
						TranslateMessage(&message);
						DispatchMessageA(&message);
					}
					s.window = nullptr;
					DeleteObject(s.font);
					s.font = nullptr;
					UnregisterClassA(windowClass.lpszClassName, instance);
				}
			}

			void Show(const std::string& imageFile) {
				State& s = Get();
				if (s.open) {
					return;
				}
				int channels = 0;
				stbi_uc* data = stbi_load(imageFile.c_str(), &s.sourceWidth, &s.sourceHeight, &channels, 4);
				if (data) {
					s.source.assign(data, data + static_cast<size_t>(s.sourceWidth) * s.sourceHeight * 4);
					stbi_image_free(data);
				}
				else {
					std::cout << "Splash: cannot read " << imageFile << "\n";
					s.sourceWidth = s.sourceHeight = 0;
				}
				s.open = true;
				s.thread = std::thread([&s] { Run(s); });
			}

			void SetStatus(const std::string& status) {
				State& s = Get();
				if (!s.open) {
					return;
				}
				std::lock_guard<std::mutex> lock(s.mutex);
				s.status = status;
			}

			void Close() {
				State& s = Get();
				if (!s.open) {
					return;
				}
				// The window may not be there yet (its thread just started): wait for it, or for the thread to give up
				for (int i = 0; i < 200 && !s.window && s.thread.joinable(); i++) {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				if (HWND hwnd = s.window) {
					PostMessageA(hwnd, WM_SPLASH_CLOSE, 0, 0);
				}
				if (s.thread.joinable()) {
					s.thread.join();
				}
				s.open = false;
			}
#else
			void Show(const std::string&) {}
			void SetStatus(const std::string&) {}
			void Close() {}
#endif
		}
	}
}
