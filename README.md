# MORSE
Morse Operating System

A 286 CPU is like an old style battery charger.

MORSE is alternatively called header 286 segments. A 286 CPU can control memory segments directly, but a 386 CPU doesn't. It controls memory segments through headers which give them spaces. No header, no concept of space, only memory segments.

When booting, MORSE program is read into a memory segment from a disk, then sets a page table (i.e. a header), which puts this memory segment into a space, preparing for CPU paging.

Once CPU paging is enabled, the first process has been ready.

Allocate a page table for the second process, read a new program into a memory segment, set the page table, assign a new CPU, then the second process is ready.

The concept of segment appeared in 286, and the concept of space appeared in 386.

386 memory layout is shown as follows:
        
+--h0--++-----------s0-----------++--h1--++-----------s1-----------+

topwaye@hotmail.com
