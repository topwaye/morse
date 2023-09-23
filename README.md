# morse
Morse Operating System

A 286 CPU is like an old style battery charger.

Morse is alternatively called header 286 segments. A 286 CPU can control memory segments directly, but a 386 CPU doesn't. It controls memory segments through headers which give them space. No header, no space, only memory segments.

When booting, morse program is read into a memory segment from a disk, then set a page table which load this memory segment into (virtual) space, preparing for CPU paging.

Once CPU paging is Enabled, the first process has been ready.

Allocate a page table for the second process, read a new program into a memory segment, set the page table, assign a new CPU, then the second process is ready.
