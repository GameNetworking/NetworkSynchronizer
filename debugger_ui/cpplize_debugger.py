import os
import sys

def create_debugger_header(source_path):
    """
    Creates the generated header file for the debugger UI.

    Args:
        source_path (str): The path to the source directory where the header will be created.
    """
    # Define the path to the 'core' directory
    core_dir = os.path.join(source_path, "core")
    
    # Create the 'core' directory if it doesn't exist
    os.makedirs(core_dir, exist_ok=True)
    
    # Define the full path to the generated header file
    header_path = os.path.join(core_dir, "__generated__debugger_ui.h")
    
    try:
        with open(header_path, "w", encoding="utf-8") as f:
            f.write("#pragma once\n\n")
            f.write("/// This is a generated file by `cpplize_debugger.py`, executed by `SCsub`.\n")
            f.write("///\n")
            f.write("/// DO NOT EDIT this header.\n")
            f.write("/// If you want to modify this Python code, simply change `debugger.py`\n")
            f.write("/// During the next compilation, this header will be updated.\n")
            f.write("///\n")
            f.write("/// HOWEVER! The file will not be copied into the `bin` folder unless you remove the\n")
            f.write("/// existing `bin/debugger.py` first; this algorithm prevents destroying any\n")
            f.write("/// changes made to that file.\n\n")
            f.write("static const char __debugger_ui_code[] = R\"TheCodeRKS(\n")
            
            size = 0
            debugger_py_path = os.path.join(source_path, 'debugger_ui', 'debugger.py')
            
            if not os.path.isfile(debugger_py_path):
                print(f"Error: The file '{debugger_py_path}' was not found.")
                sys.exit(1)
            
            with open(debugger_py_path, encoding="utf-8") as deb_f:
                for line in deb_f:
                    line_utf8 = line.encode('utf-8')
                    size += len(line_utf8)
                    f.write(line)
            
            f.write(")TheCodeRKS\";\n")
            f.write(f"static unsigned int __debugger_ui_code_size = {size};\n")
        
        print(f"Header file successfully generated at '{header_path}'.")
    
    except IOError as e:
        print(f"Error writing the header file: {e}")
        sys.exit(1)

# The create_debugger_header function is called directly from SCsub,
# so there's no need for a main function handling sys.argv.

