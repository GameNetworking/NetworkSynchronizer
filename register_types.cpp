/*************************************************************************/
/*  register_types.cpp                                                   */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

/**
	@author AndreaCatania
*/

#include "register_types.h"

#include "core/engine.h"
#include "data_buffer.h"
#include "networked_controller.h"
#include "scene_diff.h"
#include "scene_synchronizer.h"
#include "scene_synchronizer_debugger.h"
#include "input_network_encoder.h"

void register_network_synchronizer_types() {
	ClassDB::register_class<DataBuffer>();
	ClassDB::register_class<SceneDiff>();
	ClassDB::register_class<NetworkedController>();
	ClassDB::register_class<SceneSynchronizer>();
	ClassDB::register_class<SceneSynchronizerDebugger>();
	ClassDB::register_class<InputNetworkEncoder>();

	memnew(SceneSynchronizerDebugger);
	Engine::get_singleton()->add_singleton(Engine::Singleton("SceneSynchronizerDebugger", SceneSynchronizerDebugger::singleton()));

	GLOBAL_DEF("NetworkSynchronizer/debug_server_speedup", false);
	GLOBAL_DEF("NetworkSynchronizer/debug_doll_speedup", false);
	GLOBAL_DEF("NetworkSynchronizer/log_debug_warnings_and_messages", true);
	GLOBAL_DEF("NetworkSynchronizer/debugger/dump_enabled", false);
	GLOBAL_DEF("NetworkSynchronizer/debugger/dump_classes", Array());
	GLOBAL_DEF("NetworkSynchronizer/debugger/log_debug_fps_warnings", true);
}

void unregister_network_synchronizer_types() {
	memdelete(SceneSynchronizerDebugger::singleton());
}
