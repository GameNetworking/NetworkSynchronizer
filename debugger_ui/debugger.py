import PySimpleGUI as sg
import json
from os import listdir
from os.path import isfile, join, isdir, exists

# ------------------------------------------------------------------------------------------- Config
font_size = 9


# ---------------------------------------------------------------------------------------- Utilities
def load_json(path):
	if exists(path):
		with open(path) as f:
			return json.load(f)
	return {}


def compare_arrays(arr_a, arr_b):
	if len(arr_a) != len(arr_b):
		return False
	
	for i in range(len(arr_a)):
		if arr_a[i] != arr_b[i]:
			return False
	
	return True


# Fetches the directories containing the frame info.
directories = [f for f in listdir("./") if (isdir(join("./", f)) and len(listdir(join("./", f))) > 0)]

# ----------------------------------------------------------------------------------------------- UI
def create_layout():

	# Fetch the frame count from the dirs.
	frame_count = 0
	frames_description = {}
	for dir in directories:
		dir_path = join("./", dir)
		file_names = [f for f in listdir(dir_path) if isfile(join(dir_path, f))]
		for file_name in file_names:
			file_extension_index = file_name.find("fd-")
			if file_extension_index == 0:
				# This file contains information about the frame.
				# Extract the frame index
				frame_index = int(file_name[3:file_name.index(".json")])
				frame_count = max(frame_count, frame_index)

				file_path = join(dir_path, file_name)
				file_json = load_json(file_path)
				if "frame_summary" in file_json:
					if frame_index in frames_description:
						frames_description[frame_index] += " " + file_json["frame_summary"]
					else:
						frames_description[frame_index] = file_json["frame_summary"]
				else:
					frames_description[frame_index] = ""


	# --- UI - Compose timeline ---
	frame_list_values = []
	for frame_index in range(frame_count):
		frame_description = frames_description[frame_index]
		frame_list_values.append("# " + str(frame_index) + " - " + frame_description)

	# Release this array, we don't need anylonger.
	frames_description.clear()

	frames_list = sg.Listbox(frame_list_values, key="FRAMES_LIST", size = [45, 30], enable_events=True, horizontal_scroll=True, select_mode=sg.LISTBOX_SELECT_MODE_BROWSE)
	frames_list = sg.Frame("Frames", layout=[[frames_list]], vertical_alignment="top")

	# --- UI - Compose frame detail ---
	# Node list
	nodes_list_listbox = sg.Listbox([], key="NODE_LIST",  size = [45, 0], enable_events=True, horizontal_scroll=True, expand_y=True, expand_x=True, select_mode=sg.LISTBOX_SELECT_MODE_MULTIPLE)
	nodes_list_listbox = sg.Frame("Nodes", layout=[[nodes_list_listbox]], vertical_alignment="top", expand_y=True, expand_x=True);

	# Selected nodes title.
	node_tile_txt = sg.Text("", key="FRAME_SUMMARY", font="Any, " + str(font_size - 1), justification="left", border_width=1, text_color="dark red")

	# States table
	table_status_header = ["name"]
	table_status_widths = [30]
	for dir in directories:
		table_status_header.append("Begin ["+dir+"]")
		table_status_widths.append(30)

	for dir in directories:
		table_status_header.append("End ("+dir+")")
		table_status_widths.append(30)

	table_status = sg.Table([], table_status_header, key="TABLE_STATUS", justification='left', auto_size_columns=False, col_widths=table_status_widths, vertical_scroll_only=False, num_rows=38)
	table_status = sg.Frame("States", layout=[[table_status]], vertical_alignment="top")

	# Messages table
	tables_logs = []
	for dir in directories:
		tables_logs.append(sg.Frame("Log: " + dir, layout=[[sg.Table([], [" #", "Log"], key=dir+"_TABLE_LOG", justification='left', auto_size_columns=False, col_widths=[4, 70], vertical_scroll_only=False, num_rows=25)]], vertical_alignment="top"))

	logs = sg.Frame("Messages", layout=[tables_logs], vertical_alignment="top")

	# --- UI - Main Window ---
	layout = [
	  [
			sg.Frame("", [[frames_list], [nodes_list_listbox]], vertical_alignment="top", expand_y=True),
			sg.Frame("Frame detail", [[node_tile_txt], [table_status], [logs]], key="FRAME_FRAME_DETAIL", vertical_alignment="top")
		],
		[
			sg.Button("Exit")
		]
	]

	return layout


# ------------------------------------------------------------------------------------ Create window
window = sg.Window(title="Network Synchronizer Debugger.", layout=create_layout(), margins=(5, 5), font="Any, " + str(font_size), resizable=True)


# ----------------------------------------------------------------------------------- Event handling
frame_data = {}
nodes_list = []
selected_nodes = []

while True:
	event, event_values = window.read()

	# EVENT: Close the program.
	if event == "Exit" or event == sg.WIN_CLOSED:
		window.close()
		break

	# EVENT: Show frame
	if event == "FRAMES_LIST":
		window["NODE_LIST"].update([])

		if event_values["FRAMES_LIST"] != []:
			frame_description = event_values["FRAMES_LIST"][0]
			selected_frame_index = int(frame_description[2:frame_description.index(" - ")])
			print("Show frame: ", selected_frame_index)

			frame_data = {}
			nodes_list = []
			for dir in directories:
				frame_file_path = join("./", dir, "fd-" + str(selected_frame_index) + ".json")
				if exists(frame_file_path):
					frame_data_json = load_json(frame_file_path)
					frame_data[dir] = frame_data_json

					for node_path in frame_data_json["begin_state"]:
						if node_path not in nodes_list:
							# Add this node to the nodelist
							nodes_list.append(node_path)

					for node_path in frame_data_json["end_state"]:
						if node_path not in nodes_list:
							# Add this node to the nodelist
							nodes_list.append(node_path)

				else:
					frame_data[dir] = {}

			# Update the node list.
			window["NODE_LIST"].update(nodes_list)

		if len(selected_nodes) == 0:
			if len(nodes_list) > 0:
				selected_nodes = [nodes_list[0]]
			else:
				selected_nodes = []

		window["NODE_LIST"].set_value(selected_nodes)
		event = "NODE_LIST"
		event_values = {"NODE_LIST": selected_nodes}

	# EVENT: Show node data
	if event == "NODE_LIST":

		window["FRAME_SUMMARY"].update("")
		window["TABLE_STATUS"].update([])

		for dir_name in directories:
			window[dir_name + "_TABLE_LOG"].update([["", "[Nothing for this node]"]])

		if event_values["NODE_LIST"] != []:
			instances_count = len(directories)
			row_size = 1 + (instances_count * 2)

			# Compose the status table
			states_table_values = []
			states_row_colors = []
			table_logs = {}
			log_row_colors = {}

			selected_nodes = event_values["NODE_LIST"]

			for node_path in selected_nodes:

				# First collects the var names
				vars_names = ["***"]
				for dir in directories:
					if "begin_state" in frame_data[dir]:
						if node_path in frame_data[dir]["begin_state"]:
							for var_name in frame_data[dir]["begin_state"][node_path]:
								if var_name not in vars_names:
									vars_names.append(var_name)

					if "end_state" in frame_data[dir]:
						if node_path in frame_data[dir]["end_state"]:
							for var_name in frame_data[dir]["end_state"][node_path]:
								if var_name not in vars_names:
									vars_names.append(var_name)

				vars_names.append("---")

				# Add those to the table.
				for var_name in vars_names:

					# Initializes the row.
					row = [""] * row_size
					row_index = len(states_table_values)

					# Special rows
					if var_name == "***":
						# This is a special row to signal the start of a new node data
						row[0] = node_path
						states_table_values.append(row)
						states_row_colors.append((row_index, "black"))
						continue
					elif var_name == "---":
						# This is a special row to signal the end of the node data
						states_table_values.append(row)
						continue

					row[0] = var_name.replace("*", "🔄")

					# Set the row data.
					for dir_i, dir_name in enumerate(directories):
						if "begin_state" in frame_data[dir_name]:
							if node_path in frame_data[dir_name]["begin_state"]:
								if var_name in frame_data[dir_name]["begin_state"][node_path]:
									#print(1, " + (", instances_count, " * 0) + ", dir_i)
									row[1 + (instances_count * 0) + dir_i] = str(frame_data[dir_name]["begin_state"][node_path][var_name])

						if "end_state" in frame_data[dir_name]:
							if node_path in frame_data[dir_name]["end_state"]:
								if var_name in frame_data[dir_name]["end_state"][node_path]:
									#print(1, " + (", instances_count, " * 1) + ", dir_i)
									row[1 + (instances_count * 1) + dir_i] = str(frame_data[dir_name]["end_state"][node_path][var_name])

					# Check if different, so mark a worning.
					for state_index in range(2):
						for i in range(instances_count - 1):
							if row[1 + (state_index*instances_count) + i + 0] != row[1 + (state_index*instances_count) + i + 1]:
								row[1 + (state_index*instances_count) + i + 0] = "⚠️ " + row[1 + (state_index*instances_count) + i + 0]
								row[1 + (state_index*instances_count) + i + 1] = "⚠️ " + row[1 + (state_index*instances_count) + i + 1]
								states_row_colors.append((row_index, "dark salmon"))
								break

					states_table_values.append(row)

				# Compose the log
				for dir_name in directories:
					if "node_log" in frame_data[dir_name]:
						if node_path in frame_data[dir_name]["node_log"]:

							table_logs[dir_name] = table_logs.get(dir_name, [])
							log_row_colors[dir_name] = log_row_colors.get(dir_name, [])

							table_logs[dir_name] += [["", node_path]]
							log_row_colors[dir_name] += [(len(table_logs[dir_name]) - 1, "black")]

							for log_index, val in enumerate(frame_data[dir_name]["node_log"][node_path]):

								# Append the log
								table_logs[dir_name] += [[log_index, val]]
								row_index = len(table_logs[dir_name]) - 1

								# Check if this line should be colored
								if val.find("[WARNING]") == 0:
									log_row_colors[dir_name] += [(row_index, "dark salmon")]

								elif val.find("[ERROR]") == 0:
									log_row_colors[dir_name] += [(row_index, "red")]

								elif val.find("[WRITE]") == 0:
									log_row_colors[dir_name] += [(row_index, "cadet blue")]

								elif val.find("[READ]") == 0:
									log_row_colors[dir_name] += [(row_index, "medium aquamarine")]

							table_logs[dir_name] += [["", ""]]

			window["FRAME_FRAME_DETAIL"].update("Frame " + str(selected_frame_index) + " details")
			window["TABLE_STATUS"].update(states_table_values, row_colors=states_row_colors)

			frame_summary = ""
			for dir_name in directories:
				if dir_name in frame_data:
					frame_summary += frame_data[dir_name]["frame_summary"]
				if dir_name in table_logs:
					window[dir_name + "_TABLE_LOG"].update(table_logs[dir_name], row_colors=log_row_colors[dir_name]);
			
			# Check if write and read databuffer is the same.
			for dir_name in directories:
				if dir_name in frame_data and (dir_name == "nonet" or dir_name == "client"):
					are_the_same = compare_arrays(frame_data[dir_name]["data_buffer_writes"], frame_data[dir_name]["data_buffer_reads"])
					if not are_the_same:
						if frame_summary != "":
							frame_summary += "\n"
						frame_summary += "[BUG] The DataBuffer written by `_collect_inputs` and read by `_controller_process` is different. Both should be exactly the same."

			# Check if the server read correctly the received buffer.
			if "server" in frame_data and "client" in frame_data:
				are_the_same = compare_arrays(frame_data["server"]["data_buffer_reads"], frame_data["client"]["data_buffer_reads"])
				if not are_the_same:
					if frame_summary != "":
						frame_summary += "\n"
					frame_summary += "[BUG] The DataBuffer written by the client is different from the one read on the server."

			# Check if the client sent the correct inputs to the server.
			if "client" in frame_data:
				for other_frame_index, is_similar in frame_data["client"]["are_inputs_different_results"].items():
					other_frame_index = int(other_frame_index)
					other_file_path = join("./", "client", "fd-" + str(other_frame_index) + ".json")
					other_frame_json = load_json(other_file_path)
					if "data_buffer_reads" in other_frame_json:
						is_really_similar = compare_arrays(other_frame_json["data_buffer_reads"], frame_data["client"]["data_buffer_reads"])
						if is_really_similar != is_similar:
							if frame_summary != "":
								frame_summary += "\n"
							frame_summary += "[BUG] The function `_are_inputs_different` doesn't seems to work:\n"
							frame_summary += "      As the inputs read on the frame " + str(frame_data["client"]["frame"]) + " and " + str(other_frame_index) + " are " + ("SIMILAR" if is_really_similar else "DIFFERENT") +" but the net sync considered it "+ ("SIMILAR" if is_similar else "DIFFERENT")

			window["FRAME_SUMMARY"].update(frame_summary)


# --------------------------------------------------------------------------------------------- Exit
