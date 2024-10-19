Small TUI application, which can be used to manage CPU.
Dynamically adapts to terminal color scheme.
You can toggle CPU threads between on/off states.
Also, if you have ryzenadj installed, you can monitor/configure different cpu parameters.

Must be run as root/sudo to work properly, as it manipulates files in /sys/devices/system/cpu.

Can be build with:
```gcc -o cputui cputui.c -lncurses```

To go up/down use up/down arrows.

To toggle CPU threads use Enter.

To hop between ‘CPU Control’ and ‘Ryzenadj Parameters’ “windows” use Tab.

To configure ryzenadj values use left/right arrows.
![image](https://github.com/user-attachments/assets/32ba5dfd-38ce-4703-b96f-43dea5a1a236)
