/*************************************************************************/
/*  net_utilities.cpp                                                    */
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

#include "net_utilities.h"

#include "scene/main/node.h"

const uint32_t NetID_NONE = UINT32_MAX;

bool NetUtility::ChangeListener::operator==(const ChangeListener &p_other) const {
	return object_id == p_other.object_id && method == p_other.method;
}

NetUtility::VarData::VarData(const StringName &p_name) {
	var.name = p_name;
}

NetUtility::VarData::VarData(NetVarId p_id, const StringName &p_name, const Variant &p_val, bool p_skip_rewinding, bool p_enabled) :
		id(p_id),
		skip_rewinding(p_skip_rewinding),
		enabled(p_enabled) {
	var.name = p_name;
	var.value = p_val.duplicate(true);
}

bool NetUtility::VarData::operator==(const NetUtility::VarData &p_other) const {
	return var.name == p_other.var.name;
}

bool NetUtility::VarData::operator<(const VarData &p_other) const {
	return id < p_other.id;
}

bool NetUtility::NodeData::has_registered_process_functions() const {
	for (int process_phase = PROCESSPHASE_EARLY; process_phase < PROCESSPHASE_COUNT; ++process_phase) {
		if (functions[process_phase].size() > 0) {
			return true;
		}
	}
	return false;
}

bool NetUtility::NodeData::can_deferred_sync() const {
	return collect_epoch_func.is_valid() && apply_epoch_func.is_valid();
}

NetUtility::Snapshot::operator String() const {
	String s;
	s += "Snapshot input ID: " + itos(input_id);

	for (int net_node_id = 0; net_node_id < node_vars.size(); net_node_id += 1) {
		s += "\nNode Data: " + itos(net_node_id);
		for (int i = 0; i < node_vars[net_node_id].size(); i += 1) {
			s += "\n|- Variable: ";
			s += node_vars[net_node_id][i].name;
			s += " = ";
			s += String(node_vars[net_node_id][i].value);
		}
	}
	return s;
}

bool NetUtility::NodeChangeListener::operator==(const NodeChangeListener &p_other) const {
	return node_data == p_other.node_data && var_id == p_other.var_id;
}

bool NetUtility::RealtimeSyncGroup::is_node_list_changed() const {
	return nodes_list_changed;
}

const LocalVector<NetUtility::NodeData *> &NetUtility::RealtimeSyncGroup::get_nodes() const {
	return nodes;
}

const LocalVector<NetUtility::RealtimeSyncGroup::Change> &NetUtility::RealtimeSyncGroup::get_changes() const {
	return changes;
}

void NetUtility::RealtimeSyncGroup::mark_changes_as_notified() {
	changes.clear();
	nodes_list_changed = false;
}

void NetUtility::RealtimeSyncGroup::add_new_node(NodeData *p_node_data) {
	if (nodes.find(p_node_data) == -1) {
		if (changes.size() <= p_node_data->id) {
			changes.resize(p_node_data->id + 1);
		}

		changes[p_node_data->id].not_known_before = true;

		nodes.push_back(p_node_data);
		nodes_list_changed = true;

		for (int i = 0; i < int(p_node_data->vars.size()); ++i) {
			notify_new_variable(p_node_data, p_node_data->vars[i].var.name);
		}
	}
}

void NetUtility::RealtimeSyncGroup::remove_node(NodeData *p_node_data) {
	nodes.erase(p_node_data);
	nodes_list_changed = true;
}

void NetUtility::RealtimeSyncGroup::notify_new_variable(NodeData *p_node_data, const StringName &p_var_name) {
	if (changes.size() <= p_node_data->id) {
		changes.resize(p_node_data->id + 1);
	}

	changes[p_node_data->id].vars.insert(p_var_name);
	changes[p_node_data->id].uknown_vars.insert(p_var_name);
}

void NetUtility::RealtimeSyncGroup::notify_variable_changed(NodeData *p_node_data, const StringName &p_var_name) {
	if (changes.size() <= p_node_data->id) {
		changes.resize(p_node_data->id + 1);
	}

	changes[p_node_data->id].vars.insert(p_var_name);
}
