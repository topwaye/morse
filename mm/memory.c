/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

#include <asm/system.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

unsigned long high_memory = 0;

extern void sound_mem_init(void);

int nr_free_pages = 0;
unsigned long free_page_list = 0;
/*
 * The secondary free_page_list is used for malloc() etc things that
 * may need pages during interrupts etc. Normal get_free_page() operations
 * don't touch it, so it stays as a kind of "panic-list", that can be
 * accessed when all other mm tricks have failed.
 */
int nr_secondary_pages = 0;
unsigned long secondary_page_list = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")

unsigned short * mem_map = NULL;

#define CODE_SPACE(addr,p) ((addr) < (p)->end_code)

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGSEGV.
 */
void oom(struct task_struct * task)
{
	printk("\nout of memory\n");
	task->sigaction[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
}

static void free_one_table(unsigned long * page_dir)
{
	int j;
	unsigned long pg_table = *page_dir;
	unsigned long * page_table;

	if (!pg_table)
		return;
	if (pg_table >= high_memory || !(pg_table & 1)) {
		printk("Bad page table: [%08x]=%08x\n",page_dir,pg_table);
		*page_dir = 0;
		return;
	}
	*page_dir = 0;
	if (mem_map[MAP_NR(pg_table)] & MAP_PAGE_RESERVED)
		return;
	page_table = (unsigned long *) (pg_table & 0xfffff000);
	for (j = 0 ; j < 1024 ; j++,page_table++) {
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
		*page_table = 0;
		if (1 & pg)
			free_page(0xfffff000 & pg);
		else
			swap_free(pg >> 1);
	}
	free_page(0xfffff000 & pg_table);
}

/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way. Note that
 * unlike 'free_page_tables()', this function still leaves a valid
 * page-table-tree in memory: it just removes the user pages. The two
 * functions are similar, but there is a fundamental difference.
 */
void clear_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec() yet\n");
	page_dir = (unsigned long *) tsk->tss.cr3;
	if (!page_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	for (i = 0 ; i < 768 ; i++,page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}

/*
 * This function frees up all page tables of a process when it exits.
 */
void free_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return;
	if (tsk == task[0]) {
		printk("task[0] (swapper) killed: unable to recover\n");
		panic("Trying to free up swapper memory space");
	}
	pg_dir = tsk->tss.cr3;
	if (!pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return;
	}
	tsk->tss.cr3 = (unsigned long) swapper_pg_dir;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3"::"a" (tsk->tss.cr3));
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < 1024 ; i++,page_dir++)
		free_one_table(page_dir);
	free_page(pg_dir);
	invalidate();
}

/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	old_pg_dir = current->tss.cr3;
	new_pg_dir = get_free_page(GFP_KERNEL);
	if (!new_pg_dir)
		return -ENOMEM;
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;
	for (i = 0 ; i < 1024 ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (!old_pg_table)
			continue;
		if (old_pg_table >= high_memory || !(1 & old_pg_table)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption");
			*old_page_dir = 0;
			continue;
		}
		if (mem_map[MAP_NR(old_pg_table)] & MAP_PAGE_RESERVED) {
			*new_page_dir = old_pg_table;
			continue;
		}
		new_pg_table = get_free_page(GFP_KERNEL);
		if (!new_pg_table) {
			free_page_tables(tsk);
			return -ENOMEM;
		}
		*new_page_dir = new_pg_table | PAGE_ACCESSED | 7;
		old_page_table = (unsigned long *) (0xfffff000 & old_pg_table);
		new_page_table = (unsigned long *) (0xfffff000 & new_pg_table);
		for (j = 0 ; j < 1024 ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			if (!pg)
				continue;
			if (!(pg & PAGE_PRESENT)) {
				swap_duplicate(pg>>1);
				*new_page_table = pg;
				continue;
			}
			pg &= ~2;
			*new_page_table = pg;
			if (mem_map[MAP_NR(pg)] & MAP_PAGE_RESERVED)
				continue;
			*old_page_table = pg;
			mem_map[MAP_NR(pg)]++;
		}
	}
	invalidate();
	return 0;
}

/*
 * a more complete version of free_page_tables which performs with page
 * granularity.
 */
int unmap_page_range(unsigned long from, unsigned long size)
{
	unsigned long page, page_dir;
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt, pc;

	if (from & 0xfff)
		panic("unmap_page_range called with wrong alignment");
	if (!from)
		panic("unmap_page_range trying to free swapper memory space");
	size = (size + 0xfff) >> PAGE_SHIFT;
	dir = (unsigned long *) (current->tss.cr3 + ((from >> 20) & 0xffc));
	poff = (from >> PAGE_SHIFT) & 0x3ff;
	if ((pcnt = 1024 - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > 1024 ? 1024 : size)) {
		if (!(page_dir = *dir))	{
			poff = 0;
			continue;
		}
		if (!(page_dir & 1)) {
			printk("unmap_page_range: bad page directory.");
			continue;
		}
		page_table = (unsigned long *)(0xfffff000 & page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++) {
			if ((page = *page_table) != 0) {
				*page_table = 0;
				if (1 & page) {
					--current->rss;
					free_page(0xfffff000 & page);
				} else
					swap_free(page >> 1);
			}
		}
		if (pcnt == 1024) {
			free_page(0xfffff000 & page_dir);
			*dir = 0;
		}
	}
	invalidate();
	return 0;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 *
 * permiss is encoded as cxwr (copy,exec,write,read) where copy modifies
 * the behavior of write to be copy-on-write.
 *
 * due to current limitations, we actually have the following
 *		on		off
 * read:	yes		yes
 * write/copy:	yes/copy	copy/copy
 * exec:	yes		yes
 */
int remap_page_range(unsigned long from, unsigned long to, unsigned long size,
		 int permiss)
{
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt;
	unsigned long page;

	if ((from & 0xfff) || (to & 0xfff))
		panic("remap_page_range called with wrong alignment");
	dir = (unsigned long *) (current->tss.cr3 + ((from >> 20) & 0xffc));
	size = (size + 0xfff) >> PAGE_SHIFT;
	poff = (from >> PAGE_SHIFT) & 0x3ff;
	if ((pcnt = 1024 - poff) > size)
		pcnt = size;

	while (size > 0) {
		if (!(1 & *dir)) {
			if (!(page_table = (unsigned long *)get_free_page(GFP_KERNEL))) {
				invalidate();
				return -1;
			}
			*dir++ = ((unsigned long) page_table) | PAGE_ACCESSED | 7;
		}
		else
			page_table = (unsigned long *)(0xfffff000 & *dir++);
		if (poff) {
			page_table += poff;
			poff = 0;
		}

		for (size -= pcnt; pcnt-- ;) {
			int mask;

			mask = 4;
			if (permiss & 1)
				mask |= 1;
			if (permiss & 2) {
				if (permiss & 8)
					mask |= 1;
				else
					mask |= 3;
			}
			if (permiss & 4)
				mask |= 1;

			if ((page = *page_table) != 0) {
				*page_table = 0;
				--current->rss;
				if (1 & page)
					free_page(0xfffff000 & page);
				else
					swap_free(page >> 1);
			}

			/*
			 * i'm not sure of the second cond here. should we
			 * report failure?
			 * the first condition should return an invalid access
			 * when the page is referenced. current assumptions
			 * cause it to be treated as demand allocation.
			 */
			if (mask == 4 || to >= high_memory || !mem_map[MAP_NR(to)])
				*page_table++ = 0;	/* not present */
			else {
				++current->rss;
				*page_table++ = (to | mask);
				if (!(mem_map[MAP_NR(to)] & MAP_PAGE_RESERVED))
					mem_map[MAP_NR(to)]++;
			}
			to += PAGE_SIZE;
		}
		pcnt = (size > 1024 ? 1024 : size);
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 * if wp = 1 the page will be write protected
 */
static unsigned long put_page(struct task_struct * tsk,unsigned long page,unsigned long address,int wp)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page >= high_memory) {
		printk("put_page: trying to put page %p at %p\n",page,address);
		return 0;
	}
	tmp = mem_map[MAP_NR(page)];
	if (!(tmp & MAP_PAGE_RESERVED) && (tmp != 1)) {
		printk("put_page: mem_map disagrees with %p at %p\n",page,address);
		return 0;
	}
	page_table = (unsigned long *) (tsk->tss.cr3 + ((address>>20) & 0xffc));
	if ((*page_table) & PAGE_PRESENT)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		printk("put_page: bad page directory entry\n");
		oom(tsk);
		*page_table = BAD_PAGETABLE | PAGE_ACCESSED | 7;
		return 0;
	}
	page_table += (address >> PAGE_SHIFT) & 0x3ff;
	if (*page_table) {
		printk("put_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | PAGE_ACCESSED | 5 | (!wp << 1);
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */
unsigned long put_dirty_page(struct task_struct * tsk, unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page >= high_memory)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) (tsk->tss.cr3 + ((address>>20) & 0xffc));
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page(GFP_KERNEL)))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table += (address >> PAGE_SHIFT) & 0x3ff;
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | (PAGE_DIRTY | PAGE_ACCESSED | 7);
/* no need for invalidate */
	return page;
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Fixed the routine to repeat a bit more: this is slightly slower,
 * but there were race-conditions in the old code..
 */
void do_wp_page(unsigned long error_code, unsigned long address,
	struct task_struct * tsk, unsigned long user_esp)
{
	unsigned long pde, pte, old_page, dirty;
	unsigned long new_page = 0;

	/* check code space write */
	if (tsk == current && tsk->executable && CODE_SPACE(address, current)) {
		/* don't send SIGSEGV when in kernel or v86 mode */
		if (user_esp)
			send_sig(SIGSEGV, tsk, 1);
		/* Note that we still do the copy-on-write: if the process catches
		 * SIGSEGV we want things to work..
		 */
	}
repeat:
	pde = tsk->tss.cr3 + ((address>>20) & 0xffc);
	pte = *(unsigned long *) pde;
	if (!(pte & PAGE_PRESENT)) {
		if (new_page)
			free_page(new_page);
		return;
	}
	if ((pte & 7) != 7 || pte >= high_memory) {
		printk("do_wp_page: bogus page-table at address %08x (%08x)\n",address,pte);
		*(unsigned long *) pde = BAD_PAGETABLE | 7;
		send_sig(SIGKILL, tsk, 1);
		if (new_page)
			free_page(new_page);
		return;
	}
	pte &= 0xfffff000;
	pte += (address>>10) & 0xffc;
	old_page = *(unsigned long *) pte;
	if (!(old_page & PAGE_PRESENT)) {
		if (new_page)
			free_page(new_page);
		return;
	}
	if (old_page >= high_memory) {
		printk("do_wp_page: bogus page at address %08x (%08x)\n",address,old_page);
		*(unsigned long *) pte = BAD_PAGE | 7;
		send_sig(SIGKILL, tsk, 1);
		if (new_page)
			free_page(new_page);
		return;
	}
	if (old_page & PAGE_RW) {
		if (new_page)
			free_page(new_page);
		return;
	}
	tsk->min_flt++;
	dirty = old_page & PAGE_DIRTY;
	old_page &= 0xfffff000;
	if (mem_map[MAP_NR(old_page)]==1) {
		*(unsigned long *) pte |= 2;
		invalidate();
		if (new_page)
			free_page(new_page);
		return;
	}
	if (!new_page && (new_page=get_free_page(GFP_KERNEL)))
		goto repeat;
	if (new_page)
		copy_page(old_page,new_page);
	else {
		new_page = BAD_PAGE;
		oom(tsk);
	}
	*(unsigned long *) pte = new_page | dirty | PAGE_ACCESSED | 7;
	free_page(old_page);
	invalidate();
}

void write_verify(unsigned long address)
{
	if (address < TASK_SIZE)
		do_wp_page(1,address,current,0);
}

static void get_empty_page(struct task_struct * tsk, unsigned long address)
{
	unsigned long tmp;

	tmp = get_free_page(GFP_KERNEL);
	if (!tmp) {
		oom(tsk);
		tmp = BAD_PAGE;
	}
	if (!put_page(tsk,tmp,address,0))
		free_page(tmp);
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable or library.
 */
static int try_to_share(unsigned long address, struct task_struct * tsk,
	struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = p->tss.cr3 + ((address>>20) & 0xffc);
	to_page = tsk->tss.cr3 + ((address>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= high_memory)
		return 0;
	if (mem_map[MAP_NR(phys_addr)] & MAP_PAGE_RESERVED)
		return 0;
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	invalidate();
	phys_addr >>= PAGE_SHIFT;
	mem_map[phys_addr]++;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		to = get_free_page(GFP_KERNEL);
		if (!to) {
			mem_map[phys_addr]--;
			return 0;
		}
		*(unsigned long *) to_page = to | PAGE_ACCESSED | 7;
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(struct task_struct * tsk, struct inode * inode, unsigned long address)
{
	struct task_struct ** p;
	int i;

	if (!inode || inode->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (tsk == *p)
			continue;
		if (inode != (*p)->executable) {
			for (i=0; i < (*p)->numlibraries; i++)
				if (inode == (*p)->libraries[i].library)
					break;
			if (i >= (*p)->numlibraries)
				continue;
		}
		if (try_to_share(address,tsk,*p))
			return 1;
	}
	return 0;
}

/*
 * fill in an empty page-table if none exists
 */
static unsigned long get_empty_pgtable(struct task_struct * tsk,unsigned long address)
{
	unsigned long page = 0;
	unsigned long *p;
repeat:
	p = (unsigned long *) (tsk->tss.cr3 + ((address >> 20) & 0xffc));
	if (1 & *p) {
		free_page(page);
		return *p;
	}
	if (*p) {
		printk("get_empty_pgtable: bad page-directory entry \n");
		*p = 0;
	}
	if (page) {
		*p = page | PAGE_ACCESSED | 7;
		return *p;
	}
	if ((page = get_free_page(GFP_KERNEL)) != 0)
		goto repeat;
	oom(current);
	*p = BAD_PAGETABLE | 7;
	return 0;
}

void do_no_page(unsigned long error_code, unsigned long address,
	struct task_struct *tsk, unsigned long user_esp)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	unsigned int block,i;
	struct inode * inode;

	page = get_empty_pgtable(tsk,address);
	if (!page)
		return;
	page &= 0xfffff000;
	page += (address >> 10) & 0xffc;
	tmp = *(unsigned long *) page;
	if (tmp & 1)
		return;
	++tsk->rss;
	if (tmp) {
		++tsk->maj_flt;
		swap_in((unsigned long *) page);
		return;
	}
	address &= 0xfffff000;
	inode = NULL;
	block = 0;
	if (address < tsk->end_data) {
		inode = tsk->executable;
		block = 1 + address / BLOCK_SIZE;
	} else {
		i = tsk->numlibraries;
		while (i-- > 0) {
			if (address < tsk->libraries[i].start)
				continue;
			block = address - tsk->libraries[i].start;
			if (block >= tsk->libraries[i].length + tsk->libraries[i].bss)
				continue;
			inode = tsk->libraries[i].library;
			if (block < tsk->libraries[i].length)
				block = 1 + block / BLOCK_SIZE;
			else
				block = 0;
			break;
		}
	}
	if (!inode) {
		++tsk->min_flt;
		get_empty_page(tsk,address);
		if (tsk != current)
			return;
		if (address < tsk->brk)
			return;
		if (address+8192 >= (user_esp & 0xfffff000))
			return;
		send_sig(SIGSEGV,tsk,1);
		return;
	}
	if (share_page(tsk,inode,address)) {
		++tsk->min_flt;
		return;
	}
	++tsk->maj_flt;
	page = get_free_page(GFP_KERNEL);
	if (!page) {
		oom(current);
		put_page(tsk,BAD_PAGE,address,0);
		return;
	}
	if (block) {
		for (i=0 ; i<4 ; block++,i++)
			nr[i] = bmap(inode,block);
		bread_page(page,inode->i_dev,nr);
	}
	if (share_page(tsk,inode,address)) {
		free_page(page);
		return;
	}
	i = address + PAGE_SIZE - tsk->end_data;
	if (i > PAGE_SIZE-1)
		i = 0;
	tmp = page + PAGE_SIZE;
	while (i--) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(tsk,page,address,CODE_SPACE(address, tsk)))
		return;
	free_page(page);
	oom(current);
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
void do_page_fault(unsigned long *esp, unsigned long error_code)
{
	unsigned long address;
	unsigned long user_esp = 0;
	unsigned long stack_limit;
	unsigned int bit;
	extern void die_if_kernel();

	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	if (address < TASK_SIZE) {
		if (error_code & 4) {	/* user mode access? */
			if (esp[2] & VM_MASK) {
				bit = (address - 0xA0000) >> PAGE_SHIFT;
				if (bit < 32)
					current->screen_bitmap |= 1 << bit;
			} else 
				user_esp = esp[3];
		}
		if (error_code & 1)
			do_wp_page(error_code, address, current, user_esp);
		else
			do_no_page(error_code, address, current, user_esp);
		if (!user_esp)
			return;
		stack_limit = current->rlim[RLIMIT_STACK].rlim_cur;
		if (stack_limit >= RLIM_INFINITY)
			return;
		if (stack_limit >= current->start_stack)
			return;
		stack_limit = current->start_stack - stack_limit;
		if (user_esp < stack_limit)
			send_sig(SIGSEGV, current, 1);
		return;
	}
	printk("Unable to handle kernel paging request at address %08x\n",address);
	die_if_kernel("Oops",esp,error_code);
	do_exit(SIGKILL);
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 */
unsigned long __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl"
		::"a" (7+BAD_PAGE),
		  "D" ((long) empty_bad_page_table),
		  "c" (1024)
		:"di","cx");
	return (unsigned long) empty_bad_page_table;
}

unsigned long __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	__asm__ __volatile__("cld ; rep ; stosl"
		::"a" (0),
		  "D" ((long) empty_bad_page),
		  "c" (1024)
		:"di","cx");
	return (unsigned long) empty_bad_page;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	printk("Free pages:      %6d\n",nr_free_pages);
	printk("Secondary pages: %6d\n",nr_secondary_pages);
	printk("Buffer heads:    %6d\n",nr_buffer_heads);
	printk("Buffer blocks:   %6d\n",nr_buffers);
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
}

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long * pg_dir;
	unsigned long * pg_table;
	unsigned long tmp;
	unsigned long address;

/*
 * Physical page 0 is special: it's a "zero-page", and is guaranteed to
 * stay that way - it's write-protected and when there is a c-o-w, the
 * mm handler treats it specially.
 */
	memset((void *) 0, 0, 4096);
	start_mem += 4095;
	start_mem &= 0xfffff000;
	address = 0;
	pg_dir = swapper_pg_dir + 768;		/* at virtual addr 0xC0000000 */
	while (address < end_mem) {
		tmp = *pg_dir;
		if (!tmp) {
			tmp = start_mem;
			*pg_dir = tmp | 7;
			start_mem += 4096;
		}
		pg_dir++;
		pg_table = (unsigned long *) (tmp & 0xfffff000);
		for (tmp = 0 ; tmp < 1024 ; tmp++,pg_table++) {
			if (address && address < end_mem)
				*pg_table = 7 + address;
			else
				*pg_table = 0;
			address += 4096;
		}
	}
	invalidate();
	return start_mem;
}

void mem_init(unsigned long start_low_mem,
	      unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp;
	unsigned short * p;

	cli();
	end_mem &= 0xfffff000;
	high_memory = end_mem;
	start_mem += 0x0000000f;
	start_mem &= 0xfffffff0;
	tmp = MAP_NR(end_mem);
	mem_map = (unsigned short *) start_mem;
	p = mem_map + tmp;
	start_mem = (unsigned long) p;
	while (p > mem_map)
		*--p = MAP_PAGE_RESERVED;
	start_low_mem += 0x00000fff;
	start_low_mem &= 0xfffff000;
	start_mem += 0x00000fff;
	start_mem &= 0xfffff000;
	while (start_low_mem < 0xA0000) {
		mem_map[MAP_NR(start_low_mem)] = 0;
		start_low_mem += 4096;
	}
	while (start_mem < end_mem) {
		mem_map[MAP_NR(start_mem)] = 0;
		start_mem += 4096;
	}
	sound_mem_init();
	free_page_list = 0;
	nr_free_pages = 0;
	for (tmp = 0 ; tmp < end_mem ; tmp += 4096) {
		if (mem_map[MAP_NR(tmp)]) {
			if (tmp < 0xA0000)
				codepages++;
			else if (tmp < 0x100000)
				reservedpages++;
			else
				datapages++;
			continue;
		}
		*(unsigned long *) tmp = free_page_list;
		free_page_list = tmp;
		nr_free_pages++;
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %dk/%dk available (%dk kernel, %dk reserved, %dk data)\n",
		tmp >> 10,
		end_mem >> 10,
		codepages << 2,
		reservedpages << 2,
		datapages << 2);
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->freeram = 0;
	val->sharedram = 0;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i]) {
			val->freeram++;
			continue;
		}
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->freeram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
