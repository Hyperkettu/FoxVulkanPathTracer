#include "FoxRenderer.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <unordered_map>

// stb_truetype as ImGui ships it, compiled here as a private (static) copy
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace Fox {

	namespace Text {

		// ------------------------------------------------------------------------------------------------
		// Fonts
		// ------------------------------------------------------------------------------------------------

		std::shared_ptr<Font> Font::Get(const std::string& file) {
			// Kept for good: a font is read once (a weak reference let it go as soon as each text was drawn, so every
			// text read its font file again every frame, and its glyphs' outlines were flattened again)
			static std::map<std::string, std::shared_ptr<Font>> loaded;
			if (auto it = loaded.find(file); it != loaded.end()) {
				return it->second;
			}
			std::ifstream in(file, std::ios::binary);
			if (!in) {
				return nullptr;
			}
			std::shared_ptr<Font> font(new Font());
			font->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			auto* info = new stbtt_fontinfo();
			const int offset = stbtt_GetFontOffsetForIndex(font->data.data(), 0);
			if (font->data.empty() || offset < 0 || !stbtt_InitFont(info, font->data.data(), offset)) {
				delete info;
				return nullptr;
			}
			font->info = info;
			font->scale = stbtt_ScaleForMappingEmToPixels(info, 1.0f);
			int ascent = 0, descent = 0, lineGap = 0;
			stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
			font->ascent = ascent * font->scale;
			font->descent = descent * font->scale;
			font->lineGap = lineGap * font->scale;
			loaded[file] = font;
			return font;
		}

		Font::~Font() {
			delete static_cast<stbtt_fontinfo*>(info);
		}

		const Font::Glyph& Font::GetGlyph(uint32_t codepoint, float tolerance) {
			// Tolerance in halving steps, so nearby sizes share their outlines
			const int level = static_cast<int>(std::floor(std::log2(std::max(tolerance, 1e-5f))));
			const auto key = std::make_pair(codepoint, level);
			if (auto it = glyphs.find(key); it != glyphs.end()) {
				return it->second;
			}
			const float step = std::exp2(static_cast<float>(level));
			auto* font = static_cast<stbtt_fontinfo*>(info);
			Glyph glyph;
			const int index = stbtt_FindGlyphIndex(font, static_cast<int>(codepoint));
			int advance = 0, bearing = 0;
			if (index == 0 && codepoint != ' ') {
				// Not in the font: a gap the width of a space, not the font's "missing" glyph (often a box or a logo)
				stbtt_GetGlyphHMetrics(font, stbtt_FindGlyphIndex(font, ' '), &advance, &bearing);
				glyph.advance = advance * scale;
				return glyphs.emplace(key, std::move(glyph)).first->second;
			}
			stbtt_GetGlyphHMetrics(font, index, &advance, &bearing);
			glyph.advance = advance * scale;

			stbtt_vertex* shape = nullptr;
			const int count = stbtt_GetGlyphShape(font, index, &shape);
			Contour contour;
			glm::vec2 at(0.0f);
			auto finish = [&]() {
				// Closed: drop a last point repeating the first
				if (contour.size() > 1 && glm::distance(contour.front(), contour.back()) < 1e-6f) contour.pop_back();
				if (contour.size() >= 3) glyph.contours.push_back(std::move(contour));
				contour.clear();
			};
			// Curves flattened into enough segments that they stray at most `step` from the curve
			auto segments = [step](float length) {
				return std::clamp(static_cast<int>(std::ceil(std::sqrt(length / (8.0f * step)) * 2.0f)), 1, 64);
			};
			for (int i = 0; i < count; i++) {
				const stbtt_vertex& v = shape[i];
				const glm::vec2 to(v.x * scale, v.y * scale);
				switch (v.type) {
				case STBTT_vmove:
					finish();
					contour.push_back(to);
					break;
				case STBTT_vline:
					contour.push_back(to);
					break;
				case STBTT_vcurve: {
					const glm::vec2 control(v.cx * scale, v.cy * scale);
					const int n = segments(glm::length(control - at) + glm::length(to - control));
					for (int k = 1; k <= n; k++) {
						const float t = static_cast<float>(k) / n, s = 1.0f - t;
						contour.push_back(s * s * at + 2.0f * s * t * control + t * t * to);
					}
					break;
				}
				case STBTT_vcubic: {
					const glm::vec2 c1(v.cx * scale, v.cy * scale), c2(v.cx1 * scale, v.cy1 * scale);
					const int n = segments(glm::length(c1 - at) + glm::length(c2 - c1) + glm::length(to - c2));
					for (int k = 1; k <= n; k++) {
						const float t = static_cast<float>(k) / n, s = 1.0f - t;
						contour.push_back(s * s * s * at + 3.0f * s * s * t * c1 + 3.0f * s * t * t * c2 + t * t * t * to);
					}
					break;
				}
				}
				at = to;
			}
			finish();
			stbtt_FreeShape(font, shape);
			return glyphs.emplace(key, std::move(glyph)).first->second;
		}

		float Font::Kerning(uint32_t left, uint32_t right) const {
			return stbtt_GetCodepointKernAdvance(static_cast<stbtt_fontinfo*>(info), static_cast<int>(left), static_cast<int>(right)) * scale;
		}

		std::vector<uint32_t> DecodeUtf8(const std::string& text) {
			std::vector<uint32_t> out;
			for (size_t i = 0; i < text.size();) {
				const unsigned char c = static_cast<unsigned char>(text[i]);
				int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
				if (length == 0 || i + length > text.size()) {
					i++;
					continue;
				}
				uint32_t code = length == 1 ? c : length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
				for (int k = 1; k < length; k++) {
					code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
				}
				out.push_back(code);
				i += length;
			}
			return out;
		}

		std::vector<std::string> ListFonts(const std::string& folder) {
			std::vector<std::string> files;
			std::error_code error;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (entry.is_regular_file() && (extension == ".ttf" || extension == ".otf")) {
					files.push_back(entry.path().generic_string());
				}
			}
			std::sort(files.begin(), files.end());
			return files;
		}

		const std::string& DefaultFont() {
			static const std::string font = [] { const auto fonts = ListFonts(); return fonts.empty() ? std::string() : fonts.front(); }();
			return font;
		}

		Layout LayOut(Font& font, const std::string& text, Align align, float lineSpacing, float letterSpacing, float tolerance) {
			Layout layout;
			const std::vector<uint32_t> codes = DecodeUtf8(text);
			const float lineHeight = (font.Ascent() - font.Descent() + font.LineGap()) * std::max(lineSpacing, 0.1f);

			// Lines, each laid out from x = 0, then shifted by its alignment
			size_t lineStart = 0;
			float y = 0.0f;
			float x = 0.0f;
			uint32_t previous = 0;
			bool first = true;
			auto endLine = [&](size_t end) {
				const float width = x - (first ? 0.0f : letterSpacing);
				const float shift = align == Align::Left ? 0.0f : align == Align::Center ? -0.5f * width : -width;
				for (size_t c = lineStart; c < end; c++) {
					for (glm::vec2& p : layout.contours[c]) p.x += shift;
				}
				lineStart = layout.contours.size();
			};
			for (uint32_t code : codes) {
				if (code == '\n') {
					endLine(layout.contours.size());
					y -= lineHeight;
					x = 0.0f;
					previous = 0;
					first = true;
					layout.lines++;
					continue;
				}
				if (code == '\r') continue;
				if (previous) x += font.Kerning(previous, code);
				const Font::Glyph& glyph = font.GetGlyph(code, tolerance);
				for (const Contour& contour : glyph.contours) {
					Contour placed = contour;
					for (glm::vec2& p : placed) p += glm::vec2(x, y);
					layout.contours.push_back(std::move(placed));
				}
				x += glyph.advance + letterSpacing;
				previous = code;
				first = false;
			}
			endLine(layout.contours.size());

			// Bounds: of the outlines, or of the line boxes when there are none (spaces only)
			layout.min = glm::vec2(1e30f);
			layout.max = glm::vec2(-1e30f);
			for (const Contour& contour : layout.contours) {
				for (const glm::vec2& p : contour) {
					layout.min = glm::min(layout.min, p);
					layout.max = glm::max(layout.max, p);
				}
			}
			if (layout.contours.empty()) {
				layout.min = glm::vec2(0.0f, y + font.Descent());
				layout.max = glm::vec2(0.0f, font.Ascent());
			}
			return layout;
		}

		// ------------------------------------------------------------------------------------------------
		// Triangulation: ear clipping with holes bridged into their outer contour (after mapbox/earcut)
		// ------------------------------------------------------------------------------------------------

		namespace {
			struct Node {
				uint32_t i = 0;
				float x = 0.0f, y = 0.0f;
				Node* prev = nullptr;
				Node* next = nullptr;
				bool steiner = false;
			};

			class Earcut {
			public:
				std::deque<Node> nodes;
				std::vector<uint32_t>* triangles = nullptr;

				Node* Insert(uint32_t i, float x, float y, Node* last) {
					nodes.push_back(Node{ i, x, y });
					Node* p = &nodes.back();
					if (!last) { p->prev = p; p->next = p; }
					else { p->next = last->next; p->prev = last; last->next->prev = p; last->next = p; }
					return p;
				}
				static void Remove(Node* p) { p->next->prev = p->prev; p->prev->next = p->next; }
				static bool Equals(const Node* a, const Node* b) { return a->x == b->x && a->y == b->y; }
				static float Area(const Node* p, const Node* q, const Node* r) {
					return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
				}
				static bool PointInTriangle(float ax, float ay, float bx, float by, float cx, float cy, float px, float py) {
					return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py)
						&& (bx - px) * (cy - py) >= (cx - px) * (by - py);
				}
				static int Sign(float v) { return v > 0.0f ? 1 : v < 0.0f ? -1 : 0; }
				static bool OnSegment(const Node* p, const Node* q, const Node* r) {
					return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y) && q->y >= std::min(p->y, r->y);
				}
				static bool Intersects(const Node* p1, const Node* q1, const Node* p2, const Node* q2) {
					const int o1 = Sign(Area(p1, q1, p2)), o2 = Sign(Area(p1, q1, q2)), o3 = Sign(Area(p2, q2, p1)), o4 = Sign(Area(p2, q2, q1));
					if (o1 != o2 && o3 != o4) return true;
					if (o1 == 0 && OnSegment(p1, p2, q1)) return true;
					if (o2 == 0 && OnSegment(p1, q2, q1)) return true;
					if (o3 == 0 && OnSegment(p2, p1, q2)) return true;
					if (o4 == 0 && OnSegment(p2, q1, q2)) return true;
					return false;
				}
				static bool IntersectsPolygon(const Node* a, const Node* b) {
					const Node* p = a;
					do {
						if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && Intersects(p, p->next, a, b)) return true;
						p = p->next;
					} while (p != a);
					return false;
				}
				static bool LocallyInside(const Node* a, const Node* b) {
					return Area(a->prev, a, a->next) < 0.0f ? Area(a, b, a->next) >= 0.0f && Area(a, a->prev, b) >= 0.0f
						: Area(a, b, a->prev) < 0.0f || Area(a, a->next, b) < 0.0f;
				}
				static bool MiddleInside(const Node* a, const Node* b) {
					const Node* p = a;
					bool inside = false;
					const float px = 0.5f * (a->x + b->x), py = 0.5f * (a->y + b->y);
					do {
						if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)) {
							inside = !inside;
						}
						p = p->next;
					} while (p != a);
					return inside;
				}
				static bool ValidDiagonal(const Node* a, const Node* b) {
					return a->next->i != b->i && a->prev->i != b->i && !IntersectsPolygon(a, b)
						&& ((LocallyInside(a, b) && LocallyInside(b, a) && MiddleInside(a, b) && (Area(a->prev, a, b->prev) != 0.0f || Area(a, b->prev, b) != 0.0f))
							|| (Equals(a, b) && Area(a->prev, a, a->next) > 0.0f && Area(b->prev, b, b->next) > 0.0f));
				}
				Node* Split(Node* a, Node* b) {
					nodes.push_back(Node{ a->i, a->x, a->y });
					Node* a2 = &nodes.back();
					nodes.push_back(Node{ b->i, b->x, b->y });
					Node* b2 = &nodes.back();
					Node* an = a->next;
					Node* bp = b->prev;
					a->next = b; b->prev = a;
					a2->next = an; an->prev = a2;
					b2->next = a2; a2->prev = b2;
					bp->next = b2; b2->prev = bp;
					return b2;
				}
				Node* Filter(Node* start, Node* end = nullptr) {
					if (!start) return start;
					if (!end) end = start;
					Node* p = start;
					bool again;
					do {
						again = false;
						if (!p->steiner && (Equals(p, p->next) || Area(p->prev, p, p->next) == 0.0f)) {
							Remove(p);
							p = end = p->prev;
							if (p == p->next) break;
							again = true;
						}
						else {
							p = p->next;
						}
					} while (again || p != end);
					return end;
				}
				static bool IsEar(const Node* ear) {
					const Node* a = ear->prev; const Node* b = ear; const Node* c = ear->next;
					if (Area(a, b, c) >= 0.0f) return false; // Reflex
					const Node* p = ear->next->next;
					while (p != ear->prev) {
						if (PointInTriangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && Area(p->prev, p, p->next) >= 0.0f) return false;
						p = p->next;
					}
					return true;
				}
				Node* CureLocalIntersections(Node* start) {
					Node* p = start;
					do {
						Node* a = p->prev; Node* b = p->next->next;
						if (!Equals(a, b) && Intersects(a, p, p->next, b) && LocallyInside(a, b) && LocallyInside(b, a)) {
							triangles->insert(triangles->end(), { a->i, p->i, b->i });
							Remove(p); Remove(p->next);
							p = start = b;
						}
						p = p->next;
					} while (p != start);
					return Filter(p);
				}
				void SplitEarcut(Node* start) {
					Node* a = start;
					do {
						Node* b = a->next->next;
						while (b != a->prev) {
							if (a->i != b->i && ValidDiagonal(a, b)) {
								Node* c = Split(a, b);
								a = Filter(a, a->next);
								c = Filter(c, c->next);
								Linked(a, 0);
								Linked(c, 0);
								return;
							}
							b = b->next;
						}
						a = a->next;
					} while (a != start);
				}
				void Linked(Node* ear, int pass) {
					if (!ear) return;
					Node* stop = ear;
					int guard = 0;
					while (ear->prev != ear->next && guard++ < 1'000'000) {
						Node* prev = ear->prev; Node* next = ear->next;
						if (IsEar(ear)) {
							triangles->insert(triangles->end(), { prev->i, ear->i, next->i });
							Remove(ear);
							ear = next->next;
							stop = next->next;
							continue;
						}
						ear = next;
						if (ear == stop) {
							if (pass == 0) Linked(Filter(ear), 1);
							else if (pass == 1) Linked(CureLocalIntersections(Filter(ear)), 2);
							else SplitEarcut(ear);
							break;
						}
					}
				}
				static Node* Leftmost(Node* start) {
					Node* p = start; Node* left = start;
					do {
						if (p->x < left->x || (p->x == left->x && p->y < left->y)) left = p;
						p = p->next;
					} while (p != start);
					return left;
				}
				static bool SectorContainsSector(const Node* m, const Node* p) {
					return Area(m->prev, m, p->prev) < 0.0f && Area(p->next, m, m->next) < 0.0f;
				}
				static Node* HoleBridge(Node* hole, Node* outer) {
					Node* p = outer;
					const float hx = hole->x, hy = hole->y;
					float qx = -std::numeric_limits<float>::infinity();
					Node* m = nullptr;
					do {
						if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
							const float x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
							if (x <= hx && x > qx) {
								qx = x;
								m = p->x < p->next->x ? p : p->next;
								if (x == hx) return m;
							}
						}
						p = p->next;
					} while (p != outer);
					if (!m) return nullptr;
					Node* stop = m;
					const float mx = m->x, my = m->y;
					float tanMin = std::numeric_limits<float>::infinity();
					p = m;
					do {
						if (hx >= p->x && p->x >= mx && hx != p->x
							&& PointInTriangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
							const float tangent = std::abs(hy - p->y) / (hx - p->x);
							if (LocallyInside(p, hole) && (tangent < tanMin || (tangent == tanMin && (p->x > m->x || (p->x == m->x && SectorContainsSector(m, p)))))) {
								m = p;
								tanMin = tangent;
							}
						}
						p = p->next;
					} while (p != stop);
					return m;
				}
				// A ring of points in the list (clockwise in earcut's sense = counter-clockwise with y up, when outer)
				Node* Ring(const std::vector<glm::vec2>& points, uint32_t first, uint32_t count, bool clockwise) {
					float sum = 0.0f;
					for (uint32_t i = 0, j = count - 1; i < count; j = i++) {
						sum += (points[first + j].x - points[first + i].x) * (points[first + i].y + points[first + j].y);
					}
					Node* last = nullptr;
					if (clockwise == (sum > 0.0f)) {
						for (uint32_t i = 0; i < count; i++) last = Insert(first + i, points[first + i].x, points[first + i].y, last);
					}
					else {
						for (uint32_t i = count; i-- > 0;) last = Insert(first + i, points[first + i].x, points[first + i].y, last);
					}
					if (last && Equals(last, last->next)) { Remove(last); last = last->next; }
					return last;
				}
			};

			float SignedArea(const Contour& c) {
				float sum = 0.0f;
				for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) sum += c[j].x * c[i].y - c[i].x * c[j].y;
				return 0.5f * sum;
			}
			bool Inside(const Contour& c, const glm::vec2& p) {
				bool inside = false;
				for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
					if (((c[i].y > p.y) != (c[j].y > p.y)) && p.x < (c[j].x - c[i].x) * (p.y - c[i].y) / (c[j].y - c[i].y) + c[i].x) inside = !inside;
				}
				return inside;
			}
		}

		Fill Triangulate(const std::vector<Contour>& input) {
			Fill fill;
			// Outer contours wind one way, holes the other (TrueType: outer clockwise; CFF: counter-clockwise). Each glyph
			// is laid out separately, so the convention is the largest contour's: decided over all of them.
			std::vector<Contour> contours;
			for (const Contour& c : input) {
				if (c.size() >= 3 && std::abs(SignedArea(c)) > 1e-10f) contours.push_back(c);
			}
			if (contours.empty()) return fill;
			float largest = 0.0f;
			float outerSign = 1.0f;
			for (const Contour& c : contours) {
				const float area = SignedArea(c);
				if (std::abs(area) > largest) { largest = std::abs(area); outerSign = area > 0.0f ? 1.0f : -1.0f; }
			}
			std::vector<size_t> outers, holes;
			for (size_t i = 0; i < contours.size(); i++) {
				(SignedArea(contours[i]) * outerSign > 0.0f ? outers : holes).push_back(i);
			}
			// Each hole goes to the smallest outer contour around it
			std::vector<std::vector<size_t>> holesOf(contours.size());
			for (size_t h : holes) {
				size_t best = SIZE_MAX;
				float bestArea = std::numeric_limits<float>::max();
				for (size_t o : outers) {
					const float area = std::abs(SignedArea(contours[o]));
					if (area < bestArea && Inside(contours[o], contours[h].front())) { best = o; bestArea = area; }
				}
				if (best != SIZE_MAX) holesOf[best].push_back(h);
			}

			for (size_t o : outers) {
				// The points of the outer contour and its holes, then ear clipping
				const uint32_t base = static_cast<uint32_t>(fill.points.size());
				std::vector<std::pair<uint32_t, uint32_t>> rings; // first, count
				auto add = [&](const Contour& c) {
					rings.emplace_back(static_cast<uint32_t>(fill.points.size()), static_cast<uint32_t>(c.size()));
					fill.points.insert(fill.points.end(), c.begin(), c.end());
				};
				add(contours[o]);
				for (size_t h : holesOf[o]) add(contours[h]);

				Earcut earcut;
				std::vector<uint32_t> triangles;
				earcut.triangles = &triangles;
				Node* outer = earcut.Ring(fill.points, rings[0].first, rings[0].second, true);
				if (!outer || outer->next == outer->prev) continue;
				std::vector<Node*> queue;
				for (size_t r = 1; r < rings.size(); r++) {
					Node* list = earcut.Ring(fill.points, rings[r].first, rings[r].second, false);
					if (!list) continue;
					if (list == list->next) list->steiner = true;
					queue.push_back(Earcut::Leftmost(list));
				}
				std::sort(queue.begin(), queue.end(), [](const Node* a, const Node* b) { return a->x < b->x; });
				for (Node* hole : queue) {
					Node* bridge = Earcut::HoleBridge(hole, outer);
					if (!bridge) continue;
					Node* reverse = earcut.Split(bridge, hole);
					earcut.Filter(reverse, reverse->next);
					outer = earcut.Filter(bridge, bridge->next);
				}
				earcut.Linked(outer, 0);

				// Counter-clockwise (y up)
				for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
					const glm::vec2 a = fill.points[triangles[t]], b = fill.points[triangles[t + 1]], c = fill.points[triangles[t + 2]];
					if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) < 0.0f) std::swap(triangles[t + 1], triangles[t + 2]);
				}
				fill.triangles.insert(fill.triangles.end(), triangles.begin(), triangles.end());
				(void)base;
			}

			// Outlines with the filled side on the left: outer counter-clockwise, holes clockwise
			for (size_t o : outers) {
				Contour c = contours[o];
				if (SignedArea(c) < 0.0f) std::reverse(c.begin(), c.end());
				fill.outlines.push_back(std::move(c));
			}
			for (size_t h : holes) {
				Contour c = contours[h];
				if (SignedArea(c) > 0.0f) std::reverse(c.begin(), c.end());
				fill.outlines.push_back(std::move(c));
			}
			return fill;
		}

		// ------------------------------------------------------------------------------------------------
		// Meshes
		// ------------------------------------------------------------------------------------------------

		void BuildMesh(const Layout& layout, const Fill& fill, const MeshOptions& options,
			std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<uint32_t>& indices) {
			using Fox::Graphics::Vulkan::Vertex;
			const glm::vec2 extent = glm::max(layout.max - layout.min, glm::vec2(1e-4f));
			const glm::vec2 origin = layout.min + extent * options.pivot;
			const float size = options.size;
			auto uvOf = [&](const glm::vec2& p) { return glm::vec2((p.x - layout.min.x) / extent.x, 1.0f - (p.y - layout.min.y) / extent.y); };

			struct Corner { glm::vec3 position; glm::vec3 normal; glm::vec2 uv; };
			std::vector<Corner> corners;
			std::vector<uint32_t> triangles;
			auto corner = [&](const glm::vec2& p, float z, const glm::vec3& normal) {
				corners.push_back({ glm::vec3((p - origin) * size, z), normal, uvOf(p) });
				return static_cast<uint32_t>(corners.size() - 1);
			};

			// Front (+Z)
			const uint32_t front = static_cast<uint32_t>(corners.size());
			for (const glm::vec2& p : fill.points) corner(p, 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
			for (uint32_t t : fill.triangles) triangles.push_back(front + t);

			if (options.depth > 0.0f) {
				const float back = -options.depth;
				// Back (-Z): the same triangles, turned round
				const uint32_t first = static_cast<uint32_t>(corners.size());
				for (const glm::vec2& p : fill.points) corner(p, back, glm::vec3(0.0f, 0.0f, -1.0f));
				for (size_t t = 0; t + 2 < fill.triangles.size(); t += 3) {
					triangles.insert(triangles.end(), { first + fill.triangles[t], first + fill.triangles[t + 2], first + fill.triangles[t + 1] });
				}
				// Walls: a quad per outline edge, facing out (the filled side is left of each edge)
				for (const Contour& outline : fill.outlines) {
					for (size_t i = 0; i < outline.size(); i++) {
						const glm::vec2 a = outline[i], b = outline[(i + 1) % outline.size()];
						const glm::vec2 edge = b - a;
						if (glm::dot(edge, edge) < 1e-14f) continue;
						const glm::vec3 normal = glm::normalize(glm::vec3(edge.y, -edge.x, 0.0f));
						const uint32_t a0 = corner(a, 0.0f, normal), b0 = corner(b, 0.0f, normal), a1 = corner(a, back, normal), b1 = corner(b, back, normal);
						triangles.insert(triangles.end(), { a0, a1, b1, a0, b1, b0 });
					}
				}
			}

			// Subdivided: each triangle into four, edges shared (by their corners' indices)
			int levels = std::clamp(options.subdivisions, 0, 6);
			while (levels > 0 && triangles.size() / 3 * (size_t(1) << (2 * levels)) > options.maxTriangles) levels--;
			for (int level = 0; level < levels; level++) {
				std::unordered_map<uint64_t, uint32_t> middles;
				auto middle = [&](uint32_t a, uint32_t b) {
					const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
					if (auto it = middles.find(key); it != middles.end()) return it->second;
					const Corner& ca = corners[a];
					const Corner& cb = corners[b];
					corners.push_back({ 0.5f * (ca.position + cb.position), ca.normal, 0.5f * (ca.uv + cb.uv) });
					const uint32_t index = static_cast<uint32_t>(corners.size() - 1);
					middles.emplace(key, index);
					return index;
				};
				std::vector<uint32_t> finer;
				finer.reserve(triangles.size() * 4);
				for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
					const uint32_t a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
					const uint32_t ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
					finer.insert(finer.end(), { a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca });
				}
				triangles.swap(finer);
			}

			// Displaced along the smoothed normal of each position (welded across faces: no cracks), by the height map
			// at the front's texture coordinates (the same for every corner at a position)
			if (options.displacement && options.displacementScale != 0.0f) {
				auto keyOf = [](const glm::vec3& p) {
					const glm::ivec3 q = glm::ivec3(glm::round(p * 65536.0f));
					return std::make_tuple(q.x, q.y, q.z);
				};
				std::map<std::tuple<int, int, int>, glm::vec3> smooth;
				for (const Corner& c : corners) smooth[keyOf(c.position)] += c.normal;
				std::map<std::tuple<int, int, int>, glm::vec3> offset;
				for (Corner& c : corners) {
					const auto key = keyOf(c.position);
					auto it = offset.find(key);
					if (it == offset.end()) {
						const glm::vec3 n = smooth[key];
						const glm::vec3 direction = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : c.normal;
						const glm::vec2 frontUv((c.position.x / size + origin.x - layout.min.x) / extent.x, 1.0f - (c.position.y / size + origin.y - layout.min.y) / extent.y);
						const float height = options.displacement->Sample(frontUv.x, frontUv.y);
						it = offset.emplace(key, direction * ((height - 0.5f) * options.displacementScale)).first;
					}
					c.position += it->second;
				}
				// Normals again, each face group smooth within itself (corners are not shared between front, back and walls)
				std::vector<glm::vec3> normals(corners.size(), glm::vec3(0.0f));
				for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
					const glm::vec3 n = glm::cross(corners[triangles[t + 1]].position - corners[triangles[t]].position,
						corners[triangles[t + 2]].position - corners[triangles[t]].position);
					for (int k = 0; k < 3; k++) normals[triangles[t + k]] += n;
				}
				for (size_t i = 0; i < corners.size(); i++) {
					if (glm::dot(normals[i], normals[i]) > 1e-20f) corners[i].normal = glm::normalize(normals[i]);
				}
			}

			const uint32_t first = static_cast<uint32_t>(vertices.size());
			for (const Corner& c : corners) {
				Vertex v{};
				v.position[0] = c.position.x; v.position[1] = c.position.y; v.position[2] = c.position.z; v.position[3] = 1.0f;
				v.normal[0] = c.normal.x; v.normal[1] = c.normal.y; v.normal[2] = c.normal.z;
				v.uv[0] = c.uv.x; v.uv[1] = c.uv.y;
				// Tangent along +x of the text (u grows with x)
				glm::vec3 tangent = glm::vec3(1.0f, 0.0f, 0.0f) - c.normal * c.normal.x;
				tangent = glm::dot(tangent, tangent) > 1e-8f ? glm::normalize(tangent) : glm::vec3(0.0f, 1.0f, 0.0f);
				v.tangent[0] = tangent.x; v.tangent[1] = tangent.y; v.tangent[2] = tangent.z; v.tangent[3] = 1.0f;
				vertices.push_back(v);
			}
			for (uint32_t t : triangles) indices.push_back(first + t);
		}
	}
}
