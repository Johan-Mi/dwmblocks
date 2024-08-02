# dwmblocks
Modular status bar for dwm written in Zig.
# usage
To use dwmblocks run 'zig build' and then move the binary wherever you want it to install it.
After that you can put dwmblocks in your xinitrc or other startup script to have it start with dwm.
# modifying blocks
The statusbar is made from text output from commandline programs.
Blocks are added and removed by editing `src/main.zig`.
