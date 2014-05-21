#!/usr/bin/env python

import re
import os

fp = open('gdb_cmds', 'w')
portre = re.compile('Pausing to attach to pid (\d+)')
portsf = re.compile('(add-symbol-file /w*)')

while 1:
   line = raw_input()
   print line
   mobj = portre.search(line)
   if mobj:
      fp.write('attach %s%s' %(mobj.group(1),os.linesep))
   nobj = portsf.search(line)
   if nobj:
      fp.write('%s' %(line))
      break

#system("echo gdb ../pin-2.13-61206-gcc.4.4.7-linux/intel64/bin/pinbin $pid -s $cmd");
#system("gnome-terminal --maximize -e 'sudo gdb ../pin-2.13-61206-gcc.4.4.7-linux/intel64/bin/pinbin $pid  $cmd' &");
os.system("gnome-terminal --maximize -e 'gdb ../pin-2.13-62141-gcc.4.4.7-linux/intel64/bin/pinbin --command gdb_cmds' &")

#os.system("sleep 3")
#os.system("rm gdb_cmds")
