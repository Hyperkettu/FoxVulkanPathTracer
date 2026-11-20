#include "FoxRenderer.h"

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

		namespace {
			using Fox::Scene::TextComponent;
			using Fox::Scene::SpriteComponent;

			int TextureOf(Fox::Scene::TextureLibrary& library, const std::string& fill, const std::string& mask, std::string& problem) {
				if (fill.empty() && mask.empty()) {
					return -1;
				}
				try {
					return library.MaskedTexture(fill, mask, false);
				}
				catch (const std::exception& error) {
					problem = error.what();
					return -1;
				}
			}

			void DrawUIText(TextComponent& text, Fox::Scene::TextureLibrary& library, ImDrawList* list, ImVec2 display, float unit,
				const std::function<ImTextureID(int)>& imguiTexture) {
				const std::string fontFile = text.font.empty() ? Fox::Text::DefaultFont() : text.font;
				auto font = Fox::Text::Font::Get(fontFile);
				if (!font) {
					text.problem = fontFile.empty() ? "no font in Fonts/" : "cannot read " + fontFile;
					return;
				}
				const float pixels = std::max(text.size * unit, 1.0f); // Of an em
				// The outlines flattened to within a third of a pixel; the size in halving steps, so resizing reuses them
				const float tolerance = 0.35f / pixels;
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
				const Fox::Scene::UITextCache& cache = *text.cache;
				if (cache.fill.triangles.empty()) {
					return;
				}

				// Where: the pivot of its bounds at the screen position
				const glm::vec2 extent = glm::max(cache.max - cache.min, glm::vec2(1e-4f));
				const glm::vec2 pivot(cache.min.x + extent.x * text.pivot.x, cache.max.y - extent.y * text.pivot.y);
				const ImVec2 anchor(text.screenPosition.x * display.x, text.screenPosition.y * display.y);
				auto toScreen = [&](const glm::vec2& p) { return ImVec2(anchor.x + (p.x - pivot.x) * pixels, anchor.y - (p.y - pivot.y) * pixels); };

				// Its texture (fill and mask), or plain colour
				const std::string textureKey = text.fill + "|" + text.mask;
				if (text.uiTextureKey != textureKey) {
					text.uiTextureKey = textureKey;
					text.uiTexture = TextureOf(library, text.fill, text.mask, text.problem);
				}
				const bool textured = text.uiTexture >= 0;
				const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
				auto uvOf = [&](const glm::vec2& p) {
					return textured ? ImVec2((p.x - cache.min.x) / extent.x, (cache.max.y - p.y) / extent.y) : white;
				};
				const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(text.color.r, text.color.g, text.color.b, text.color.a));
				const ImU32 clear = color & ~IM_COL32_A_MASK;

				if (textured) {
					list->PushTexture(ImTextureRef(imguiTexture(text.uiTexture)));
				}
				const auto& fill = cache.fill;
				size_t fringeVertices = 0;
				for (const auto& outline : fill.outlines) fringeVertices += 2 * outline.size();
				const size_t vertexCount = fill.points.size() + fringeVertices;
				if (vertexCount < 60000) {
					size_t fringeIndices = 0;
					for (const auto& outline : fill.outlines) fringeIndices += 6 * outline.size();
					list->PrimReserve(static_cast<int>(fill.triangles.size() + fringeIndices), static_cast<int>(vertexCount));
					const unsigned int base = list->_VtxCurrentIdx;
					for (const glm::vec2& p : fill.points) list->PrimWriteVtx(toScreen(p), uvOf(p), color);
					for (uint32_t index : fill.triangles) list->PrimWriteIdx(static_cast<ImDrawIdx>(base + index));

					// A one pixel fringe fading out around every outline: smooth edges (the filled side is on the left)
					const float fringe = 1.0f / pixels;
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
							list->PrimWriteIdx(static_cast<ImDrawIdx>(a));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(b));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(a));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(a + 1));
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
				if (textured) {
					list->PopTexture();
				}
			}

			void DrawUISprite(SpriteComponent& sprite, Fox::Scene::TextureLibrary& library, ImDrawList* list, ImVec2 display, float unit,
				const std::function<ImTextureID(int)>& imguiTexture) {
				const std::string textureKey = sprite.image + "|" + sprite.mask;
				if (sprite.textureKey != textureKey) {
					sprite.textureKey = textureKey;
					sprite.problem.clear();
					sprite.texture = TextureOf(library, sprite.image, sprite.mask, sprite.problem);
				}
				glm::vec2 size = sprite.size * unit;
				const auto& textures = Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetBindlessTextureArray();
				if (sprite.keepAspect && sprite.texture >= 0 && sprite.texture < static_cast<int>(textures.size())) {
					const VkExtent3D extent = textures[sprite.texture]->GetExtent();
					if (extent.height > 0) size.x = size.y * static_cast<float>(extent.width) / static_cast<float>(extent.height);
				}
				const glm::vec2 anchor(sprite.screenPosition.x * display.x, sprite.screenPosition.y * display.y);
				const float angle = glm::radians(sprite.rotation);
				const glm::vec2 axisX(std::cos(angle), std::sin(angle)), axisY(-std::sin(angle), std::cos(angle));
				auto corner = [&](float u, float v) {
					const glm::vec2 local((u - sprite.pivot.x) * size.x, (v - sprite.pivot.y) * size.y);
					const glm::vec2 p = anchor + axisX * local.x + axisY * local.y;
					return ImVec2(p.x, p.y);
				};
				const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a));
				if (sprite.texture >= 0) {
					list->AddImageQuad(ImTextureRef(imguiTexture(sprite.texture)), corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1),
						ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), color);
				}
				else {
					list->AddQuadFilled(corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1), color);
				}
			}
		}

		void DrawGameUI(Fox::Scene::RayTracing::MainRayTracingScene& scene, ImDrawList* list, ImVec2 display,
			const std::function<ImTextureID(int)>& texture) {
			if (display.x <= 0.0f || display.y <= 0.0f) {
				return;
			}
			auto& graph = scene.GetSceneGraph();
			auto& registry = graph.GetRegistry();
			const float unit = display.y / 1080.0f;

			using Entity = Fox::Scene::RayTracing::MainRayTracingScene::Entity;
			struct Item { int layer; size_t order; Entity entity; bool text; };
			std::vector<Item> items;
			registry.Each<TextComponent>([&](Entity entity, TextComponent& text) {
				if (text.enabled && text.space == TextComponent::Space::Screen && text.style == TextComponent::Style::Flat && graph.IsActiveInHierarchy(entity)) {
					items.push_back({ text.layer, items.size(), entity, true });
				}
			});
			registry.Each<SpriteComponent>([&](Entity entity, SpriteComponent& sprite) {
				if (sprite.enabled && graph.IsActiveInHierarchy(entity)) {
					items.push_back({ sprite.layer, items.size(), entity, false });
				}
			});
			std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });

			Fox::Scene::TextureLibrary& library = scene.GetTextureLibrary();
			for (const Item& item : items) {
				if (item.text) {
					DrawUIText(registry.Get<TextComponent>(item.entity), library, list, display, unit, texture);
				}
				else {
					DrawUISprite(registry.Get<SpriteComponent>(item.entity), library, list, display, unit, texture);
				}
			}
		}
	}
}
