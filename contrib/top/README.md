# top

The tasks using the CPU, and memory, for AmigaOS 2.04 or newer (any CPU),
in the AmiBash Terminal or any Shell window.

```
top                     # an update every 2 seconds; q quits
top -d 0.5              # every half second
top -b -n 3 > T:load    # batch: three reports, one after another, for a file
top -s name             # sorted by name (cpu, pri, name, time)
```

Keys while it runs:

| Key | Does |
|---|---|
| `q` (or Esc, Ctrl-C) | quit |
| `c` `p` `n` `t` | sort by CPU, priority, name, CPU time |
| `+` / `-` | slower or faster updates |
| space | update now |

At the top:
* the CPU and chipset, Kickstart, and how long `top` has been watching;
* the number of tasks (ready and waiting) and task switches a second;
* the CPU load, Chip and Fast memory as bars.

Then one line per task:
* its share of the CPU in the last interval and its CPU time;
* priority and state: `R` running, `r` ready, `W` waiting;
* stack used and stack size, CLI number;
* the name, or for a CLI the command it runs.

Exec doesn't count the CPU time of tasks. `top` adds a vertical blank
interrupt server that notes, 50 or 60 times a second, which task is running
or that the CPU is idle. A task's share is its share of those notes, so the
figures are samples: at a 2-second interval one frame is 1%. The measuring
code (`tasktime.c`, `tasktime.h`, `tthooks.s`) is shared with SysPulse, the
graphical monitor: keep the copies the same.
