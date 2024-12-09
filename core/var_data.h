#pragma once

#include "core.h"
#include <functional>
#include <memory>

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
			float x;
			float y;
			float z;
			float w;
		} vec_f32;

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
			float x;
			float y;
			float z;
			float w;
		} columns_f32[4];

		struct {
			double x;
			double y;
			double z;
			double w;
		} columns[4];

		struct {
			float x;
			float y;
			float z;
			float w;
		} rows_f32[4];

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
	VarData(float x, float y = 0.0f, float z = 0.0f, float w = 0.0f);
	VarData(double x, double y = 0.0, double z = 0.0, double w = 0.0);

	VarData(const VarData &p_other) = delete;
	VarData &operator=(const VarData &p_other) = delete;

	VarData(VarData &&p_other);
	VarData &operator=(VarData &&p_other);

	static VarData make_copy(const VarData &p_other);
	void copy(const VarData &p_other);
};

class SynchronizerManager;

NS_NAMESPACE_END

#define NS_VarDataSetFunc std::function<void(class NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_app_object_handle, const std::string &p_name, const NS::VarData &p_val)>
#define NS_VarDataGetFunc std::function<void(const class NS::SynchronizerManager &p_synchronizer_manager, NS::ObjectHandle p_app_object_handle, const std::string &p_name, NS::VarData &r_val)>