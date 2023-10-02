#pragma once

#include "modules/network_synchronizer/core/core.h"
#include <memory>
#include <vector>

NS_NAMESPACE_BEGIN

class Buffer {
	std::uint32_t size;

public:
	std::uint8_t *data;

	Buffer(std::uint32_t p_size);
	~Buffer();
};

// VarData is a struct that olds the value of a variable.
struct VarData {
	/// The type of the data, defined by the user.
	std::uint8_t type = 0;

	/// The data.
	union {
		void *ptr = nullptr;

		std::int64_t integer;

		double flt;

		struct {
			double x;
			double y;
			double z;
			double w;
		};

		struct {
			double x;
			double y;
			double z;
			double w;
		} row[4];

		struct {
			std::int64_t ix;
			std::int64_t iy;
			std::int64_t iz;
			std::int64_t iw;
		};
	} data;

	// Eventually shared buffer across many `VarData`.
	std::shared_ptr<const Buffer> shared_buffer;

public:
	VarData() = default;

	VarData(const VarData &p_other) = delete;
	VarData &operator=(const VarData &p_other) = delete;

	VarData(VarData &&p_other);
	VarData &operator=(VarData &&p_other);

	void copy(const VarData &p_other);
};

NS_NAMESPACE_END
