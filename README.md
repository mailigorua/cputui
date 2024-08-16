Small TUI application, which can be used to manage CPU.
At the moment it can only switch individual threads to online/offline state.
Dynamically adapts to terminal color scheme.
Must be run as root/sudo to work properly, as it manipulates files in /sys/devices/system/cpu.
integration with ryzenadj is planned for the future.
Can be build with:
```gcc -o cputui cputui.c -lncurses```
