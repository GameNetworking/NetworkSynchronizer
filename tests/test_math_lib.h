#pragma once

#include "../core/var_data.h"

namespace NS_Test {

struct Vec3 {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	Vec3() = default;
	Vec3(float px, float py, float pz) :
			x(px), y(py), z(pz) {}

	operator NS::VarData() const;
	static Vec3 from(const NS::VarData &p_vd);

	Vec3 operator+(const Vec3 &p_vd) const;
	Vec3 &operator+=(const Vec3 &p_vd);

	Vec3 operator-(const Vec3 &p_vd) const;
	Vec3 &operator-=(const Vec3 &p_vd);

	Vec3 operator/(const Vec3 &p_vd) const;
	Vec3 &operator/=(const Vec3 &p_vd);

	Vec3 operator*(const Vec3 &p_vd) const;
	Vec3 &operator*=(const Vec3 &p_vd);

	Vec3 operator/(const float p_val) const;
	Vec3 &operator/=(const float &p_val);

	Vec3 operator*(const float p_val) const;
	Vec3 &operator*=(const float &p_val);

	float length() const;
	void normalize();
	Vec3 normalized() const;

	float distance_to(const Vec3 &p_v) const;
};

void test_math();
}; //namespace NS_Test
