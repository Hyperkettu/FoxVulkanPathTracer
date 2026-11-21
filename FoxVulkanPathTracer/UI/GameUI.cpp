#include "FoxRenderer.h"

#include <unordered_map>
#include <cstring>
#include <future>
#include <thread>

namespace Fox {

	namespace Scene {

		// A screen text's filled outlines, kept until the text or its look changes
		struct UITextCache {
			std::string key;
			Fox::Text::Fill fill;
			glm::vec2 min{ 0.0f }, max{ 0.0f };
		};
	}

	namespace UI {

		using Fox::Scene::TextComponent;
		using Fox::Scene::SpriteComponent;
		using Fox::Scene::UIAnimationComponent;
		using Fox::Scene::UILayout;
		using Easing = UIAnimationComponent::Easing;

		// ------------------------------------------------------------------------------------------------
		// Easings (after easings.net)
		// ------------------------------------------------------------------------------------------------

		float Ease(Easing easing, float t) {
			t = glm::clamp(t, 0.0f, 1.0f);
			const float pi = glm::pi<float>();
			auto bounceOut = [](float x) {
				const float n = 7.5625f, d = 2.75f;
				if (x < 1.0f / d) return n * x * x;
				if (x < 2.0f / d) { x -= 1.5f / d; return n * x * x + 0.75f; }
				if (x < 2.5f / d) { x -= 2.25f / d; return n * x * x + 0.9375f; }
				x -= 2.625f / d;
				return n * x * x + 0.984375f;
			};
			const float back = 1.70158f, backInOut = back * 1.525f;
			switch (easing) {
			case Easing::Linear: return t;
			case Easing::SineIn: return 1.0f - std::cos(t * pi * 0.5f);
			case Easing::SineOut: return std::sin(t * pi * 0.5f);
			case Easing::SineInOut: return -(std::cos(pi * t) - 1.0f) * 0.5f;
			case Easing::QuadIn: return t * t;
			case Easing::QuadOut: return 1.0f - (1.0f - t) * (1.0f - t);
			case Easing::QuadInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
			case Easing::CubicIn: return t * t * t;
			case Easing::CubicOut: return 1.0f - std::pow(1.0f - t, 3.0f);
			case Easing::CubicInOut: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
			case Easing::ExpoIn: return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
			case Easing::ExpoOut: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
			case Easing::ExpoInOut:
				if (t <= 0.0f || t >= 1.0f) return t;
				return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
			case Easing::BackIn: return (back + 1.0f) * t * t * t - back * t * t;
			case Easing::BackOut: { const float u = t - 1.0f; return 1.0f + (back + 1.0f) * u * u * u + back * u * u; }
			case Easing::BackInOut:
				return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((backInOut + 1.0f) * 2.0f * t - backInOut)) * 0.5f
					: (std::pow(2.0f * t - 2.0f, 2.0f) * ((backInOut + 1.0f) * (t * 2.0f - 2.0f) + backInOut) + 2.0f) * 0.5f;
			case Easing::ElasticIn:
				if (t <= 0.0f || t >= 1.0f) return t;
				return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * (2.0f * pi / 3.0f));
			case Easing::ElasticOut:
				if (t <= 0.0f || t >= 1.0f) return t;
				return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * pi / 3.0f)) + 1.0f;
			case Easing::ElasticInOut:
				if (t <= 0.0f || t >= 1.0f) return t;
				return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * pi / 4.5f))) * 0.5f
					: (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * (2.0f * pi / 4.5f))) * 0.5f + 1.0f;
			case Easing::BounceIn: return 1.0f - bounceOut(1.0f - t);
			case Easing::BounceOut: return bounceOut(t);
			case Easing::BounceInOut: return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) * 0.5f : (1.0f + bounceOut(2.0f * t - 1.0f)) * 0.5f;
			}
			return t;
		}

		namespace {
			bool pointerOverButton = false;
			bool buttonsShown = false;
		}

		bool ButtonsShown() {
			return buttonsShown;
		}

		bool PointerOverButton() {
			return pointerOverButton;
		}

		const char* EasingName(Easing easing) {
			static const char* const names[] = { "Linear", "Sine in", "Sine out", "Sine in-out", "Quad in", "Quad out", "Quad in-out",
				"Cubic in", "Cubic out", "Cubic in-out", "Expo in", "Expo out", "Expo in-out", "Back in", "Back out", "Back in-out",
				"Elastic in", "Elastic out", "Elastic in-out", "Bounce in", "Bounce out", "Bounce in-out" };
			const int index = static_cast<int>(easing);
			return index >= 0 && index < IM_ARRAYSIZE(names) ? names[index] : "?";
		}

		namespace {
			using Entity = Fox::Scene::RayTracing::MainRayTracingScene::Entity;
			uint64_t EntityKey(const Entity& e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; }

			// ------------------------------------------------------------------------------------------------
			// Animation
			// ------------------------------------------------------------------------------------------------

			// What an animation does to its element now
			struct Animated {
				glm::vec2 position{ 0.0f };
				glm::vec2 scale{ 1.0f };
				float rotation = 0.0f;
				float alpha = 1.0f;
				glm::vec3 tint{ 1.0f };
			};

			glm::vec4 Neutral(UIAnimationComponent::Property property) {
				using P = UIAnimationComponent::Property;
				return property == P::Scale || property == P::Alpha || property == P::Tint ? glm::vec4(1.0f) : glm::vec4(0.0f);
			}

			// The steps' start and end times (in order; one "with the previous" starts with the step before it)
			float Schedule(const UIAnimationComponent& animation, std::vector<glm::vec2>& times) {
				times.resize(animation.steps.size());
				float sequenceEnd = 0.0f, previousStart = 0.0f;
				for (size_t i = 0; i < animation.steps.size(); i++) {
					const auto& step = animation.steps[i];
					const float start = (step.withPrevious && i > 0 ? previousStart : sequenceEnd) + std::max(step.delay, 0.0f);
					const float end = start + std::max(step.duration, 0.0f);
					times[i] = glm::vec2(start, end);
					previousStart = start - std::max(step.delay, 0.0f);
					sequenceEnd = std::max(sequenceEnd, end);
				}
				return sequenceEnd;
			}

			Animated Evaluate(const UIAnimationComponent& animation) {
				Animated result;
				if (!animation.enabled || animation.steps.empty()) return result;
				std::vector<glm::vec2> times;
				const float total = Schedule(animation, times);
				float t = animation.time;
				if (total <= 0.0f) t = 0.0f;
				else if (animation.repeat == UIAnimationComponent::Repeat::Loop) t = std::fmod(t, total);
				else if (animation.repeat == UIAnimationComponent::Repeat::PingPong) { t = std::fmod(t, 2.0f * total); if (t > total) t = 2.0f * total - t; }
				else t = std::min(t, total);

				// Each property starts neutral; each step moves it from where the steps before ended (or its own from)
				glm::vec4 now[5], ended[5];
				for (int p = 0; p < 5; p++) now[p] = ended[p] = Neutral(static_cast<UIAnimationComponent::Property>(p));
				for (size_t i = 0; i < animation.steps.size(); i++) {
					const auto& step = animation.steps[i];
					const int p = std::clamp(static_cast<int>(step.property), 0, 4);
					const glm::vec4 from = step.useFrom ? step.from : ended[p];
					if (t >= times[i].y) now[p] = step.to;
					else if (t > times[i].x) now[p] = glm::mix(from, step.to, Ease(step.easing, (t - times[i].x) / std::max(times[i].y - times[i].x, 1e-6f)));
					else if (step.useFrom && i == 0) now[p] = from; // Waiting at its start
					ended[p] = step.to;
				}
				result.position = glm::vec2(now[0]);
				result.scale = glm::vec2(now[1]);
				result.rotation = now[2].x;
				result.alpha = now[3].x;
				result.tint = glm::vec3(now[4]);
				return result;
			}

			// ------------------------------------------------------------------------------------------------
			// Layout
			// ------------------------------------------------------------------------------------------------

			// An element placed: its transform to the screen (pixels, y down), its rectangle in its own space, what it
			// passes on to its children
			struct Placed {
				glm::mat3 matrix{ 1.0f };
				glm::vec2 rectMin{ 0.0f }, rectSize{ 0.0f };
				float alpha = 1.0f;
				glm::vec3 tint{ 1.0f };
			};

			glm::mat3 Translate(const glm::vec2& t) { glm::mat3 m(1.0f); m[2] = glm::vec3(t, 1.0f); return m; }
			glm::mat3 Rotate(float degrees) {
				const float a = glm::radians(degrees), c = std::cos(a), s = std::sin(a);
				glm::mat3 m(1.0f); m[0] = glm::vec3(c, s, 0.0f); m[1] = glm::vec3(-s, c, 0.0f); return m;
			}
			glm::mat3 Scale(const glm::vec2& s) { glm::mat3 m(1.0f); m[0][0] = s.x; m[1][1] = s.y; return m; }
			ImVec2 Apply(const glm::mat3& m, const glm::vec2& p) { const glm::vec3 r = m * glm::vec3(p, 1.0f); return ImVec2(r.x, r.y); }

			Placed Place(const UILayout& layout, const Placed& parent, glm::vec2 naturalSize, bool sized, float unit, const Animated& animated) {
				const glm::vec2 a = parent.rectMin + parent.rectSize * layout.anchorMin;
				const glm::vec2 b = parent.rectMin + parent.rectSize * layout.anchorMax;
				glm::vec2 size = sized ? layout.size * unit : naturalSize;
				// Stretched between the anchors on an axis where they are apart (size then adds to that)
				if (sized && layout.anchorMax.x != layout.anchorMin.x) size.x = (b.x - a.x) + layout.size.x * unit;
				if (sized && layout.anchorMax.y != layout.anchorMin.y) size.y = (b.y - a.y) + layout.size.y * unit;
				size = glm::max(size, glm::vec2(0.0f));
				const glm::vec2 at = glm::mix(a, b, layout.pivot) + (layout.offset + animated.position) * unit;
				Placed placed;
				placed.matrix = parent.matrix * Translate(at) * Rotate(layout.rotation + animated.rotation) * Scale(layout.scale * animated.scale);
				placed.rectSize = size;
				placed.rectMin = -layout.pivot * size;
				placed.alpha = parent.alpha * animated.alpha;
				placed.tint = parent.tint * animated.tint;
				return placed;
			}

			int TextureOf(Fox::Scene::TextureLibrary& library, const std::string& fill, const std::string& mask, std::string& problem) {
				if (fill.empty() && mask.empty()) return -1;
				try {
					return library.MaskedTexture(fill, mask, false);
				}
				catch (const std::exception& error) {
					problem = error.what();
					return -1;
				}
			}

			// Its outlines at this size (cached), and their bounds; false if there is nothing to draw
			bool PrepareText(TextComponent& text, float pixels) {
				const std::string fontFile = text.font.empty() ? Fox::Text::DefaultFont() : text.font;
				auto font = Fox::Text::Font::Get(fontFile);
				if (!font) {
					text.problem = fontFile.empty() ? "no font in Fonts/" : "cannot read " + fontFile;
					return false;
				}
				// The outlines flattened to within a third of a pixel; the size in halving steps, so resizing reuses them
				const float tolerance = 0.35f / std::max(pixels, 1.0f);
				const std::string key = text.text + "|" + fontFile + "|" + std::to_string(text.align) + "|" + std::to_string(text.lineSpacing) + "|"
					+ std::to_string(text.letterSpacing) + "|" + std::to_string(static_cast<int>(std::floor(std::log2(tolerance))));
				if (!text.cache || text.cache->key != key) {
					auto cache = std::make_shared<Fox::Scene::UITextCache>();
					cache->key = key;
					const Fox::Text::Layout layout = Fox::Text::LayOut(*font, text.text, static_cast<Fox::Text::Align>(std::clamp(text.align, 0, 2)),
						text.lineSpacing, text.letterSpacing, tolerance);
					cache->fill = Fox::Text::Triangulate(layout.contours);
					cache->min = layout.min;
					cache->max = layout.max;
					text.cache = cache;
					text.problem.clear();
				}
				return true;
			}

			void DrawUIText(TextComponent& text, const Placed& placed, float pixels, Fox::Scene::TextureLibrary& library, ImDrawList* list,
				const std::function<ImTextureID(int)>& imguiTexture) {
				if (!text.cache || text.cache->fill.triangles.empty()) return;
				const Fox::Scene::UITextCache& cache = *text.cache;
				const glm::vec2 extent = glm::max(cache.max - cache.min, glm::vec2(1e-4f));
				// Em units to the element's own space (its pivot at the origin, y down), then to the screen
				const glm::vec2 pivot(cache.min.x + extent.x * text.layout.pivot.x, cache.max.y - extent.y * text.layout.pivot.y);
				auto toScreen = [&](const glm::vec2& p) { return Apply(placed.matrix, glm::vec2((p.x - pivot.x) * pixels, -(p.y - pivot.y) * pixels)); };

				const std::string textureKey = text.fill + "|" + text.mask;
				if (text.uiTextureKey != textureKey) {
					text.uiTextureKey = textureKey;
					text.uiTexture = TextureOf(library, text.fill, text.mask, text.problem);
				}
				const bool textured = text.uiTexture >= 0;
				const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
				auto uvOf = [&](const glm::vec2& p) { return textured ? ImVec2((p.x - cache.min.x) / extent.x, (cache.max.y - p.y) / extent.y) : white; };
				const glm::vec3 rgb = glm::vec3(text.color) * placed.tint;
				const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.r, rgb.g, rgb.b, glm::clamp(text.color.a * placed.alpha, 0.0f, 1.0f)));
				if ((color & IM_COL32_A_MASK) == 0) return;
				const ImU32 clear = color & ~IM_COL32_A_MASK;

				if (textured) list->PushTexture(ImTextureRef(imguiTexture(text.uiTexture)));
				const auto& fill = cache.fill;
				size_t fringeVertices = 0, fringeIndices = 0;
				for (const auto& outline : fill.outlines) { fringeVertices += 2 * outline.size(); fringeIndices += 6 * outline.size(); }
				const size_t vertexCount = fill.points.size() + fringeVertices;
				if (vertexCount < 60000) {
					list->PrimReserve(static_cast<int>(fill.triangles.size() + fringeIndices), static_cast<int>(vertexCount));
					const unsigned int base = list->_VtxCurrentIdx;
					for (const glm::vec2& p : fill.points) list->PrimWriteVtx(toScreen(p), uvOf(p), color);
					for (uint32_t index : fill.triangles) list->PrimWriteIdx(static_cast<ImDrawIdx>(base + index));

					// A one pixel fringe fading out around every outline (the filled side is on the left): smooth edges,
					// at whatever scale the element is drawn
					const float drawnScale = std::sqrt(std::abs(glm::determinant(glm::mat2(placed.matrix))));
					const float fringe = 1.0f / std::max(pixels * drawnScale, 1e-3f);
					unsigned int at = base + static_cast<unsigned int>(fill.points.size());
					for (const auto& outline : fill.outlines) {
						const size_t n = outline.size();
						for (size_t i = 0; i < n; i++) {
							const glm::vec2 previous = outline[(i + n - 1) % n], point = outline[i], next = outline[(i + 1) % n];
							auto outward = [](const glm::vec2& edge) {
								const float length = glm::length(edge);
								return length > 1e-9f ? glm::vec2(edge.y, -edge.x) / length : glm::vec2(0.0f);
							};
							const glm::vec2 n0 = outward(point - previous), n1 = outward(next - point);
							glm::vec2 normal = n0 + n1;
							const float length = glm::length(normal);
							normal = length > 1e-6f ? normal / length : n1;
							const float miter = 1.0f / std::max(glm::dot(normal, n1), 0.35f);
							const glm::vec2 outer = point + normal * (fringe * miter);
							list->PrimWriteVtx(toScreen(point), uvOf(point), color);
							list->PrimWriteVtx(toScreen(outer), uvOf(outer), clear);
						}
						for (size_t i = 0; i < n; i++) {
							const unsigned int a = at + 2 * static_cast<unsigned int>(i), b = at + 2 * static_cast<unsigned int>((i + 1) % n);
							for (unsigned int index : { a, b, b + 1, a, b + 1, a + 1 }) list->PrimWriteIdx(static_cast<ImDrawIdx>(index));
						}
						at += 2 * static_cast<unsigned int>(n);
					}
				}
				else {
					// Very long texts: triangle by triangle (16 bit indices), without the fringe
					for (size_t t = 0; t + 2 < fill.triangles.size(); t += 3) {
						list->PrimReserve(3, 3);
						const unsigned int base = list->_VtxCurrentIdx;
						for (int k = 0; k < 3; k++) {
							const glm::vec2 p = fill.points[fill.triangles[t + k]];
							list->PrimWriteVtx(toScreen(p), uvOf(p), color);
							list->PrimWriteIdx(static_cast<ImDrawIdx>(base + k));
						}
					}
				}
				if (textured) list->PopTexture();
			}

			void DrawUISprite(SpriteComponent& sprite, const Placed& placed, ImDrawList* list, const std::function<ImTextureID(int)>& imguiTexture) {
				const glm::vec3 rgb = glm::vec3(sprite.color) * placed.tint;
				const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.r, rgb.g, rgb.b, glm::clamp(sprite.color.a * placed.alpha, 0.0f, 1.0f)));
				if ((color & IM_COL32_A_MASK) == 0) return;
				auto corner = [&](float u, float v) { return Apply(placed.matrix, placed.rectMin + placed.rectSize * glm::vec2(u, v)); };
				if (sprite.texture >= 0) {
					list->AddImageQuad(ImTextureRef(imguiTexture(sprite.texture)), corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1),
						ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), color);
				}
				else {
					list->AddQuadFilled(corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1), color);
				}
			}
		}

		namespace {
			// World canvases: a draw list each (built like the screen's), turned into pixels on the CPU
			std::map<uint64_t, std::unique_ptr<ImDrawList>> canvasLists;

			// The textures the UI asked for, by bindless index: their files (to read their pixels on the CPU)
			std::map<int, std::pair<std::string, std::string>>& TextureFiles() {
				static std::map<int, std::pair<std::string, std::string>> files;
				return files;
			}

			// World canvas draw lists name textures as this marker plus their bindless index (the screen's: ImGui's ids)
			constexpr uint64_t CANVAS_TEXTURE = 1ull << 62;

			// Draws a draw list's triangles into straight-alpha RGBA pixels: colour and texture coordinates interpolated,
			// textures sampled bilinearly, blended over what is there (as ImGui does on the GPU). Each row visits only the
			// pixels inside the triangle (the span where all three edges agree), and bands of rows are drawn on threads
			// of their own (each band draws every triangle in order, so the blending order is kept).
			// A canvas's draw list as it was when its picture was asked for, and the textures it samples
			struct CanvasDraw {
				ImVector<ImDrawCmd> CmdBuffer;
				ImVector<ImDrawVert> VtxBuffer;
				ImVector<ImDrawIdx> IdxBuffer;
				std::vector<std::shared_ptr<const Fox::Scene::RgbaImage>> images; // Per command (null: untextured)
			};

			void RasterizeRows(const CanvasDraw& list, int width, int rowBegin, int rowEnd, std::vector<uint8_t>& out) {
				auto unpack = [](ImU32 col) { return glm::vec4(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, (col >> 24) & 0xFF) * (1.0f / 255.0f); };
				for (int command = 0; command < list.CmdBuffer.Size; command++) {
					const ImDrawCmd& cmd = list.CmdBuffer[command];
					if (cmd.UserCallback || cmd.ElemCount == 0) continue;
					const Fox::Scene::RgbaImage* image = list.images[command].get();
					if (image && image->width <= 0) image = nullptr;
					const int clipX0 = std::max(0, static_cast<int>(cmd.ClipRect.x));
					const int clipY0 = std::max(rowBegin, static_cast<int>(cmd.ClipRect.y));
					const int clipX1 = std::min(width, static_cast<int>(std::ceil(cmd.ClipRect.z)));
					const int clipY1 = std::min(rowEnd, static_cast<int>(std::ceil(cmd.ClipRect.w)));
					if (clipX0 >= clipX1 || clipY0 >= clipY1) continue;
					for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3) {
						const ImDrawVert& a = list.VtxBuffer[cmd.VtxOffset + list.IdxBuffer[cmd.IdxOffset + i]];
						const ImDrawVert& b = list.VtxBuffer[cmd.VtxOffset + list.IdxBuffer[cmd.IdxOffset + i + 1]];
						const ImDrawVert& c = list.VtxBuffer[cmd.VtxOffset + list.IdxBuffer[cmd.IdxOffset + i + 2]];
						const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
						if (std::abs(area) < 1e-8f) continue;
						const int y0 = std::max(clipY0, static_cast<int>(std::floor(std::min({ a.pos.y, b.pos.y, c.pos.y }))));
						const int y1 = std::min(clipY1, static_cast<int>(std::ceil(std::max({ a.pos.y, b.pos.y, c.pos.y }))));
						if (y0 >= y1) continue;
						const int bx0 = std::max(clipX0, static_cast<int>(std::floor(std::min({ a.pos.x, b.pos.x, c.pos.x }))));
						const int bx1 = std::min(clipX1, static_cast<int>(std::ceil(std::max({ a.pos.x, b.pos.x, c.pos.x }))));
						if (bx0 >= bx1) continue;

						// Each weight is linear in the pixel centre, w = A x + B y + C: positive inside, divided by the area
						const float inv = 1.0f / area; // The sign of the area makes the inside positive
						auto edge = [&](const ImVec2& p, const ImVec2& q, float& A, float& B, float& C) {
							// (p.x - x)(q.y - y) - (p.y - y)(q.x - x)
							A = (p.y - q.y) * inv;
							B = (q.x - p.x) * inv;
							C = (p.x * q.y - p.y * q.x) * inv;
						};
						float A[3], B[3], C[3];
						edge(b.pos, c.pos, A[0], B[0], C[0]);
						edge(c.pos, a.pos, A[1], B[1], C[1]);
						edge(a.pos, b.pos, A[2], B[2], C[2]);
						const glm::vec4 ca = unpack(a.col), cb = unpack(b.col), cc = unpack(c.col);
						const bool flat = a.col == b.col && b.col == c.col;
						for (int y = y0; y < y1; y++) {
							const float py = y + 0.5f;
							// The span of pixel centres where every weight is >= 0
							float lo = bx0 + 0.5f, hi = bx1 - 0.5f;
							bool empty = false;
							for (int e = 0; e < 3; e++) {
								const float rest = B[e] * py + C[e];
								if (A[e] > 0.0f) lo = std::max(lo, -rest / A[e]);
								else if (A[e] < 0.0f) hi = std::min(hi, -rest / A[e]);
								else if (rest < 0.0f) empty = true;
							}
							if (empty || lo > hi) continue;
							const int x0 = std::max(bx0, static_cast<int>(std::ceil(lo - 0.5f)));
							const int x1 = std::min(bx1 - 1, static_cast<int>(std::floor(hi - 0.5f)));
							uint8_t* row = &out[static_cast<size_t>(y) * width * 4];
							for (int x = x0; x <= x1; x++) {
								const float px = x + 0.5f;
								const float w0 = std::max(A[0] * px + B[0] * py + C[0], 0.0f);
								const float w1 = std::max(A[1] * px + B[1] * py + C[1], 0.0f);
								const float w2 = std::max(A[2] * px + B[2] * py + C[2], 0.0f);
								glm::vec4 color = flat ? ca : ca * w0 + cb * w1 + cc * w2;
								if (image) {
									// Bilinear, repeating
									const float u = (a.uv.x * w0 + b.uv.x * w1 + c.uv.x * w2) * image->width - 0.5f;
									const float v = (a.uv.y * w0 + b.uv.y * w1 + c.uv.y * w2) * image->height - 0.5f;
									const int iu = static_cast<int>(std::floor(u)), iv = static_cast<int>(std::floor(v));
									const float fu = u - iu, fv = v - iv;
									auto texel = [&](int tx, int ty) {
										tx = ((tx % image->width) + image->width) % image->width;
										ty = ((ty % image->height) + image->height) % image->height;
										const uint8_t* t = &image->pixels[(static_cast<size_t>(ty) * image->width + tx) * 4];
										return glm::vec4(t[0], t[1], t[2], t[3]) * (1.0f / 255.0f);
									};
									color *= glm::mix(glm::mix(texel(iu, iv), texel(iu + 1, iv), fu), glm::mix(texel(iu, iv + 1), texel(iu + 1, iv + 1), fu), fv);
								}
								if (color.a <= 0.0f) continue;
								uint8_t* d = row + static_cast<size_t>(x) * 4;
								glm::vec3 rgb = glm::vec3(color);
								float alpha = color.a;
								if (color.a < 1.0f && d[3] > 0) {
									const glm::vec4 dst = glm::vec4(d[0], d[1], d[2], d[3]) * (1.0f / 255.0f);
									alpha = color.a + dst.a * (1.0f - color.a);
									rgb = (glm::vec3(color) * color.a + glm::vec3(dst) * dst.a * (1.0f - color.a)) / alpha;
								}
								d[0] = static_cast<uint8_t>(glm::clamp(rgb.r, 0.0f, 1.0f) * 255.0f + 0.5f);
								d[1] = static_cast<uint8_t>(glm::clamp(rgb.g, 0.0f, 1.0f) * 255.0f + 0.5f);
								d[2] = static_cast<uint8_t>(glm::clamp(rgb.b, 0.0f, 1.0f) * 255.0f + 0.5f);
								d[3] = static_cast<uint8_t>(glm::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
							}
						}
					}
				}
			}

			// The textures a canvas samples, copied once from the library (the copies outlive what the library keeps while a
			// picture is drawn on another thread)
			std::shared_ptr<const Fox::Scene::RgbaImage> CanvasImage(Fox::Scene::TextureLibrary& library, const std::string& image, const std::string& mask) {
				static std::map<std::pair<std::string, std::string>, std::shared_ptr<const Fox::Scene::RgbaImage>> copies;
				auto& copy = copies[{ image, mask }];
				if (!copy) {
					try { copy = std::make_shared<const Fox::Scene::RgbaImage>(library.MaskedPixels(image, mask)); }
					catch (const std::exception&) { copy = nullptr; }
				}
				return copy;
			}

			CanvasDraw CopyCanvasDraw(const ImDrawList& list, Fox::Scene::TextureLibrary& library) {
				CanvasDraw draw;
				draw.CmdBuffer = list.CmdBuffer;
				draw.VtxBuffer = list.VtxBuffer;
				draw.IdxBuffer = list.IdxBuffer;
				draw.images.resize(list.CmdBuffer.Size);
				for (int command = 0; command < list.CmdBuffer.Size; command++) {
					const ImDrawCmd& cmd = list.CmdBuffer[command];
					if (cmd.UserCallback || cmd.ElemCount == 0) continue;
					const ImTextureID id = cmd.TexRef._TexData ? ImTextureID{} : cmd.TexRef._TexID;
					if ((static_cast<uint64_t>(id) & CANVAS_TEXTURE) != 0) {
						const int index = static_cast<int>(static_cast<uint64_t>(id) & ~CANVAS_TEXTURE);
						auto file = TextureFiles().find(index);
						if (file != TextureFiles().end()) draw.images[command] = CanvasImage(library, file->second.first, file->second.second);
					}
				}
				return draw;
			}

			// The picture, in bands of rows on threads of their own (this thread draws the first)
			std::vector<uint8_t> Rasterize(const CanvasDraw& list, int width, int height) {
				std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4, 0);
				const int bands = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, std::max(1, height / 16));
				const int rowsPerBand = (height + bands - 1) / bands;
				std::vector<std::future<void>> others;
				for (int band = 1; band < bands; band++) {
					const int begin = band * rowsPerBand, end = std::min(height, begin + rowsPerBand);
					if (begin >= end) break;
					others.push_back(std::async(std::launch::async, [&list, &out, width, begin, end] { RasterizeRows(list, width, begin, end, out); }));
				}
				RasterizeRows(list, width, 0, std::min(height, rowsPerBand), out);
				for (auto& other : others) other.get();
				return out;
			}

			// Each canvas's picture being drawn on another thread: started by one frame, taken by a later one (an animated
			// sign takes milliseconds to draw: the frame goes on meanwhile)
			struct CanvasJob {
				std::future<std::vector<uint8_t>> pixels;
				int width = 0, height = 0;
			};
			std::map<uint64_t, CanvasJob> canvasJobs;

			uint64_t Hash(const ImDrawList& list) {
				uint64_t hash = 1469598103934665603ull;
				// 8 bytes at a time (a canvas's vertices are hundreds of kilobytes)
				auto mix = [&](const void* data, size_t size) {
					const uint8_t* bytes = static_cast<const uint8_t*>(data);
					size_t i = 0;
					for (; i + 8 <= size; i += 8) {
						uint64_t word;
						std::memcpy(&word, bytes + i, 8);
						hash = (hash ^ word) * 1099511628211ull;
						hash ^= hash >> 29;
					}
					for (; i < size; i++) { hash ^= bytes[i]; hash *= 1099511628211ull; }
				};
				mix(list.VtxBuffer.Data, list.VtxBuffer.Size * sizeof(ImDrawVert));
				mix(list.IdxBuffer.Data, list.IdxBuffer.Size * sizeof(ImDrawIdx));
				for (const ImDrawCmd& cmd : list.CmdBuffer) {
					const ImTextureID id = cmd.TexRef._TexData ? ImTextureID{} : cmd.TexRef._TexID;
					mix(&id, sizeof(id));
					mix(&cmd.ElemCount, sizeof(cmd.ElemCount));
				}
				return hash;
			}
		}

		void Shutdown() {
			canvasJobs.clear(); // Waits for the pictures being drawn
			canvasLists.clear();
		}

		void DrawGameUI(Fox::Scene::RayTracing::MainRayTracingScene& scene, ImDrawList* list, ImVec2 display, float deltaTime,
			const std::function<ImTextureID(int)>& texture, const glm::vec3& mouseOrigin, const glm::vec3& mouseDirection) {
			if (display.x <= 0.0f || display.y <= 0.0f) return;
			auto& graph = scene.GetSceneGraph();
			auto& registry = graph.GetRegistry();
			Fox::Scene::TextureLibrary& library = scene.GetTextureLibrary();
			const float screenUnit = scene.GetEnvironment().ui.ScaleFor(display.x, display.y); // Screen pixels per reference pixel

			// --- Animations: on in edit mode (preview) or from the start of play; over again when that changes ---
			static bool wasPlaying = false;
			const bool playing = scene.IsPlaying();
			const bool switched = playing != wasPlaying;
			wasPlaying = playing;
			registry.Each<UIAnimationComponent>([&](Entity, UIAnimationComponent& animation) {
				if (switched) {
					animation.time = 0.0f;
					animation.triggered = false;
					animation.playing = playing ? animation.playOnStart : animation.previewInEditor;
				}
				else if (!playing && !animation.triggered) {
					animation.playing = animation.previewInEditor;
				}
				if (animation.playing && animation.enabled) {
					animation.time += std::clamp(deltaTime, 0.0f, 0.25f) * animation.speed;
					// A preview of one that plays once starts over a second after it ends (not one a click or script played)
					if (!playing && !animation.triggered && animation.repeat == UIAnimationComponent::Repeat::Once) {
						std::vector<glm::vec2> times;
						const float total = Schedule(animation, times);
						if (animation.time > total + 1.0f) animation.time = 0.0f;
					}
				}
			});

			// --- The elements, each under the nearest UI element above it, on the screen or in a world canvas above it ---
			auto isElement = [&](Entity entity) {
				if (registry.Has<SpriteComponent>(entity)) return true;
				const TextComponent* text = registry.TryGet<TextComponent>(entity);
				return text && text->space == TextComponent::Space::Screen && text->style == TextComponent::Style::Flat;
			};
			struct Node { Entity entity; int layer = 0; size_t order = 0; std::vector<size_t> children; };
			std::vector<Node> nodes;
			std::unordered_map<uint64_t, size_t> nodeOf;
			const std::vector<Entity> order = graph.DepthFirst();
			for (Entity entity : order) {
				if (!isElement(entity)) continue;
				const TextComponent* text = registry.TryGet<TextComponent>(entity);
				const SpriteComponent* sprite = registry.TryGet<SpriteComponent>(entity);
				const bool enabled = sprite ? sprite->enabled : text->enabled;
				if (!enabled || !graph.IsActiveInHierarchy(entity)) continue;
				nodeOf[EntityKey(entity)] = nodes.size();
				nodes.push_back({ entity, sprite ? sprite->layout.layer : text->layout.layer, nodes.size(), {} });
			}
			// Roots by where they are drawn: 0 the screen, else the canvas entity's key
			std::map<uint64_t, std::vector<size_t>> roots;
			std::map<uint64_t, Entity> canvasOf;
			for (size_t i = 0; i < nodes.size(); i++) {
				Entity up = graph.GetParent(nodes[i].entity);
				while (graph.IsValid(up) && !nodeOf.count(EntityKey(up)) && !registry.Has<Fox::Scene::UICanvasComponent>(up)) up = graph.GetParent(up);
				if (graph.IsValid(up) && nodeOf.count(EntityKey(up))) {
					nodes[nodeOf[EntityKey(up)]].children.push_back(i);
				}
				else if (graph.IsValid(up)) {
					roots[EntityKey(up)].push_back(i);
					canvasOf[EntityKey(up)] = up;
				}
				else {
					roots[0].push_back(i);
				}
			}
			auto byLayer = [&](std::vector<size_t>& list) {
				std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) { return nodes[a].layer < nodes[b].layer; });
			};

			// --- Buttons: last frame's hover and press shape this one; clicks are the frame's ---
			struct Hit { Entity entity; glm::mat3 toLocal; glm::vec2 rectMin, rectSize; };
			registry.Each<Fox::Scene::UIButtonComponent>([&](Entity, Fox::Scene::UIButtonComponent& button) {
				button.clicked = false;
				// What its clicks switched on: off again when their time is up (play starting or stopping ends them too:
				// stopping restores the scene as it was)
				for (auto& [target, left] : button.activated) {
					left -= switched ? 1e9f : std::clamp(deltaTime, 0.0f, 0.25f);
					if (left <= 0.0f && graph.IsValid(target) && !switched) graph.SetActive(target, false);
				}
				button.activated.erase(std::remove_if(button.activated.begin(), button.activated.end(),
					[](const auto& entry) { return entry.second <= 0.0f; }), button.activated.end());
			});

			// --- Drawn parents first, each placed in its parent's rectangle ---
			std::function<void(size_t, const Placed&, ImDrawList*, float, const std::function<ImTextureID(int)>&, std::vector<Hit>&)> draw =
				[&](size_t index, const Placed& parent, ImDrawList* target, float unit, const std::function<ImTextureID(int)>& textureOf, std::vector<Hit>& hits) {
				Node& node = nodes[index];
				const UIAnimationComponent* animation = registry.TryGet<UIAnimationComponent>(node.entity);
				Animated animated = animation ? Evaluate(*animation) : Animated{};
				Fox::Scene::UIButtonComponent* button = registry.TryGet<Fox::Scene::UIButtonComponent>(node.entity);
				if (button && button->enabled && button->hovered) {
					animated.scale *= button->pressed ? button->pressedScale : button->hoverScale;
					animated.tint *= button->hoverTint;
				}
				Placed placed;
				if (SpriteComponent* sprite = registry.TryGet<SpriteComponent>(node.entity)) {
					const std::string textureKey = sprite->image + "|" + sprite->mask;
					if (sprite->textureKey != textureKey) {
						sprite->textureKey = textureKey;
						sprite->problem.clear();
						sprite->texture = TextureOf(library, sprite->image, sprite->mask, sprite->problem);
						if (sprite->texture >= 0) TextureFiles()[sprite->texture] = { sprite->image, sprite->mask };
					}
					UILayout layout = sprite->layout;
					// Keeping the image's proportions: the width from the height (not when stretched across)
					const auto& textures = Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetBindlessTextureArray();
					if (sprite->keepAspect && sprite->texture >= 0 && sprite->texture < static_cast<int>(textures.size())
						&& layout.anchorMin.x == layout.anchorMax.x && layout.anchorMin.y == layout.anchorMax.y) {
						const VkExtent3D extent = textures[sprite->texture]->GetExtent();
						if (extent.height > 0) layout.size.x = layout.size.y * static_cast<float>(extent.width) / static_cast<float>(extent.height);
					}
					placed = Place(layout, parent, glm::vec2(0.0f), true, unit, animated);
					DrawUISprite(*sprite, placed, target, textureOf);
				}
				else {
					TextComponent& text = registry.Get<TextComponent>(node.entity);
					const float pixels = std::max(text.size * unit, 1.0f); // Of an em
					glm::vec2 natural(0.0f);
					const bool ready = PrepareText(text, pixels);
					if (ready && text.cache) natural = (text.cache->max - text.cache->min) * pixels;
					placed = Place(text.layout, parent, natural, false, unit, animated);
					if (ready) {
						DrawUIText(text, placed, pixels, library, target, textureOf);
						if (text.uiTexture >= 0) TextureFiles()[text.uiTexture] = { text.fill, text.mask };
					}
				}
				if (button && button->enabled && placed.alpha > 0.01f) {
					hits.push_back({ node.entity, glm::inverse(placed.matrix), placed.rectMin, placed.rectSize });
				}
				byLayer(node.children);
				for (size_t child : node.children) draw(child, placed, target, unit, textureOf, hits);
			};
			auto inside = [](const Hit& hit, const glm::vec2& point) {
				const glm::vec2 local = glm::vec2(hit.toLocal * glm::vec3(point, 1.0f)) - hit.rectMin;
				return local.x >= 0.0f && local.y >= 0.0f && local.x <= hit.rectSize.x && local.y <= hit.rectSize.y;
			};

			// --- The screen ---
			std::vector<Hit> screenHits;
			{
				Placed screen;
				screen.rectSize = glm::vec2(display.x, display.y);
				auto& screenRoots = roots[0];
				byLayer(screenRoots);
				for (size_t root : screenRoots) draw(root, screen, list, screenUnit, texture, screenHits);
			}

			// --- World canvases: drawn into their pictures; the mouse ray where it meets them ---
			const ImGuiIO& io = ImGui::GetIO();
			const bool editorHasMouse = io.WantCaptureMouse;
			Entity over{};
			float overDistance = std::numeric_limits<float>::max();
			bool worldButtonsNear = false;
			const auto canvasTexture = [](int index) { return static_cast<ImTextureID>(CANVAS_TEXTURE | static_cast<uint64_t>(index)); };
			registry.Each<Fox::Scene::UICanvasComponent>([&](Entity entity, Fox::Scene::UICanvasComponent& canvas) {
				if (!canvas.enabled || canvas.texture < 0 || canvas.object == SIZE_MAX || canvas.object >= scene.GetObjectCount()) return;
				const int width = std::clamp(static_cast<int>(canvas.resolution.x), 16, 4096);
				const int height = std::clamp(static_cast<int>(canvas.resolution.y), 16, 4096);
				auto& owned = canvasLists[EntityKey(entity)];
				if (!owned) owned = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
				ImDrawList& canvasList = *owned;
				canvasList._ResetForNewFrame();
				canvasList.PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width), static_cast<float>(height)));
				canvasList.PushTexture(io.Fonts->TexRef);
				std::vector<Hit> hits;
				if (graph.IsActiveInHierarchy(entity)) {
					Placed rect;
					rect.rectSize = glm::vec2(width, height);
					auto& canvasRoots = roots[EntityKey(entity)];
					byLayer(canvasRoots);
					for (size_t root : canvasRoots) draw(root, rect, &canvasList, 1.0f, canvasTexture, hits);
				}
				// Its picture, drawn again (on another thread) only when what is on it changed. A finished one is compared with
				// the last (which the GPU has) and only the rectangle that differs is sent: an animated sign changes a small part
				CanvasJob& job = canvasJobs[EntityKey(entity)];
				if (job.pixels.valid() && job.pixels.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
					std::vector<uint8_t> fresh = job.pixels.get();
					if (job.width == width && job.height == height && fresh.size() == canvas.pixels.size()) {
						int top = height, bottom = -1, left = width, right = -1;
						const size_t rowBytes = static_cast<size_t>(width) * 4;
						for (int y = 0; y < height; y++) {
							const uint32_t* a = reinterpret_cast<const uint32_t*>(fresh.data() + y * rowBytes);
							const uint32_t* b = reinterpret_cast<const uint32_t*>(canvas.pixels.data() + y * rowBytes);
							if (std::memcmp(a, b, rowBytes) == 0) continue;
							top = std::min(top, y);
							bottom = y;
							int x0 = 0;
							while (x0 < width && a[x0] == b[x0]) x0++;
							int x1 = width - 1;
							while (x1 > x0 && a[x1] == b[x1]) x1--;
							left = std::min(left, x0);
							right = std::max(right, x1);
						}
						canvas.pixels.swap(fresh);
						if (bottom >= top) {
							const int w = right - left + 1, h = bottom - top + 1;
							std::vector<uint8_t> region(static_cast<size_t>(w) * h * 4);
							for (int y = 0; y < h; y++) {
								std::memcpy(region.data() + static_cast<size_t>(y) * w * 4,
									canvas.pixels.data() + (static_cast<size_t>(top + y) * width + left) * 4, static_cast<size_t>(w) * 4);
							}
							scene.UpdateTextureRegion(canvas.texture, left, top, w, h, std::move(region));
						}
					}
					else {
						canvas.drawnHash = 0; // Drawn for another size: again
					}
				}
				const uint64_t hash = Hash(canvasList);
				if (hash != canvas.drawnHash && !job.pixels.valid() && static_cast<int>(canvas.pixels.size()) == width * height * 4) {
					canvas.drawnHash = hash;
					job.width = width;
					job.height = height;
					job.pixels = std::async(std::launch::async, [draw = CopyCanvasDraw(canvasList, library), width, height] { return Rasterize(draw, width, height); });
				}

				// The mouse ray on its panel (its object as traced), in its pixels
				if (hits.empty()) return;
				const glm::mat4 toLocal = glm::inverse(scene.GetObjectTransform(canvas.object).Matrix());
				const glm::vec3 origin = glm::vec3(toLocal * glm::vec4(mouseOrigin, 1.0f));
				const glm::vec3 direction = glm::vec3(toLocal * glm::vec4(mouseDirection, 0.0f));
				const float panelWidth = std::max(canvas.width, 0.01f), panelHeight = panelWidth * height / static_cast<float>(width);
				if (glm::distance(mouseOrigin, glm::vec3(glm::inverse(toLocal)[3])) <= canvas.interactDistance) worldButtonsNear = true;
				if (std::abs(direction.z) < 1e-8f || editorHasMouse) return;
				const float t = -origin.z / direction.z;
				if (t <= 0.0f) return;
				const glm::vec3 at = origin + direction * t;
				const glm::vec2 pixel((at.x / panelWidth + 0.5f) * width, (0.5f - at.y / panelHeight) * height);
				const float distance = t * glm::length(glm::vec3(scene.GetObjectTransform(canvas.object).Matrix() * glm::vec4(direction, 0.0f)));
				for (const Hit& hit : hits) {
					if (inside(hit, pixel) && distance < overDistance) {
						over = hit.entity;
						overDistance = distance;
					}
				}
			});

			// --- The button under the mouse: the screen's first (drawn on top), else the nearest world canvas's ---
			const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
			if (!editorHasMouse && io.MousePos.x > -1e30f) {
				Entity onScreen{};
				for (const Hit& hit : screenHits) {
					if (inside(hit, mouse)) onScreen = hit.entity;
				}
				if (!onScreen.IsNull()) over = onScreen;
			}
			pointerOverButton = !over.IsNull();
			buttonsShown = !screenHits.empty() || worldButtonsNear;
			registry.Each<Fox::Scene::UIButtonComponent>([&](Entity entity, Fox::Scene::UIButtonComponent& button) {
				const bool hovered = !over.IsNull() && entity == over;
				button.hovered = hovered;
				if (hovered && io.MouseClicked[0]) button.pressed = true;
				if (!io.MouseDown[0]) {
					// Released over it after pressing it: a click
					if (button.pressed && hovered) {
						button.clicked = true;
						button.clicks++;
						// Switch the named entities on (for a while)
						auto forEachNamed = [&](const std::string& list, const std::function<void(Entity)>& action) {
							size_t from = 0;
							while (from <= list.size()) {
								size_t to = list.find(',', from);
								if (to == std::string::npos) to = list.size();
								std::string name = list.substr(from, to - from);
								name.erase(0, name.find_first_not_of(" \t"));
								name.erase(name.find_last_not_of(" \t") + 1);
								from = to + 1;
								if (name.empty()) continue;
								for (Entity e : order) if (graph.GetName(e) == name) action(e);
							}
						};
						forEachNamed(button.activateOnClick, [&](Entity e) {
							graph.SetActive(e, true);
							if (button.activeSeconds > 0.0f) {
								auto it = std::find_if(button.activated.begin(), button.activated.end(), [&](const auto& entry) { return entry.first == e; });
								if (it != button.activated.end()) it->second = button.activeSeconds; // Again: its time starts over
								else button.activated.push_back({ e, button.activeSeconds });
							}
						});
						// Play the named entities' animations (and their children's) from the start
						std::string names = button.playOnClick;
						size_t start = 0;
						while (start <= names.size()) {
							size_t end = names.find(',', start);
							if (end == std::string::npos) end = names.size();
							std::string name = names.substr(start, end - start);
							name.erase(0, name.find_first_not_of(" \t"));
							name.erase(name.find_last_not_of(" \t") + 1);
							start = end + 1;
							if (name.empty()) continue;
							for (Entity target : order) {
								if (graph.GetName(target) != name) continue;
								const std::vector<Entity> group = button.withChildren ? graph.Subtree(target) : std::vector<Entity>{ target };
								for (Entity e : group) {
									if (auto* animation = registry.TryGet<UIAnimationComponent>(e)) {
										animation->time = 0.0f;
										animation->playing = true;
										animation->triggered = true;
									}
								}
							}
						}
					}
					button.pressed = false;
				}
			});
		}
	}
}
