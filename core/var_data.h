#pragma once

#include "modules/network_synchronizer/core/core.h"
#include <memory>
#include <vector>

NS_NAMESPACE_BEGIN

// VarData is a struct that olds the value of a variable.
struct VarData {
	/// The type of the data, defined by the user.
	std::uint8_t type = 0;

	/// The data.
	union {
		void *ptr;

		bool boolean;
		std::int32_t i32;
		std::int64_t i64;
		float f32;
		double f64;

		struct {
			double x;
			double y;
			double z;
			double w;
		} vec;

		struct {
			std::int64_t ix;
			std::int64_t iy;
			std::int64_t iz;
			std::int64_t iw;
		} ivec;

		struct {
			double x;
			double y;
			double z;
			double w;
		} columns[4];

		struct {
			double x;
			double y;
			double z;
			double w;
		} rows[4];
	} data;

	// Eventually shared buffer across many `VarData`.
	std::shared_ptr<void> shared_buffer;

public:
	VarData();
	VarData(double x, double y = 0.0, double z = 0.0, double w = 0.0);

	VarData(const VarData &p_other) = delete;
	VarData &operator=(const VarData &p_other) = delete;

	VarData(VarData &&p_other);
	VarData &operator=(VarData &&p_other);

	static VarData make_copy(const VarData &p_other);
	void copy(const VarData &p_other);
};

NS_NAMESPACE_END
