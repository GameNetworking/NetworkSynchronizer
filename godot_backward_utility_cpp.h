#pragma once
/// Used to compile against Godot 3.x

/**
	@author AndreaCatania
*/

#include "core/engine.h"
#include "scene/main/viewport.h"
#define FLOAT REAL
#define STRING_NAME STRING
#define Callable(a, b) a, b
#define ObjectID CompatObjectID
#define SNAME(a) a