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

Copy-on-write means if you find you cannot write, change a page table record of yours. Only you, not someone else.

Dirty-read/write doesn't exist on a hardware layer, only read or write one by one, because there is a God (i.e. a bus arbiter).

On a bus, everything is serial, not parallel. That is, one is writing, another one is waiting automatically, not reading or writing.

On a bus, a series of CPU instructions are shown as follows:

+--write--++--write a page table record--++--read--++--read--++--read--++--write--+

Copy-on-write is safe.

Consider the following concurrence scenario: Process A and Process B use the same one page table. Process C and Process A share with each other partly. Process C quits, Process A triggers copy-on-write (e.g. writing a page), Process B spawns Process D. That is, Process A and Process B change the same page table at the same time.

Algorithm:

* page_table->busy:
* page->busy:
* 0: free
* 1: busy
* page->ref:
* 0: free
* 1+: number of holders (i.e. page table records)

/* all CPUs together, only one goes through the same one page table */

while ( 1 ) {

if ( atomic_rw_group_if_then ( page_table->busy, 0 , 1 ) ) /* +--r--++--w--+ */ { 

spin_wait_to_return ( i ); /* someone else not ending yet */

if ( ! done ) /* whether work already done by someone else or not */ {

call start_working;

}

spin_wait_to_full_sleep_queue ( j ); /* wait for enter_sleep_queue_on ( page_table ) ending */

page_table->busy = 0; /* +--w--+ */ /* no more sleepers */

empty_sleep_queue_on ( page_table );

return; /* call ... */

} else {

enter_sleep_queue_on ( page_table );

}

}

/* all CPUs together, only one goes through the same one page */

start_working:

while ( 1 ) {

if ( atomic_rw_group_if_then ( page->busy, 0 , 1 ) ) /* +--r--++--w--+ */ { 

spin_wait_to_return ( n ); /* someone else not ending yet */

call start_working_x;

spin_wait_to_full_sleep_queue ( m ); /* wait for enter_sleep_queue_on ( page ) ending */

page->busy = 0; /* +--w--+ */ /* no more sleepers*/

empty_sleep_queue_on ( page );

return; /* call start_working */

} else {

enter_sleep_queue_on ( page );

}

}

start_working_x:

assert ( page->ref >= 0 ); /* 'static' value, not refreshing */

if ( page->ref == 0 ) {

load_data ( page );

}

if ( page->ref > 1 ) {

split_page ( &page ); /* copy-on-write */

}

set_page_table ( page ) /* page table record holds page */

? page->ref++ /* increase holder count */

: page->ref--; /* decrease holder count */

if ( page->ref == 0 ) {

unload_data ( page );

}

topwaye@hotmail.com
