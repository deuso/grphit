#!/usr/bin/env python

import os
import re
import sys
import getopt

def main():

   if len(sys.argv)>2:
      print "Please enter:\n"+sys.argv[0]+" file_name"
      sys.exit(1)
   
   myfile =  open(sys.argv[1])
   outfile =  open(sys.argv[1]+'XX', 'w')
   
   starttime = 0

   for i, line in enumerate(myfile):
      #handle str reg-ex
      myre = re.compile("(^\d+\s*)")
      mp = myre.search(line)
      if mp:
         if(i== 0):
            starttime = int(mp.group(1))
            line = myre.sub('0\t\t', line)
            print starttime
         else:
            deltime = int(mp.group(1))-starttime
            if(deltime<100):
               line = myre.sub(str(deltime)+'\t\t', line)
            else:
               line = myre.sub(str(deltime)+'\t', line)
            print deltime
      #print outstr,
      outfile.write('%s' % (line))

   os.system("mv " + sys.argv[1]+'XX '+ sys.argv[1])


if __name__  == "__main__":
   main()
