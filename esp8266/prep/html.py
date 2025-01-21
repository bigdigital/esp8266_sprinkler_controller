Import("env")
import os
import glob
from pathlib import Path
import sys
import pip


env.Append(CPPDEFINES=[
    ("SW_VERSION", env.StringifyMacro(env.GetProjectOption("custom_version")))
])


def install(package):
    if hasattr(pip, 'main'):
        pip.main(['install', package])
    else:
        pip._internal.main(['install', package])

try:
    import minify_html
except ImportError:
    install('minify_html')

filePath = 'src/web/'
try:
  print("==========================")
  print("Generating webpage")
  print("==========================")
  print("Preparing html.h file from source")
  print("  -insert header") 
  cpp_output = "#pragma once\n\n#include <Arduino.h>  // PROGMEM\n\n"
  print("  -insert html")

  for x in glob.glob(filePath+"*.html"):
   print("prozessing file:" + Path(x).stem)
   print(Path(x).stem)
   cpp_output += "static const char "+Path(x).stem+"[] PROGMEM = R\"rawliteral("
    # Read the HTML file
   with open(x, "r") as f:
    html_content = f.read()
    # Replace {{variable}} with %variable% and handle escaping %%
    html_content = html_content.replace("%", "%%")  # Ensure %% remains as %
    html_content = html_content.replace("{{", "%").replace("}}", "%")

    if env.GetProjectOption("build_type") == "debug":
     cpp_output += html_content 
    else:
     cpp_output += html_content  #disable compression until fixed that the compressor remove %VARIABLE%
    # cpp_output += minify_html.minify(html_content, minify_js=True)
   cpp_output += ")rawliteral\";\n"

   f = open ("./src/html.h", "w")
   f.write(cpp_output)
   f.close()
   print("==========================\n")

except SyntaxError as e:
  print(e)