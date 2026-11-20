#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Fox {

	namespace Text {

		// Vector text from TrueType / OpenType fonts (.ttf, .otf; stb_truetype): each glyph's outline (its quadratic and
		// cubic curves, flattened finely enough for the size it is drawn at), laid out with the font's advances and
		// kerning, then filled by triangulating the outlines (holes included: the inside of an "o"). No bitmaps: the
		// edges stay sharp at any size, on screen (GameUI) and as geometry in the world (flat or extruded 3D text).
		//
		// Coordinates: em units (1 = the font size), x right, y up, the first line's baseline at y = 0.

		using Contour = std::vector<glm::vec2>; // A closed polyline (the last point joins the first)

		class Font {
		public:
			// The font in a file, loaded once (nullptr if it cannot be read)
			static std::shared_ptr<Font> Get(const std::string& file);
			~Font();

			float Ascent() const { return ascent; }   // Above the baseline, em units
			float Descent() const { return descent; } // Below it (negative)
			float LineGap() const { return lineGap; }

			// A glyph's outline, flattened to within tolerance (em units), and how far it advances
			struct Glyph {
				std::vector<Contour> contours;
				float advance = 0.0f;
			};
			const Glyph& GetGlyph(uint32_t codepoint, float tolerance);
			float Kerning(uint32_t left, uint32_t right) const;

		private:
			Font() = default;
			std::vector<unsigned char> data;
			void* info = nullptr; // stbtt_fontinfo
			float scale = 1.0f;   // Font units to em
			float ascent = 0.8f, descent = -0.2f, lineGap = 0.0f;
			std::map<std::pair<uint32_t, int>, Glyph> glyphs; // By codepoint and tolerance level
		};

		enum class Align { Left, Center, Right };

		struct Layout {
			std::vector<Contour> contours; // Every glyph's outline, placed
			glm::vec2 min{ 0.0f }, max{ 0.0f };  // Bounds of the outlines
			int lines = 1;
		};
		// UTF-8 text laid out in lines ('\n'), aligned. tolerance: flattening, em units (e.g. 0.5 pixel / size in pixels).
		Layout LayOut(Font& font, const std::string& text, Align align, float lineSpacing, float letterSpacing, float tolerance);

		// The filled shape: triangles over points (counter-clockwise, y up), and the outline contours (outer ones
		// counter-clockwise, holes clockwise: the filled side is on the left of each edge)
		struct Fill {
			std::vector<glm::vec2> points;
			std::vector<uint32_t> triangles;
			std::vector<Contour> outlines;
		};
		Fill Triangulate(const std::vector<Contour>& contours);

		// Geometry for the world (or the screen, as 3D): in the XY plane facing +Z, centred on the pivot (0..1 of the
		// bounds), size: metres per em.
		//   depth 0: flat, one sided triangles (the path tracer shades both sides)
		//   depth > 0: extruded: front at z = 0, back at -depth, walls between (hard edges)
		// subdivisions: each triangle split in 4 that many times (for displacement). Texture coordinates: the text's
		// bounds over 0..1 (front, back and walls alike, so a mask cuts through the walls too).
		// displacement: a height map (0..1, 0.5 none) sampled at those coordinates, moving every point along its
		// smoothed normal by (height - 0.5) * displacementScale metres; welded, so no cracks open along edges.
		struct MeshOptions {
			float size = 1.0f;
			glm::vec2 pivot{ 0.5f, 0.5f };
			float depth = 0.0f;
			int subdivisions = 0;
			const Fox::Scene::GrayImage* displacement = nullptr;
			float displacementScale = 0.0f;
			size_t maxTriangles = 2'000'000;
		};
		void BuildMesh(const Layout& layout, const Fill& fill, const MeshOptions& options,
			std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<uint32_t>& indices);

		// Decodes UTF-8 (invalid bytes are skipped)
		std::vector<uint32_t> DecodeUtf8(const std::string& text);

		// Font files (.ttf, .otf) under folder, forward slashes, sorted
		std::vector<std::string> ListFonts(const std::string& folder = "Fonts");
		// The font a text without one uses: the first under Fonts/ (looked up once; empty if there is none)
		const std::string& DefaultFont();
	}
}
