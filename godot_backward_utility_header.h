#pragma once

/// Used to compile against Godot 3.x

/**
	@author AndreaCatania
*/

#include "core/typedefs.h"

struct CompatObjectID {
	uint64_t id;

	CompatObjectID() :
			id(0){};
	CompatObjectID(uint64_t p_id) :
			id(p_id){};

	operator uint64_t() const { return id; }

	bool is_valid() const { return id != 0; }
	bool is_null() const { return id == 0; }
};