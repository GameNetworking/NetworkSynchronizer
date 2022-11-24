from os import listdir
from os.path import isfile, join, isdir, exists

def create_debugger_header():
    
    f = open("__generated__debugger_ui.h", "w")
    f.write("#pragma once\n")
    f.write("\n")
    f.write("/// This is a generated file by `cpplize_debugger.py`, executed by `SCsub`.\n")
    f.write("/// \n")
    f.write("/// DO NOT EDIT this header.\n")
    f.write("/// If you want to modify this python code, you can simply change `debugger.py`\n")
    f.write("/// During the next compilation this header will be updated.\n")
    f.write("/// \n")
    f.write("/// HOWEVER! The file will not be copied into the `bin` folder unless you remove the\n")
    f.write("/// existing `bin/debugger.py` first; this algorithm prevents destroying eventual\n")
    f.write("/// changes made on that file.\n")
    f.write("\n")
    f.write("static const char __debugger_ui_code[] = R\"TheCodeRKS(")

    size = 0
    with open('./debugger_ui/debugger.py') as deb_f:
        for l in deb_f.readlines():
            l_utf8 = l.encode('utf-8')
            size += len(l_utf8)
            f.write(l);

    f.write("   )TheCodeRKS\";\n")
    f.write("static unsigned int __debugger_ui_code_size = "+str(size)+";\n")
    f.close()
