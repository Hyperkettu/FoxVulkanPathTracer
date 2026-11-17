#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Uniform Catmull-Rom segment through p1 (t = 0) and p2 (t = 1)
			template<class T>
			T CatmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
				const float t2 = t * t, t3 = t2 * t;
				return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
			}

			// A closed curve in a plane through its control points: the shape of a trunk's cross-section.
			// Scale 1 is the trunk's radius; points go counter-clockwise (either way works, it is detected).
			struct ClosedSpline {
				std::vector<glm::vec2> points;

				static ClosedSpline Circle(int count = 8) {
					ClosedSpline spline;
					for (int i = 0; i < count; i++) {
						const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(count);
						spline.points.emplace_back(std::cos(angle), std::sin(angle));
					}
					return spline;
				}

				// t in [0, 1) goes once around; each control point covers an equal share
				glm::vec2 Evaluate(float t) const {
					const int n = static_cast<int>(points.size());
					if (n == 0) return glm::vec2(std::cos(glm::two_pi<float>() * t), std::sin(glm::two_pi<float>() * t));
					if (n < 3) return points[0];

					t = t - std::floor(t);
					const float scaled = t * static_cast<float>(n);
					const int i = std::min(static_cast<int>(scaled), n - 1);
					const float f = scaled - static_cast<float>(i);
					auto at = [&](int k) { return points[((k % n) + n) % n]; };
					return CatmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), f);
				}
			};

			// An open curve through its control points, t = 0 at the first and 1 at the last. The ends are
			// extended by mirroring, so the curve leaves the first point heading towards the second.
			template<class T>
			struct OpenSpline {
				std::vector<T> points;

				T Evaluate(float t) const {
					const int n = static_cast<int>(points.size());
					if (n == 0) return T(0.0f);
					if (n == 1) return points[0];

					t = glm::clamp(t, 0.0f, 1.0f);
					const float scaled = t * static_cast<float>(n - 1);
					const int i = std::min(static_cast<int>(scaled), n - 2);
					const float f = scaled - static_cast<float>(i);
					auto at = [&](int k) {
						if (k < 0) return 2.0f * points[0] - points[1];
						if (k >= n) return 2.0f * points[n - 1] - points[n - 2];
						return points[k];
					};
					return CatmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), f);
				}
			};
		}
	}
}
