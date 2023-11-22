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

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "data_buffer.h"
#include "godot4/gd_networked_controller.h"
#include "godot4/gd_scene_synchronizer.h"
#include "input_network_encoder.h"
#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "scene_synchronizer_debugger.h"

#ifdef DEBUG_ENABLED
#include "tests/tests.h"
#endif

void initialize_network_synchronizer_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		GDREGISTER_CLASS(DataBuffer);
		GDREGISTER_CLASS(GdNetworkedController);
		GDREGISTER_CLASS(GdSceneSynchronizer);
		GDREGISTER_CLASS(InputNetworkEncoder);

		memnew(SceneSynchronizerDebugger);
		Engine::get_singleton()->add_singleton(Engine::Singleton("SceneSynchronizerDebugger", SceneSynchronizerDebugger::singleton()));

		GLOBAL_DEF("NetworkSynchronizer/debug_server_speedup", false);
		GLOBAL_DEF("NetworkSynchronizer/log_debug_rewindings", false);
		GLOBAL_DEF("NetworkSynchronizer/log_debug_warnings_and_messages", true);
		GLOBAL_DEF("NetworkSynchronizer/log_debug_nodes_relevancy_update", false);
		GLOBAL_DEF("NetworkSynchronizer/debugger/dump_enabled", false);
		GLOBAL_DEF("NetworkSynchronizer/debugger/dump_classes", Array());
		GLOBAL_DEF("NetworkSynchronizer/debugger/log_debug_fps_warnings", true);
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
#ifdef DEBUG_ENABLED
		List<String> args = OS::get_singleton()->get_cmdline_args();
		if (args.find("--editor")) {
			NS_GD_Test::test_var_data_conversin();
			NS_Test::test_all();
		}
#endif
	}
}

void uninitialize_network_synchronizer_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	if (SceneSynchronizerDebugger::singleton()) {
		memdelete(SceneSynchronizerDebugger::singleton());
	}
}
