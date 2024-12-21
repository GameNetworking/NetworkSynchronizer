#pragma once

#include "core.h"
#include <cmath>

NS_NAMESPACE_BEGIN
class MathFunc {
public:
	// Constants
	static constexpr float TAU = 6.28318530718f; // 2π
	static constexpr float PI = 3.14159265359f;
	static constexpr float HALF_PI = 1.57079632679f; // π/2
	static constexpr float TWO_OVER_PI = 0.6366197723675814f; // 2 / π
	static constexpr float NEG_INF = -std::numeric_limits<float>::infinity();

	template <typename T>
	static bool is_equal_approx(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
		// Check for exact equality first, required to handle "infinity" values.
		if (a == b) {
			return true;
		}

		// Then check for approximate equality.
		return std::abs(a - b) <= epsilon;
	}

	template <typename T>
	static bool is_zero_approx(T a, T epsilon = std::numeric_limits<T>::epsilon()) {
		return std::abs(a) < epsilon;
	}

	template <typename T>
	static T vec2_length_squared(T x, T y) {
		return x * x + y * y;
	}

	template <typename T>
	static T vec2_length(T x, T y) {
		return std::sqrt(x * x + y * y);
	}

	template <typename T>
	static T vec3_length_squared(T x, T y, T z) {
		return x * x + y * y + z * z;
	}

	template <typename T>
	static T vec3_length(T x, T y, T z) {
		return std::sqrt(x * x + y * y + z * z);
	}

	template <typename T>
	static bool vec2_is_normalized(T x, T y) {
		return is_equal_approx(vec2_length(x, y), T(1.0));
	}

	template <typename T>
	static bool vec3_is_normalized(T x, T y, T z) {
		return is_equal_approx(vec3_length(x, y, z), T(1.0));
	}

	static float vec2_angle(float x, float y) {
		return atan2(y, x);
	}

	template <typename T>
	static void vec2_normalize(T &x, T &y) {
		T l = x * x + y * y;
		if (l != 0) {
			l = std::sqrt(l);
			x /= l;
			y /= l;
		} else {
			x = 0;
			y = 0;
		}
	}

	template <typename T>
	static void vec3_normalize(T &x, T &y, T &z) {
		T l = x * x + y * y + z * z;
		if (l != 0) {
			l = std::sqrt(l);
			x /= l;
			y /= l;
			z /= l;
		} else {
			x = 0;
			y = 0;
			z = 0;
		}
	}

	template <typename F>
	static F lerp(F a, F b, F alpha) {
		return a + alpha * (b - a);
	}

	template <typename T, typename T2, typename T3>
	static T clamp(const T m_a, const T2 m_min, const T3 m_max) {
		return m_a < m_min ? m_min : (m_a > m_max ? m_max : m_a);
	}

	// Ported from Jolt - Deterministic across platforms.
	static float sin(float in_x) {
		float s, c;
		CrossSinCosInternal(in_x, s, c);
		return s;
	}

	// Ported from Jolt - Deterministic across platforms.
	static float cos(float in_x) {
		float s, c;
		CrossSinCosInternal(in_x, s, c);
		return c;
	}

	// Ported from Jolt - Deterministic across platforms.
	static inline float atan(float v) {
		// Make argument positive, remember sign
		union {
			float f;
			uint32_t u;
		} tmp;
		tmp.f = v;
		uint32_t sign = tmp.u & 0x80000000U;
		float x = (sign == 0) ? v : -v;

		// If x > tan(π/8)
		float threshold1 = 0.4142135623730950f; // tan(π/8) ~ 0.4142135623
		bool greater1 = (x > threshold1);
		float x1 = (x - 1.0f) / (x + 1.0f);

		// If x > tan(3π/8)
		float threshold2 = 2.414213562373095f; // tan(3π/8) ~ 2.4142135624
		bool greater2 = (x > threshold2);
		// Add a tiny epsilon to avoid div by zero
		float x2 = -1.0f / (x + 1e-38f);

		// Apply first condition
		float x_sel = greater1 ? x1 : x;
		float y_sel = greater1 ? 0.78539816339f : 0.0f; // π/4 = 0.78539816339

		// Apply second condition
		float x_final = greater2 ? x2 : x_sel;
		float y_final = greater2 ? 1.57079632679f : y_sel; // π/2 = 1.57079632679

		// Polynomial approximation
		float z = x_final * x_final;
		float add = (((8.05374449538e-2f * z - 1.38776856032e-1f) * z + 1.99777106478e-1f) * z - 3.33329491539e-1f) * z * x_final + x_final;
		float result = y_final + add;

		// Put sign back
		union {
			float f;
			uint32_t u;
		} s;
		s.f = result;
		s.u ^= sign;
		return s.f;
	}

	// Ported from Jolt - Deterministic across platforms.
	static inline float atan2(float y, float x) {
		// If x=0
		if (x == 0.0f) {
			if (y > 0.0f)
				return 1.57079632679f; //  π/2
			if (y < 0.0f)
				return -1.57079632679f; // -π/2
			return 0.0f; //  (0,0)
		}

		// Sign bits / absolute values
		union {
			float f;
			uint32_t u;
		} sign_x, sign_y;
		sign_x.f = x;
		sign_y.f = y;
		uint32_t x_sign = sign_x.u & 0x80000000U;
		uint32_t y_sign = sign_y.u & 0x80000000U;
		float ax = (x_sign == 0) ? x : -x;
		float ay = (y_sign == 0) ? y : -y;

		// Always divide smaller by larger
		bool x_is_numer = (ax < ay);
		float numer = x_is_numer ? ax : ay;
		float denom = x_is_numer ? ay : ax;

		// Base atan of ratio
		float ratio = (denom < 1e-38f) ? 0.0f : (numer / denom);
		float angle = atan(ratio);

		// If we did x / y instead of y / x => angle = π/2 - angle
		if (x_is_numer)
			angle = 1.57079632679f - angle; // π/2

		// Map to correct quadrant:
		// If x<0 => angle = (y>=0)? (angle+π) : (angle-π)
		// Then flip sign if (x_sign ^ y_sign) is set
		if (x_sign != 0)
			angle = (y_sign == 0) ? (angle + 3.14159265359f) : (angle - 3.14159265359f);

		union {
			float f;
			uint32_t u;
		} ret;
		ret.f = angle;
		// Flip sign if (x_sign ^ y_sign) is set
		ret.u ^= (x_sign ^ y_sign);
		return ret.f;
	}

	// Function to compute the minimal difference between two angles in radians
	static inline float angle_difference(float angle1, float angle2) {
		float diff = fmod(angle2 - angle1 + PI, TAU);
		if (diff < 0)
			diff += TAU;
		diff -= PI;
		return std::fabs(diff);
	}

	// Ported from Jolt - Deterministic across platforms.
	static void CrossSinCosInternal(float in_x, float &out_sin, float &out_cos) {
		//------------------------------------------------------
		// 1) Normalize angle in [0..2π)
		//------------------------------------------------------
		float angle = in_x;
		//float angle = std::fmod(in_x, TAU);
		//if (angle < 0.0f)
		//	angle += TAU; // e.g. -5.76 => +0.52368 => 30 deg

		//------------------------------------------------------
		// 2) quadrant = int(angle * 2/π + 0.5)
		//------------------------------------------------------
		int quad = static_cast<int>(angle * TWO_OVER_PI + 0.5f);
		float fquad = static_cast<float>(quad);

		//------------------------------------------------------
		// 3) Subtract quadrant*(π/2) via cody–waite steps
		//------------------------------------------------------
		float x = ((angle - fquad * 1.5703125f)
					- fquad * 0.0004837512969970703125f)
				- fquad * 7.549789948768648e-8f;
		// x now in [-π/4, π/4], if angle was near a quadrant boundary

		float x2 = x * x;

		//------------------------------------------------------
		// 4) Polynomial expansions
		//------------------------------------------------------
		// sin_approx
		float sin_approx =
				((-1.9515295891e-4f * x2 + 8.3321608736e-3f) * x2 - 1.6666654611e-1f)
				* x2 * x
				+ x;

		// cos_approx
		float cos_approx =
				(((2.443315711809948e-5f * x2 - 1.388731625493765e-3f) * x2
					+ 4.166664568298827e-2f) * x2 * x2)
				- 0.5f * x2
				+ 1.0f;

		//------------------------------------------------------
		// 5) quadrant-based sign manipulations
		//    (no manual sign-flip for negative in_x!)
		//------------------------------------------------------
		int quadrant_bits = quad & 3;
		float s, c;
		switch (quadrant_bits) {
			case 0:
				s = sin_approx;
				c = cos_approx;
				break;
			case 1:
				s = cos_approx;
				c = -sin_approx;
				break;
			case 2:
				s = -sin_approx;
				c = -cos_approx;
				break;
			default: // 3
				s = -cos_approx;
				c = sin_approx;
				break;
		}

		out_sin = s;
		out_cos = c;
	}
};

NS_NAMESPACE_END