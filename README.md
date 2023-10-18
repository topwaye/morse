# MORSE
Morse Operating System

A 286 CPU is like an old style battery charger.

MORSE is alternatively called header 286 segments. A 286 CPU can control memory segments directly, but a 386 CPU doesn't. It controls memory segments through headers which give them spaces. No header, no concept of space, only memory segments.

The concept of segment appeared in 286, and the concept of space appeared in 386.

286 memory layout is shown as follows:

+-----------program0-----------++-----------program1-----------+

386 memory layout is shown as follows:

+--header0--++-----------program0-----------++--header1--++-----------program1-----------+

When booting, MORSE program is read into a memory segment from a disk, then sets a page table (i.e. a header), which puts this memory segment into a space, preparing for CPU paging.

Once CPU paging is enabled, the first process has been ready.

For the second process, the first process allocates outside (i.e. out of the first process) a new page table and a new memory segment, maps them in (i.e. adds them to the page table of the first process), for reading or writing a new program into the new memory segment, and setting the new page table, then maps them out (i.e. removes them from the page table of the first process), now the second process is ready, assigns a new CPU for this new one.

Almost all CPUs today is interrupt-driven, which means they follow a list of instructions in a program and run those until they get to the end or sense an interrupt signal. If the latter event happens, the CPU pauses running the current program.

386 memory expanded layout is shown as follows:

+--h0--++--interrupt service program--++--program0--++--h1--++--interrupt service program--++--program1--+

In this case, header0 and header1 create two spaces totally. Each space cannot access the other directly or indirectly.

For header0 space, it puts the interrupt service program and program0 into two subspaces respectively. Each subspace cannot access the other directly.

A network is based on a one-to-one architecture.

TCP is one packet for one ack packet asynchronously, meaning a batch of packets are transferred at one time, then TCP waits for their ack packets.

TCP three-way handshake is one packet for one ack packet synchronously as below: For me, I give you one packet and you give me one ack packet. For you, you give me one packet and I give you one ack packet. That is, both packets are acked respectively.

TCP sliding window is directed against payloads, not packets. Packets without payloads can be free to transfer anytime. Only if payloads reach the top line, TCP resets the top line of the payload window size.

Copy-on-write means if you find you cannot write, change your page table record. Only you, not someone else.

Dirty-read/write doesn't exist in the hardware layer, only read or write one by one, because there is a God (i.e. a bus arbiter).

topwaye@hotmail.com
