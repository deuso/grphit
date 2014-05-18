#!/usr/bin/env python

import os
import re
import sys
import getopt

msg = {
      1  :  "EX_REQ",
      2  :  "SH_REQ",
      3  :  "INV_REQ",
      4  :  "FLUSH_REQ",
      5  :  "WB_REQ",
      6  :  "EX_REP",
      7  :  "SH_REP",
      8  :  "UPGRADE_REP",
      9  :  "INV_REP",
      10 :  "FLUSH_REP",
      11 :  "WB_REP",
      12 :  "DRAM_FETCH_REQ",
      13 :  "DRAM_STORE_REQ",
      14 :  "DRAM_FETCH_REP",
      15 :  "NULLIFY_REQ",
      16 :  "SPDIR_RD_REQ",
      17 :  "SPDIR_WR_REQ",
      18 :  "SPDIR_RD_REP",
      19 :  "SPDIR_WR_REP",
      20 :  "L2_SPDIR_REQ",
      21 :  "L2_SPDIR_REP"
}

components = {
      0  :  "INVALID",
      1  :  "CORE",
      2  :  "L1_ICACHE",
      3  :  "L1_DCACHE",
      4  :  "L2_CACHE",
      5  :  "DRAM_DIRECTORY",
      6  :  "SP_DIRECTORY",
      7  :  "DRAM_CNTLR"
}

def main():

   if len(sys.argv)>2:
      print "Please enter:\n"+sys.argv[0]+" file_name"
      sys.exit(1)
   
   myfile =  open(sys.argv[1])
   outfile = open("out_"+sys.argv[1], 'w')
   
   for line in myfile:
      #handle str reg-ex
      myre = re.compile("(?<=type\()(.*?)(?=\))")
      mp = myre.search(line)
      if mp:
         line = myre.sub(msg[int(mp.group(1))], line)
      #print outstr,
      outfile.write('%s' % (line))


if __name__  == "__main__":
   main()
