# MORSE
Morse Operating System (based on Linux 0.99.5)

A 286 CPU is like an old-style battery charger.

MORSE is alternatively called header 286 segments. A 286 CPU can control memory segments directly, but a 386 CPU doesn't. It controls memory segments through headers which give them spaces. No header, no concept of space, only memory segments.

The concept of segment appeared in 286, and the concept of space appeared in 386.

286 memory layout is shown as follows:

+-----------program0-----------++-----------program1-----------+

386 memory layout is shown as follows:

+--header0--++-----------program0-----------++--header1--++-----------program1-----------+

When booting, MORSE program is read into a blank memory segment from a disk, then sets a page table (i.e. a header), which puts this memory segment into a space, preparing for CPU paging.

Once CPU paging is enabled, the first process has been ready.

For the second process, the first process allocates outside (i.e. out of the first process) a new page table and a new memory segment, maps them in (i.e. adds them to the page table of the first process), for reading or writing a new program into the new memory segment, and setting the new page table, then maps them out (i.e. removes them from the page table of the first process), now the second process is ready, assigns a new CPU for this new one.

Almost all CPUs today are interrupt-driven, which means they follow a list of instructions in a program and run those until they get to the end or sense an interrupt signal. If the latter event happens, the CPU pauses running the current program.

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

On a bus, everything is serial, not parallel. That is, one is reading or writing, the others are waiting automatically, not reading or writing.

On a bus, a series of CPU instructions are shown as follows:

+--write--++--write a page table record--++--read--++--read--++--read--++--write--+

Beware: in a 386 CPU, each instruction above consists of 2 steps: read a page table, and then read/write memory. The 2 steps are an atom, monopolizing a bus unconditionally.

Copy-on-write is safe.

Consider the following concurrence scenario: Process A and Process B use the same one page table. Process C and Process A 'share' with each other partly. Process A and Process B trigger copy-on-write (e.g. writing the same one page 'shared' with Process C). That is, Process A and Process B change the same page table at the same time.

Algorithm:

* page_table->busy:
* page->busy:
* 0: free
* 1: busy
* page->ref:
* 0: free
* 1+: number of holders (i.e. page table records)

/* all CPUs together, only one goes through the same one page table */

atomic_rw_group_increase ( &i ); /* +--r--++--w--+ *//* 1 + count of entering sleep queue */

while ( 1 ) {

if ( atomic_rw_group_if_then ( &page_table->busy, 0 , 1 ) ) /* +--r--++--w--+ */ { 

while ( j ); /* +--r--+ *//* spinning to wait for return, because someone else not ending yet */

atomic_rw_group_increase ( &j ); /* +--r--++--w--+ */

if ( ! done ) /* whether work already done by someone else or not */ {

call start_working;

}

k = i; /* k is a 'static' value, not refreshing */

while ( get_sleep_queue_length () + 1 < k  ); /* +--r--+ *//* waiting for enter_sleep_queue_on () ending */

page_table->busy = 0; /* +--w--+ */ /* no more sleepers */

empty_sleep_queue_on ( page_table, k - 1 );

atomic_rw_group_decrease ( &i ); /* +--r--++--w--+ */

atomic_rw_group_decrease ( &j ); /* +--r--++--w--+ */

return; /* call .. */

} else {

enter_sleep_queue_on ( page_table );

}

}

/* all CPUs together, only one goes through the same one page */

start_working:

atomic_rw_group_increase ( &n ); /* +--r--++--w--+ *//* 1 + count of entering sleep queue */

while ( 1 ) {

if ( atomic_rw_group_if_then ( &page->busy, 0 , 1 ) ) /* +--r--++--w--+ */ { 

while ( m ); /* +--r--+ *//* spinning to wait for return, because someone else not ending yet */

atomic_rw_group_increase ( &m ); /* +--r--++--w--+ */

call start_working_x;

t = n; /* t is a 'static' value, not refreshing */

while ( get_sleep_queue_length () + 1 < t  ); /* +--r--+ *//* waiting for enter_sleep_queue_on () ending */

page->busy = 0; /* +--w--+ */ /* no more sleepers */

empty_sleep_queue_on ( page, t - 1 );

atomic_rw_group_decrease ( &n ); /* +--r--++--w--+ */

atomic_rw_group_decrease ( &m ); /* +--r--++--w--+ */

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

Beware: in parallel computing, function ( a, b ), a and b should be 'static' values, not refreshing.

The sleeping mechanism ensures that only one process at a time manipulates an object. However, an interrupt service routine manipulating the same one object in this/another running process can break this rule resulting in a conflict. Therefore, before manipulating this object, we must raise the processor priority level to block out interrupts.

To summarise, one monopolizes an object.

Locks may be based on this sleeping mechanism. You meet a lock, you go to sleep.

Virtual machine memory layout is shown as follows:

+--headerVM--++--header0--++-----------program0-----------++--header1--++-----------program1-----------+

HeaderVM records header0, program0, header1 and program1. Header0 records program0 in headerVM space. Header1 records program1 in headerVM space.

MORSE memory layout is shown as follows:

+--h0--++--interrupt service program--++--h1--++--interrupt service program--++--program1--+

Essentially, an OS is a battery which gives birth to new batteries. When powered on, new batteries are born, a battery selects a battery to charge.

There are public batteries and private batteries identified by Session ID. A public battery can only be derived from a public battery, A private battery may be derived from a public battery, or from a private battery with the same Session ID, but may not be derived from the other private batteries.

The arrangement of batteries is like a colored tree, made up of battery nodes with different colors, representing the public and the private.

Sleep is to pull a battery out of this old-style battery charger.

A battery in running state means to turn on the indicator light of a slot in which the battery is plugged.

Signals are marks. Nothing happens when setting a mark to a running battery, until when it goes into kernel area and returns to user area. 

When waking up (i.e. plugging it back in) with a signal mark, it does a long jump before continuing, rather than continue directly.

When waking up without a signal mask, it continues directly.

Theoretically, you can plug in any batteries.

Practically, if you do, some batteries are ejected (i.e. back to sleep) instantly, which means they are not ready to run.

Furthermore, you can pull out any batteries, only when they pause (i.e. ready to sleep, or a clock interrupt).

There are 3 positions to set priority of a battery for scheduling: kernel area, user area, and adjacency area.

Each clock interrupt pulls out all batteries passively, then plugs in batteries according to battery priorities.

A battery is a container filled with round slices of electricity.

Memory is a pond of round slices. Each round slice has a name by which we can find it out.

Devices are sources of round slices to be put into or gotten from the pond.

Debug is a procedure of plugging in a battery and then pulling it out.

topwaye@hotmail.com
