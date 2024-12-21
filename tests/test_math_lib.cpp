#include "test_math_lib.h"

#include "../core/net_math.h"
#include "NetworkSynchronizer/core/ensure.h"

namespace NS_Test {
Vec3::operator NS::VarData() const {
	NS::VarData vd;
	vd.data.vec_f32.x = x;
	vd.data.vec_f32.y = y;
	vd.data.vec_f32.z = z;
	return vd;
}


Vec3 Vec3::from(const NS::VarData &p_vd) {
	Vec3 v;
	v.x = p_vd.data.vec_f32.x;
	v.y = p_vd.data.vec_f32.y;
	v.z = p_vd.data.vec_f32.z;
	return v;
}

Vec3 Vec3::operator+(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v += p_vd;
	return v;
}

Vec3 &Vec3::operator+=(const Vec3 &p_vd) {
	x += p_vd.x;
	y += p_vd.y;
	z += p_vd.z;
	return *this;
}

Vec3 Vec3::operator-(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v -= p_vd;
	return v;
}

Vec3 &Vec3::operator-=(const Vec3 &p_vd) {
	x -= p_vd.x;
	y -= p_vd.y;
	z -= p_vd.z;
	return *this;
}

Vec3 Vec3::operator/(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v /= p_vd;
	return v;
}

Vec3 &Vec3::operator/=(const Vec3 &p_vd) {
	x /= p_vd.x;
	y /= p_vd.y;
	z /= p_vd.z;
	return *this;
}

Vec3 Vec3::operator*(const Vec3 &p_vd) const {
	Vec3 v = *this;
	v *= p_vd;
	return v;
}

Vec3 &Vec3::operator*=(const Vec3 &p_vd) {
	x *= p_vd.x;
	y *= p_vd.y;
	z *= p_vd.z;
	return *this;
}

Vec3 Vec3::operator/(const float p_val) const {
	Vec3 v = *this;
	v /= p_val;
	return v;
}

Vec3 &Vec3::operator/=(const float &p_val) {
	x /= p_val;
	y /= p_val;
	z /= p_val;
	return *this;
}

Vec3 Vec3::operator*(const float p_val) const {
	Vec3 v = *this;
	v *= p_val;
	return v;
}

Vec3 &Vec3::operator*=(const float &p_val) {
	x *= p_val;
	y *= p_val;
	z *= p_val;
	return *this;
}

float Vec3::length() const {
	return std::sqrt(x * x + y * y + z * z);
}

void Vec3::normalize() {
	float l = length();
	if (l > 0.0001) {
		*this /= l;
	} else {
		x = 0.0;
		y = 0.0;
		z = 0.0;
	}
}

Vec3 Vec3::normalized() const {
	Vec3 v = *this;
	v.normalize();
	return v;
}

float Vec3::distance_to(const Vec3 &p_v) const {
	Vec3 v = *this;
	v -= p_v;
	return v.length();
}

void test_math_trigonometry() {
	// We'll test angles from -2π to +2π
	constexpr int STEPS = 100000;
	constexpr float START_ANGLE = -20.0f * NS::MathFunc::PI;
	constexpr float END_ANGLE = 20.0f * NS::MathFunc::PI;
	float step_size = (END_ANGLE - START_ANGLE) / STEPS;

	// For atan2 tests, we do y and x in [-2π .. 2π], but let's just re-use the loop
	float max_diff_sin = 0.0f, sum_diff_sin = 0.0f;
	float max_diff_cos = 0.0f, sum_diff_cos = 0.0f;
	float max_diff_at2 = 0.0f, sum_diff_at2 = 0.0f;
	int count_at2 = 0;

	// Sin/Cos test
	float angle = START_ANGLE;
	for (int i = 0; i <= STEPS; ++i) {
		float cross_s = NS::MathFunc::sin(angle);
		float cross_c = NS::MathFunc::cos(angle);
		float std_s = std::sin(angle);
		float std_c = std::cos(angle);

		float ds = std::fabs(cross_s - std_s);
		float dc = std::fabs(cross_c - std_c);
		if (ds > max_diff_sin)
			max_diff_sin = ds;
		if (dc > max_diff_cos)
			max_diff_cos = dc;
		sum_diff_sin += ds;
		sum_diff_cos += dc;

		angle += step_size;
	}

	// ATan2 test
	// We'll just do a grid in x,y in [-2, +2], for instance
	const int GRID_STEPS = 501;
	float grid_min = -2.0f;
	float grid_max = 2.0f;
	float gx_step = (grid_max - grid_min) / (GRID_STEPS - 1);

	for (int ix = 0; ix < GRID_STEPS; ++ix) {
		float px = grid_min + ix * gx_step;
		for (int iy = 0; iy < GRID_STEPS; ++iy) {
			float py = grid_min + iy * gx_step;

			float cross_at = NS::MathFunc::atan2(py, px);
			float std_at = std::atan2(py, px);
			float diff = std::fabs(NS::MathFunc::angle_difference(cross_at, std_at));

			if (diff > max_diff_at2)
				max_diff_at2 = diff;
			sum_diff_at2 += diff;

			++count_at2;
		}
	}

	NS_ASSERT_COND(max_diff_sin < 0.005);
	NS_ASSERT_COND(max_diff_cos < 0.005);
	NS_ASSERT_COND(max_diff_at2 < 0.0001);
}

void test_math() {
	test_math_trigonometry();
}
}; //namespace NS_Test