#pragma once

#include "core.h"
#include <cmath>

#define _USE_MATH_DEFINES
#include <math.h>

#define M_TAU (M_PI * 2.0)

NS_NAMESPACE_BEGIN

class MathFunc {
public:
	template <typename T>
	static bool is_equal_approx(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
		// Check for exact equality first, required to handle "infinity" values.
		if (a == b) {
			return true;
		}

		// Then check for approximate equality.
		return std::abs(a - b) < epsilon;
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
		return is_equal_approx(vec2_length_squared(x, y), T(1.0));
	}

	template <typename T>
	static bool vec3_is_normalized(T x, T y, T z) {
		return is_equal_approx(vec3_length_squared(x, y, z), T(1.0));
	}

	template <typename T>
	static bool vec2_angle(T x, T y) {
		return std::atan2(y, x);
	}

	template <typename F>
	static F lerp(F a, F b, F alpha) {
		return a + alpha * (b - a);
	}

	template <typename T, typename T2, typename T3>
	static T clamp(const T m_a, const T2 m_min, const T3 m_max) {
		return m_a < m_min ? m_min : (m_a > m_max ? m_max : m_a);
	}
};

NS_NAMESPACE_END