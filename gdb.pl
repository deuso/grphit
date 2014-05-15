#!/usr/bin/perl

open FP,">","gdb_cmds";

while(<>) {
   print $_;
   if(/Pausing to attach to pid (\S+)/) {
      print FP  "attach ".$1."\n";
   }
   elsif(/add-symbol-file /) {
      print FP  $_;
      last;
#break;
   }
}
close FP;
#system("sleep 1");

#system("echo gdb ../pin-2.13-61206-gcc.4.4.7-linux/intel64/bin/pinbin $pid -s $cmd");
#system("gnome-terminal --maximize -e 'sudo gdb ../pin-2.13-61206-gcc.4.4.7-linux/intel64/bin/pinbin $pid  $cmd' &");
system("gnome-terminal --maximize -e 'gdb ../pin-2.13-61206-gcc.4.4.7-linux/intel64/bin/pinbin --command gdb_cmds' &");

#while(<>) {
#   print $_;
#}
