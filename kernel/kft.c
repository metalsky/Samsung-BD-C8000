/*
 *  kernel/kft.c
 *
 *  Kernel Function Trace
 *
 *  Copyright (C) 2002  MontaVista Software
 *      (when it was kfi.c)
 *  Copyright 2005  Sony Corporation
 *  
 *  Support for tracing function entry/exit in the Linux kernel,
 *  using the function instrumentation feature of GCC (-finstrument-functions).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kft.h>
#include <linux/hardirq.h>
#include <linux/vmalloc.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <asm/system.h>


#define COMMAND_BUFFER_LEN	2048

#ifdef CONFIG_KFT_STATIC_RUN
extern struct kft_run kft_run0;
static struct kft_run* run_curr = &kft_run0;
#else
int kft_run0;
static struct kft_run* run_curr = 0;
#endif

static int in_entry_exit[NR_CPUS];

/* control whether a generic or custom clock routine is used */
#if !defined(CONFIG_MIPS) && !defined(CONFIG_SH)
#define GENERIC_KFTREADCLOCK 1
#endif

#ifdef GENERIC_KFTREADCLOCK
/*
 * Define a genefic kft_readclock routine.
 * This should work well enough for platforms where sched_clock()
 * gives good (sub-microsecond) precision.
 *
 * There are valid reasons to use other routines, including:
 *  - when using kft for boot timings
 *    - on most platforms, sched_clock() does not work correctly until
 *    after time_init()
 *  - reduced overhead for obtaining a microsecond value
 *    (This may be incorrect, since at most this adds one
 *    64-bit-by-32-bit divide, in addition to the shift that
 *    is inside sched_clock(). KFT does enough other stuff
 *    that this one divide is probably not a major factor
 *    in KFT overhead.)
 */ 
static inline unsigned long __noinstrument kft_readclock(void)
{
	unsigned long long t;
	
	t = sched_clock();
	/* convert to microseconds */
	do_div(t,1000);
	return (unsigned long)t;
}

static inline unsigned long __noinstrument kft_clock_to_usecs(unsigned long clock)
{
	return clock;
}

#endif /* GENERIC_KFTREADCLOCK - non-MIPS, non-SH */

#ifndef GENERIC_KFTREADCLOCK
/*
 * Use arch-specific kft_readclock() and kft_clock_to_usecs() routines
 * 
 * First - define some platform-specific constants
 *
 * !! If using a non-generic KFT readclock, you need
 * to set the following constants for your machine!!
 * 
 * CLOCK_FREQ is a hardcoded value for the frequency of
 * whatever clock you are using for kft_readclock()
 * It would be nice to use a probed clock freq (cpu_hz)
 * here, but it  isn't set early enough for some boot
 * measurements.
 * Hint: for x86, boot once and look at /proc/cpuinfo
 *
 * CLOCK_SHIFT is used to bring the clock frequency into
 * a manageable range.  For my 3 GHz machine, I decided
 * to divide the cpu cycle clock by 8. This throws
 * away some clock precision, but makes some of the
 * other math faster and helps us stay in 32 bits.
 */

#ifdef CONFIG_X86_TSC
// Tim's old laptop
//#define CLOCK_FREQ 645206000ULL
// Tim's HP desktop
#define CLOCK_FREQ 2992332000ULL
#define CLOCK_SHIFT	3
#endif /* CONFIG_X86_TSC */

#ifdef CONFIG_PPC32
// Ebony board
#define CLOCK_FREQ 400000000ULL
#define CLOCK_SHIFT	3
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_CPU_SH4
#define CLOCK_FREQ 15000000ULL	/* =P/4 */
#define CLOCK_SHIFT	0
#endif /* CONFIG_CPU_SH4 */

#ifdef CONFIG_MIPS
/* tx4938 */
#define CLOCK_FREQ ( 300000000ULL / 2 )
#define CLOCK_SHIFT	3
#endif /* CONFIG_MIPS */



#ifdef CONFIG_X86_TSC
#include <asm/time.h>	/* for rdtscll macro */
static inline unsigned long __noinstrument kft_readclock(void)
{
	unsigned long long ticks;

	rdtscll(ticks);
	return (unsigned long)((ticks>>CLOCK_SHIFT) & 0xffffffff);
}
#endif /* CONFIG_X86_TSC */


#ifdef CONFIG_PPC32
#include <asm/time.h>	/* for get_tbu macro */
/* copied from sched_clock for ppc */
static inline unsigned long __noinstrument kft_readclock(void)
{
	unsigned long lo, hi, hi2;
	unsigned long long ticks;

	do {
		hi = get_tbu();
		lo = get_tbl();
		hi2 = get_tbu();
	} while (hi2 != hi);
	ticks = ((unsigned long long) hi << 32) | lo;
	return (unsigned long)((ticks>>CLOCK_SHIFT) & 0xffffffff);
}
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_CPU_SH4
/*
 * In advance, start Timer Unit4(TMU4)
 * ex.
 *  *TMU4_TCR = 0x0000;
 *  *TMU4_TCOR = 0;
 *  *TMU4_TCNT = 0;
 *  *TMU_TSTR2 = (*TMU_TSTR2|0x02);
 */
#define TMU4_TCNT	((volatile unsigned long *)0xFE100018)

static inline unsigned long __noinstrument kft_readclock(void)
{
	return (-(*TMU4_TCNT))>>CLOCK_SHIFT;
}
#endif /* CONFIG_CPU_SH4 */

#ifdef CONFIG_MIPS
static inline unsigned long __noinstrument kft_readclock(void)
{
	return (unsigned long)read_c0_count();
}
#endif /* CONFIG_MIPS */

/*
 * Now define a generic routine to convert from clock tics to usecs.
 * 
 * This weird scaling factor makes it possible to use shifts and a 
 * single 32-bit divide, instead of more expensive math,
 * for the conversion to microseconds.
 */
#define CLOCK_SCALE ((((CLOCK_FREQ*1024*1024)/1000000))>>CLOCK_SHIFT)

static inline unsigned long __noinstrument kft_clock_to_usecs(unsigned long clock)
{
	/* math to stay in 32 bits. Try to avoid over and underflows */
	if (clock<4096)
		return (clock<<20)/CLOCK_SCALE;
	if (clock<(4096<<5))
		return (clock<<15)/(CLOCK_SCALE>>5);
	if (clock<(4096<<10))
		return (clock<<10)/(CLOCK_SCALE>>10);
	if (clock<(4096<<15))
		return (clock<<5)/(CLOCK_SCALE>>15);
	else
		return clock/(CLOCK_SCALE>>20);
}

#endif /* not GENERIC_KFT_READCLOCK */

#ifdef CONFIG_KFT_CLOCK_SCALE

extern void set_cyc2ns_scale(unsigned long cpu_mhz);

/*
 * Do whatever is required to prepare for calling sched_clock very
 * early in the boot sequence.
 */
extern void __noinstrument setup_early_kft_clock(void)
{
	set_cyc2ns_scale(CONFIG_KFT_CLOCK_SCALE);
}
#endif /* CONFIG_KFT_CLOCK_SCALE */

#ifdef SMP
static unsigned long usecs_since_boot[NR_CPUS];
static unsigned long last_machine_cycles[NR_CPUS];

static inline unsigned long __noinstrument update_usecs_since_boot(void)
{
	unsigned long machine_cycles, delta;
	int cpu;

	cpu = smp_processor_id();

	machine_cycles = kft_readclock();
	delta = machine_cycles - last_machine_cycles[cpu];
	delta = kft_clock_to_usecs(delta);
	/*
	 * check for clock going backwards - this may happen
	 * because the clock is reset during startup
	 * initialization of the timer.
	 * In this case, we lose the correct value for this
	 * entry - but that's better than moving usecs_since_boot
	 * backwards and causing negative durations in the log.
	 */
	if (delta > 0x8000000) {
		delta = 0;
	}
	usecs_since_boot[cpu] += delta;
	
	last_machine_cycles[cpu] = machine_cycles;
	return usecs_since_boot[cpu];
}
#else /* !CONFIG_SMP */
static unsigned long usecs_since_boot = 0;
static unsigned long last_machine_cycles = 0;

static inline unsigned long __noinstrument update_usecs_since_boot(void)
{
	unsigned long machine_cycles, delta;

	machine_cycles = kft_readclock();
	delta = machine_cycles - last_machine_cycles;
	delta = kft_clock_to_usecs(delta);
	/*
	 * check for clock going backwards - this may happen
	 * because the clock is reset during startup
	 * initialization of the timer.
	 * In this case, we lose the correct value for this
	 * entry - but that's better than moving usecs_since_boot
	 * backwards and causing negative durations in the log.
	 */
	if (delta > 0x8000000) {
		delta = 0;
	}
	usecs_since_boot += delta;
	
	last_machine_cycles = machine_cycles;
	return usecs_since_boot;
}
#endif /* !CONFIG_SMP */

static inline int __noinstrument in_func_list(struct kft_filters* filters,
	void* func)
{
	int i;

	for (i=0; i < filters->func_list_size; i++) {
		if (filters->func_list[i] == func)
			return 1;
	}

	return 0;
}

/*
 * filter_out: return 1 if function should NOT be logged
 * Can be because function is NOT on the filter list, or because
 * of context (interrupt or not)
 */
static inline int __noinstrument filter_out(struct kft_filters* filters,
	void *this_fn)
{
	int in_intr;
	
	if (filters->func_list && !in_func_list(filters, this_fn)) {
		filters->cnt.func_list++;
		return 1;
	}

	in_intr = in_interrupt();

	if (filters->no_ints && in_intr) {
		filters->cnt.no_ints++;
		return 1;
	}

	if (filters->only_ints && !in_intr) {
		filters->cnt.only_ints++;
		return 1;
	}
	
	return 0;
}


#define LOG_LOCKED	1
#define LOG_UNLOCKED	0
static unsigned int log_lock = LOG_UNLOCKED;

atomic_t drop_count = ATOMIC_INIT(0);
atomic_t lock_timeout_entry_count = ATOMIC_INIT(0);
atomic_t lock_timeout_exit_count = ATOMIC_INIT(0);
/* don't loop more than a million times waiting for the log lock */
#define LOG_LOCK_SPIN_LIMIT	1000000

static inline void __noinstrument do_func_entry(struct kft_run* run, void *this_fn,
	void *call_site)
{
	struct kft_entry* entry;
	int lock_held_count;

#ifdef CONFIG_KFT_SAVE_ARGS
	unsigned long *fp = __builtin_frame_address(1);
#endif
	
	/* check for log full */
	if (run->next_entry >= run->num_entries) {
		run->complete = 1;
		run->stop_trigger.mark = update_usecs_since_boot();
		run->stop_trigger.type = TRIGGER_LOG_FULL;
		return;
	}
	/* acquire lock on trace log */
	lock_held_count = 0;
	while( (cmpxchg(&log_lock, LOG_UNLOCKED, LOG_LOCKED))==LOG_LOCKED ) {
		lock_held_count++;
		if (lock_held_count >= LOG_LOCK_SPIN_LIMIT ) {
			atomic_inc(&lock_timeout_entry_count);
			return;
		}
	}
	/* allocate space for the new entry */
	entry = &run->log[run->next_entry];
	run->next_entry++;
	
	entry->va = this_fn;
	entry->call_site = call_site;
	entry->pid = in_interrupt() ? INTR_CONTEXT : current->pid;

	entry->delta = TIME_NOT_SET;
	entry->time = update_usecs_since_boot() - run->start_trigger.mark;
#ifdef CONFIG_KFT_SAVE_ARGS
	entry->fp = (unsigned long)fp;
#ifdef CONFIG_PPC32
	entry->a1 = fp[6]; /* from cwg.pdf ABI spec */
	entry->a2 = fp[7]; /* from cwg.pdf ABI spec */
	entry->a3 = fp[8]; /* from cwg.pdf ABI spec */
#endif /* CONFIG_PPC32 */
#endif /* CONFIG_KFT_SAVE_REGS */
	log_lock = LOG_UNLOCKED;
}

static inline void __noinstrument do_func_exit(struct kft_run* run, void *this_fn,
	void *call_site)
{
	struct kft_entry* entry;
	unsigned long exittime;
	unsigned long delta;
	unsigned int pid;
	int entry_i;
	int lock_held_count;

	int i;

	pid = in_interrupt() ? INTR_CONTEXT : current->pid;

	/* acquire lock on trace log */
	lock_held_count = 0;
	while( (cmpxchg(&log_lock, LOG_UNLOCKED, LOG_LOCKED))==LOG_LOCKED ) {
		lock_held_count++;
		if (lock_held_count >= LOG_LOCK_SPIN_LIMIT ) {
			atomic_inc(&lock_timeout_exit_count);
			return;
		}
	}

	/* find matching entry in log - searching backwards from current log end */
	/* FIXTHIS - need lock on next_entry here */
	entry_i = -1;
	for (i = run->next_entry-1; i >= 0; i--) {
		entry = &run->log[i];
		if (entry->va == this_fn &&
		    entry->pid == pid &&
		    entry->delta == TIME_NOT_SET) {
			entry_i = i;
			break;
		}
	}

	if (entry_i == -1) {
		run->notfound++;
		log_lock = LOG_UNLOCKED;
		return;
	}
	
	/* entry = &run->log[entry_i];  - it's already set from above loop */

	// calc delta
	exittime = update_usecs_since_boot() - run->start_trigger.mark;
	delta = exittime - entry->time;
	
	if ((run->filters.min_delta && delta < run->filters.min_delta) ||
	    (run->filters.max_delta && delta > run->filters.max_delta)) {
		run->filters.cnt.delta++;
		/* remove this entry by moving all succeeding entries down in
		 * the log.  This is a potentially expensive operation.
	 	 * Note that on uniprocessor, it is rare to have to move anything
		 * at all because the function being exited is usually the one 
		 * at the end of the log.
		 */
		run->next_entry--;
		for (i = entry_i; i < run->next_entry; i++)
			run->log[i] = run->log[i+1];
	} else {
		entry->delta = delta;
#ifdef CONFIG_SMP
		/* save CPU number in pid, bits 24-31 */
		entry->pid &= ~(0xff<<24);
		entry->pid |= (smp_processor_id() << 24);
#endif
	}
	log_lock = LOG_UNLOCKED;
}


static inline int __noinstrument test_trigger(struct kft_run* run, int start_trigger, 
	int func_entry, void* func_addr)
{
	unsigned long time, base_time;
	int ret = 0;
	struct kft_trigger* t;
		
	t = start_trigger ? &run->start_trigger : &run->stop_trigger;

	switch (t->type) {
	case TRIGGER_TIME:
		time = update_usecs_since_boot();
		if (start_trigger) {
			/* trigger start time based from boot */
			base_time = 0;
		} else {
			/* trigger stop time based from start trigger time */
			base_time = run->start_trigger.mark;
		}
		
		if (time > base_time + t->time) {
			t->mark = time; // mark trigger time
			ret = 1;
		}
		break;
	case TRIGGER_FUNC_ENTRY:
		if (func_entry && func_addr == t->func_addr) {
			time = update_usecs_since_boot();
			t->mark = time; // mark trigger time
			ret = 1;
		}
		break;
	case TRIGGER_FUNC_EXIT:
		if (!func_entry && func_addr == t->func_addr) {
			time = update_usecs_since_boot();
			t->mark = time; // mark trigger time
			ret = 1;
		}
		break;
	default:
		break;
	}

	return ret;
}


static inline void __noinstrument func_entry_exit(void *this_fn, void *call_site,
	int func_entry)
{
	unsigned long flags;
	struct kft_run* run;
	int cpu;
	int complete = 0;
	
	/* stave off interrupts on the current processor,
	 * note: we use raw_local_irq_save/restore here
	 * due to avoid infinite recursion here,
	 * as some trace facilities inside local_irq_save/restore
	 * are instrumentable themselves.
	 */
	raw_local_irq_save(flags);

	/* only allow one thread through here per processor */
	cpu = smp_processor_id();
	if (in_entry_exit[cpu] ) {
		/* this should never happen (with ints disabled),
		 * but we check for it anyway.
		 */
		atomic_inc(&drop_count);
		raw_local_irq_restore(flags);
		return;
	}
	in_entry_exit[cpu] = 1;
	
	update_usecs_since_boot();
	
	run = run_curr;

	if (!run || run->complete) {
		goto entry_exit_byebye;
	}
	
	if (!run->triggered) {
		/* test for start trigger */
		if (!run->primed || !(run->triggered = test_trigger(run, 1,
						    func_entry, this_fn))) {
			goto entry_exit_byebye;
		}
	}

	if (!run->complete) {
		/* test for stop trigger */
		complete = test_trigger(run, 0, func_entry, this_fn);
	}

	if ((!complete ||
		(complete && run->flags & KFT_MODE_AUTO_REPEAT &&
		run->stop_trigger.type != TRIGGER_LOG_FULL)) &&
		!filter_out(&run->filters, this_fn)) {
		if (func_entry)
			do_func_entry(run, this_fn, call_site);
		else
			do_func_exit(run, this_fn, call_site);
	}

	if (!run->complete) {
		run->complete = complete;
	}

	/* test for auto restart of trace */
	if (run->complete && run->flags & KFT_MODE_AUTO_REPEAT &&
		run->stop_trigger.type != TRIGGER_LOG_FULL ) {
		run->triggered = 0;
		run->complete = 0;
	}
	
 entry_exit_byebye:
	in_entry_exit[cpu] = 0;
	raw_local_irq_restore(flags);
}


void __noinstrument __cyg_profile_func_enter (void *this_fn, void *call_site)
{
	func_entry_exit(this_fn, call_site, 1);
}

void __noinstrument __cyg_profile_func_exit (void *this_fn, void *call_site)
{
	func_entry_exit(this_fn, call_site, 0);
}


#define dump_str(buf, len, fmt, arg...) \
    if (buf) len += sprintf(buf + len, fmt, ## arg); \
    else len += printk(KERN_EMERG fmt, ## arg)

static int __noinstrument print_trigger(char* buf, int len,
	struct kft_trigger* t, int start_trigger)
{
	char trigbuf[80];
	
	switch (t->type) {
	case TRIGGER_USER:
		sprintf(trigbuf, "system call\n");
		break;
	case TRIGGER_TIME:
		sprintf(trigbuf, "time at %lu usec from %s\n",
		       t->time, start_trigger ? "boot" : "start trigger");
		break;
	case TRIGGER_FUNC_ENTRY:
		sprintf(trigbuf, "entry to function 0x%08lx\n",
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_FUNC_EXIT:
		sprintf(trigbuf, "exit from function 0x%08lx\n",
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_LOG_FULL:
		sprintf(trigbuf, "log full\n");
		break;
	default:
		sprintf(trigbuf, "?\n");
		break;
	}

	dump_str(buf, len, "Logging %s at %lu usec by %s",
		 (start_trigger ? "started" : "stopped"),
		 t->mark, trigbuf);

	return len;
}

static void __noinstrument print_trigger2(struct seq_file *m,
	struct kft_trigger* t, int start_trigger)
{
	char trigbuf[80];
	
	switch (t->type) {
	case TRIGGER_USER:
		sprintf(trigbuf, "user action\n");
		break;
	case TRIGGER_TIME:
		sprintf(trigbuf, "time at %lu usec from %s\n",
		       t->time, start_trigger ? "boot" : "start trigger");
		break;
	case TRIGGER_FUNC_ENTRY:
		sprintf(trigbuf, "entry to function 0x%08lx\n",
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_FUNC_EXIT:
		sprintf(trigbuf, "exit from function 0x%08lx\n",
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_LOG_FULL:
		sprintf(trigbuf, "log full\n");
		break;
	default:
		sprintf(trigbuf, "?\n");
		break;
	}

	seq_printf(m, "Logging %s at %lu usec by %s",
		 (start_trigger ? "started" : "stopped"),
		 t->mark, trigbuf);
	return;
}

int __noinstrument kft_dump_log(char* buf)
{
	int i, len = 0;
	struct kft_run* run = run_curr;
	struct kft_filters* filters = &run->filters;

	if (!run) {
		dump_str(buf, len, "\nNo logging run registered\n");
		return len;
	}

	if (!run->triggered) {
		dump_str(buf, len, "\nLogging not yet triggered\n");
		return len;
	}

	if (!run->complete) {
		dump_str(buf, len, "\nLogging is running\n");
		return len;
	}

	dump_str(buf, len, "\nKernel Instrumentation Run ID %d\n\n",
		 run->id);
	
	dump_str(buf, len, "Filters:\n");
	if (filters->func_list_size) {
		dump_str(buf, len, "\t%d-entry function list\n",
			 filters->func_list_size);
	}
	if (filters->min_delta) {
		dump_str(buf, len, "\t%ld usecs minimum execution time\n",
			 filters->min_delta);
	}
	if (filters->max_delta) {
		dump_str(buf, len, "\t%ld usecs maximum execution time\n",
			 filters->max_delta);
	}
	if (filters->no_ints) {
		dump_str(buf, len, "\tno functions in interrupt context\n");
	}
	if (filters->only_ints) {
		dump_str(buf, len,
			 "\tno functions NOT in interrupt context\n");
	}
	if (filters->func_list) {
		dump_str(buf, len, "\tfunction list\n");
	}
	
	dump_str(buf, len, "\nFilter Counters:\n");

	if (filters->min_delta || filters->max_delta) {
		dump_str(buf, len, "\nExecution time filter count = %d\n",
			 filters->cnt.delta);
	}
	if (filters->no_ints) {
		dump_str(buf, len,
			 "No Interrupt functions filter count = %d\n",
			 filters->cnt.no_ints);
	}
	if (filters->only_ints) {
		dump_str(buf, len,
			 "Only Interrupt functions filter count = %d\n",
			 filters->cnt.only_ints);
	}
	if (filters->func_list_size) {
		dump_str(buf, len, "Function List filter count = %d\n",
			 filters->cnt.func_list);
	}
	dump_str(buf, len, "Total entries filtered = %d\n",
		 filters->cnt.delta +
		 filters->cnt.no_ints +
		 filters->cnt.only_ints +
		 filters->cnt.func_list);
	dump_str(buf, len, "Entries not found = %d\n", run->notfound);
	dump_str(buf, len, "\nNumber of entries after filters = %d\n\n",
		 run->next_entry);

	len += print_trigger(buf, len, &run->start_trigger, 1);
	len += print_trigger(buf, len, &run->stop_trigger, 0);
	
	/* print out header */
	dump_str(buf, len, "\n");
	dump_str(buf, len,
		 " Entry      Delta       PID      Function    Caller\n");
	dump_str(buf, len,
		 "--------   --------   --------   --------   --------\n");

	for (i=0; i < run->next_entry; i++) {
		dump_str(buf, len, "%8lu   %8lu   %7d%s   %08lx   %08lx\n",
			 run->log[i].time,
			 run->log[i].delta,
			 run->log[i].pid,
			 (run->log[i].pid == INTR_CONTEXT) ? "i" : " ",
			 (unsigned long)run->log[i].va,
			 (unsigned long)run->log[i].call_site);
	}

	return len;
}

/*
 * start of /proc/kft control handler stuff
 */

static struct proc_dir_entry *kft_proc_file;

#define tok_match(tok, str) (strncmp(tok, str, strlen(str))==0)

/* move pos to next white space */
static void __noinstrument skip_token(const char **pos)
{
	size_t non_white_count;

	/* return pointer to next white space, or \0 */
	if (*pos) {
		non_white_count = strcspn(*pos, " \t\n");
		*pos = *pos + non_white_count;	
	}
}

/*
 * return pointer to next non-white-space,
 * advancing position to next white space following that
 */
static const char __noinstrument *next_token(const char **pos)
{
	size_t white_count;
	const char *tok;

	/* return pointer to next non-white space, or \0 */
	if (*pos) {
		white_count = strspn(*pos, " \t\n");
		*pos = *pos + white_count;	
	}
	tok = *pos;
	skip_token(pos);
	return tok;
	
}

static int __noinstrument parse_func(const char **pos, void **func_addr)
{
	int ret;

	ret = sscanf(*pos, "%x", (int *)func_addr);
	skip_token(pos);
	if (ret != 1) {
		return -EINVAL;
	} else {
		return 0;
	}
}

/*
 * parse_trigger: syntax is: trigger start|stop entry|exit|time arg
 * arg for time is decimal number (usecs)
 * arg for entry or exit is hexadecimal (function address)
 */
static int __noinstrument parse_trigger(const char **pos, struct kft_run *run)
{
	const char *tok;
	struct kft_trigger *trigger;
	int ret, rcode = 0;
	
	/* parse event-type (start or stop) */
	tok = next_token(pos);
	if tok_match(tok, "start") {
		trigger = &run->start_trigger;
	} else {
		if tok_match(tok, "stop") {
			trigger = &run->stop_trigger;
		} else {
			printk("Error: missing trigger event-type\n");
			return -EINVAL;
		}
	}

	/* parse type (entry, exit, time)*/
	tok = next_token(pos);
	if tok_match(tok, "time") {
		trigger->type = TRIGGER_TIME;
		tok = next_token(pos);
		ret = sscanf(tok, "%lu", &trigger->time);
		if (ret != 1) {
			printk("Error: can't parse trigger time\n");
			rcode = -EINVAL;
		}
	} else {
		if tok_match(tok, "entry") {
			trigger->type = TRIGGER_FUNC_ENTRY;
			rcode = parse_func(pos, &trigger->func_addr);
		} else {
			if tok_match(tok, "exit") {
				trigger->type = TRIGGER_FUNC_EXIT;
				rcode = parse_func(pos, &trigger->func_addr);
			}
		}
		if (rcode)
			printk("Error: can't parse trigger function\n");
	}
	return rcode;
}

/*
 * parse_filter: syntax is: filter mintime|maxtime|noints|onlyints|funclist [args]
 * arg for time (mintime or maxtime) is decimal number (usecs)
 * arg for funclist is list of function addresses, followed by "fend"
 * e.g. filter funclist c0008000 c0008110 fend
 */
static int __noinstrument parse_filter(const char **pos, struct kft_run *run)
{
	const char *tok;
	const char *pos_save; 
	int i, ret, rcode = 0;
	int parsed = 0;
	
	/* parse filter type */
	tok = next_token(pos);
	if tok_match(tok, "noints") {
		run->filters.no_ints = 1;
		parsed = 1;
	}
	if tok_match(tok, "onlyints") {
		run->filters.only_ints = 1;
		parsed = 1;
	}
	if tok_match(tok, "mintime") {
		tok = next_token(pos);
		ret = sscanf(tok, "%lu", &run->filters.min_delta);
		if (ret != 1) {
			printk("Error: can't parse filter mintime\n");
			rcode = -EINVAL;
		}
		parsed = 1;
	}
	if tok_match(tok, "maxtime") {
		tok = next_token(pos);
		ret = sscanf(tok, "%lu", &run->filters.max_delta);
		if (ret != 1) {
			printk("Error: can't parse filter maxtime\n");
			rcode = -EINVAL;
		}
		parsed = 1;
	}
	if tok_match(tok, "funclist") {
		pos_save = *pos;

		/* count number of functions */
		i = 0;
		tok = next_token(pos);
		while (*pos && !tok_match(tok, "fend")) {
			i++;
			skip_token(pos);
			tok = next_token(pos);
		}
		if (!tok_match(tok, "fend")) {
			printk("Error: missing \"fend\" in filter funclist\n");
			return -EINVAL;
		}
		/* allocate space for functions */
		run->filters.func_list = kmalloc( sizeof(void *) * i, GFP_KERNEL);
		run->filters.func_list_size = i;
		
		/* parse functions */
		*pos = pos_save; /* rewind to beginning of funclist */
		tok = next_token(pos);
		i = 0;
		while (**pos && !tok_match(tok, "fend")) {
			rcode = parse_func(&tok, &run->filters.func_list[i]);
			i++;
			if (rcode) {
				printk("Error: can't parse function %d in filter "
					"funclist\n", i+1);
				break;
			}
			tok = next_token(pos);
		}
		parsed = 1;
	}
	
	if (!parsed) {
		printk("Error: unknown filter type. (tok=%s)\n", tok);
		rcode = -EINVAL;
	}

	return rcode;
}


static int __noinstrument kft_parse_config(const char *config, struct kft_run *run)
{
	const char *tok;
	int ret, rcode;
	const char **pos;

	pos = &config;
	tok = next_token(pos);
	while (**pos && !tok_match(tok, "end")) {
		if (tok_match(tok, "trigger")) {
			rcode = parse_trigger(pos, run);
			if (rcode) {
				return rcode;
			}
		}
		if (tok_match(tok, "filter")) {
			rcode = parse_filter(pos, run);
			if (rcode) {
				return rcode;
			}
		}
		if (tok_match(tok, "logentries")) {
			tok = next_token(pos);
			ret = sscanf(tok, "%d", &run->num_entries);
			if (ret != 1) {
				printk("Error: bad logentries.\n");
				return -EINVAL;
			}
		}
		if (tok_match(tok, "autorepeat")) {
			run->flags |= KFT_MODE_AUTO_REPEAT;
		}
		tok = next_token(pos);
	}
	if (!tok_match(tok, "end")) {
		printk("Error: missing \"end\" statement\n");
		return -EINVAL;
	}
	return 0;
}

static int __noinstrument print_trigger_config(char *buf, int len, char *ss, struct kft_trigger *t)
{
	
	char *ts = "  trigger";
	
	switch (t->type) {
	case TRIGGER_TIME:
		dump_str(buf, len, "%s %s at time %lu\n", ts, ss, t->time);
		break;
	case TRIGGER_FUNC_ENTRY:
		dump_str(buf, len, "%s %s entry 0x%08lX\n", ts, ss,
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_FUNC_EXIT:
		dump_str(buf, len, "%s %s exit 0x%08lX\n", ts, ss,	
			(unsigned long)t->func_addr);
		break;
	case TRIGGER_NONE:
		dump_str(buf, len, "%s %s not set\n", ts, ss);
		break;
	case TRIGGER_USER:
		dump_str(buf, len, "%s %s by user action\n", ts, ss);
		break;
	case TRIGGER_LOG_FULL:
		dump_str(buf, len, "%s %s by log full\n", ts, ss);
		break;
	default:
		dump_str(buf, len, "%s %s ???\n", ts, ss);
		break;
	}
	return len;
}

static int __noinstrument dump_config(char *buf, struct kft_run *run)
{
	int i, len = 0;

	/* print status information */
	dump_str(buf, len, "status: run id %d, %sprimed, %striggered, %scomplete\n\n",
		run->id, run->primed?"":"not ", run->triggered?"":"not ",
		run->complete?"":"not ");

	//run->flags = KFT_MODE_TIMED; /* and NOT KFT_MODE_OVERWRITE */
	dump_str(buf, len, "config:\n");
	dump_str(buf, len, "  mode %d\n", run->flags);

	/* triggers */
	len = print_trigger_config(buf, len, "start", &run->start_trigger);
	len = print_trigger_config(buf, len, "stop", &run->stop_trigger);

	/* filters */
	dump_str(buf, len, "  filter mintime %lu\n", run->filters.min_delta);
	dump_str(buf, len, "  filter maxtime %lu\n", run->filters.max_delta);
	if (run->filters.no_ints) {
		dump_str(buf, len, "  filter noints\n");
	}
	if (run->filters.only_ints) {
		dump_str(buf, len, "  filter onlyints\n");
	}
	if (run->filters.func_list) {
		dump_str(buf, len, "  filter funclist ");
		for (i=0; i<run->filters.func_list_size; i++ ) {
			dump_str(buf, len, "0x%08lX ",
				(unsigned long)run->filters.func_list[i]);
		}
		dump_str(buf, len, "fend\n");
	}

	/* misc stuff */
	dump_str(buf, len, "  logentries %d\n", run->num_entries);
	return len;
}


static int __noinstrument kft_new_run(const char *run_config_str)
{
	unsigned long flags;
	int rcode;
	int req_entries;

	struct kft_run *run;

	run = (struct kft_run*)kmalloc(sizeof(struct kft_run), GFP_KERNEL);
	if (!run) {
		printk(KERN_ERR "Error allocating space for new kft_run struct\n");
		return -ENOMEM;
	}

	/* set up a new run by parsing config_str */
	/* set default configuration to handle any un-set entries */
	run->primed = run->triggered = run->complete = 0;
	run->flags = KFT_MODE_TIMED; /* and NOT KFT_MODE_OVERWRITE */
	run->start_trigger.type = TRIGGER_NONE;
	run->start_trigger.func_addr = NULL;
	run->stop_trigger.type = TRIGGER_NONE;
	run->stop_trigger.func_addr = NULL;
	run->filters.min_delta = 0;
	run->filters.max_delta = 0;
	run->filters.no_ints = 0;
	run->filters.only_ints = 0;
	run->filters.func_list = NULL;
	run->filters.func_list_size = 0;
	run->num_entries = DEFAULT_RUN_LOG_ENTRIES;
	run->next_entry = 0;

	rcode = kft_parse_config(run_config_str, run);
	if (rcode) {
		kfree(run);
		printk("KFT: Could not configure new kft run");
		return rcode;
	}
		
	/* FIXTHIS - should sanity check some of the values before continuing */

	/* reset stat counters */
	memset(&run->filters.cnt, 0, sizeof(run->filters.cnt));
	run->notfound = 0;

	/* allocate log */
	/* try kmalloc first.  If that fails, try vmalloc and reducing the size */
	req_entries = run->num_entries;
	run->log = (struct kft_entry *)
		kmalloc(sizeof(struct kft_entry) * run->num_entries, GFP_KERNEL);
	run->log_is_kmem = 1;
	while(run->log==NULL && run->num_entries > 100) {
		run->log = (struct kft_entry *)
			vmalloc(sizeof(struct kft_entry) * run->num_entries);
		run->log_is_kmem = 0;
		if( run->log == NULL ) {
			run->num_entries /= 2;
		}
	}
	if (run->log == NULL) {
		printk("KFT: Could not allocate %u bytes for kft log.\n",
			(unsigned int)(sizeof(struct kft_entry) * run->num_entries));
		rcode = -ENOMEM;
		goto free_stuff_out;
	}
	/* report if the log shrunk from what was requested */
	if (run->num_entries != req_entries) {
		printk("Allocated %d log entries (%d were requested).\n",
			run->num_entries, req_entries);
	}

	//printk("log=%p\n", run->log);
	memset(run->log, 0, sizeof(struct kft_entry) * run->num_entries);
	
	/* set the run id */
	if (!run_curr) {
		run->id = 0;
	} else {
		run->id = run_curr->id + 1;
	}

	dump_config(NULL, run);

	/* install then new run as current run */
	local_irq_save(flags);
	if (run_curr && run_curr != (struct kft_run *)&kft_run0) {
		/* free the old run, if it wasn't a static run */
		if (run_curr->filters.func_list) {
			kfree(run_curr->filters.func_list);
		}
		if (run_curr->log_is_kmem) {
			kfree(run_curr->log);
		} else {
			vfree(run_curr->log);
		}
		kfree(run_curr);
	}
	run_curr = run;
	local_irq_restore(flags);
	printk("KFT: new kft run installed\n");
	return 0;

free_stuff_out:
	kfree(run->filters.func_list);
	if (run->log_is_kmem) {
		kfree(run->log);
	} else {
		vfree(run->log);
	}
	kfree(run);
	return rcode;
}

/*
 * start the current run
 */
static int __noinstrument kft_start(void)
{
	unsigned long flags;
	struct kft_run* run;

	local_irq_save(flags);
	run = run_curr;
	/* missing, done, or already started? */
	if (!run || run->complete || run->triggered) {
		local_irq_restore(flags);
		return -EINVAL;
	}
	run->triggered = 1;
	run->start_trigger.mark = update_usecs_since_boot();
	run->start_trigger.type = TRIGGER_USER;
	local_irq_restore(flags);
	return 0;
}

static int __noinstrument kft_prime(void)
{
	unsigned long flags;
	struct kft_run* run;

	local_irq_save(flags);
	run = run_curr;
	/* missing, or currently running? */
	if (!run || (run->triggered && !run->complete)) {
		local_irq_restore(flags);
		return -EINVAL;
	}
	run->primed = 1;
	local_irq_restore(flags);
	return 0;
}

/*
 * stop the current run
 */
static int __noinstrument kft_stop(void)
{
	unsigned long flags;
	struct kft_run* run;

	local_irq_save(flags);
	run = run_curr;
	/* missing or already done? */
	if (!run || run->complete) {
		local_irq_restore(flags);
		return -EINVAL;
	}
	run->complete = 1;
	run->stop_trigger.mark = update_usecs_since_boot();
	run->stop_trigger.type = TRIGGER_USER;
	local_irq_restore(flags);
	return 0;
}

static int __noinstrument proc_read_kft(char *page, char **start, off_t off, int count, int *eof,
	void *data)
{
	int len;
	struct kft_run* run = run_curr;

	if (!run) {
		len = 0;
		dump_str(page, len, "No logging run registered\n");
	} else {
		len = dump_config(page, run);
	}

	/* uncomment the next few lines to debug the synchronization mechanisms */
	/*
	dump_str(page, len, "drop_count=%d\n", atomic_read(&drop_count));
	dump_str(page, len, "entry timeout count=%d\n", atomic_read(&lock_timeout_entry_count));
	dump_str(page, len, "exit timeout count=%d\n", atomic_read(&lock_timeout_exit_count));
	*/
	return len;
}

static int __noinstrument proc_write_kft(struct file *file, const char *buffer,
	unsigned long count, void *data)
{
	int rcode = 0;
	static char cmd_buffer[COMMAND_BUFFER_LEN];

	if (count > COMMAND_BUFFER_LEN) {
		return -EINVAL;
	}

	/* FIXTHIS - do I need a verify_area() here? */
	if (copy_from_user(cmd_buffer, buffer, count)) {
		return -EFAULT;
	}
	cmd_buffer[count] = '\0';

	if (strncmp(cmd_buffer, "prime", 5)==0 ) {
		rcode = kft_prime();
	}

	if (strncmp(cmd_buffer, "start", 5)==0 ) {
		rcode = kft_start();
	}

	if (strncmp(cmd_buffer, "stop", 4)==0 ) {
		rcode = kft_stop();
	}

	if (strncmp(cmd_buffer, "new", 3)==0 ) {
		rcode = kft_new_run(cmd_buffer+3);
	}

	if (rcode) {
		return rcode;
	} else {
		return count;
	}
}

/*
 * stuff for /proc/kft_data
 */
static DECLARE_MUTEX(kft_run_mutex);

static void * __noinstrument k_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	struct kft_run* run = run_curr;
	struct kft_filters* filters = &run->filters;

	down(&kft_run_mutex);
	/*
	 * if the file is being newly read, stop any current trace
	 */
	if (!n) {
		// FIXTHIS - stop trace
	}
	
	if (!n) {
		/* print out header */
		if (!run) {
			seq_printf(m, "No logging run registered\n");
			return NULL;
		}
			
		seq_printf(m, "\nKernel Instrumentation Run ID %d\n\n",
			run->id);

		print_trigger2(m, &run->start_trigger, 1);
		print_trigger2(m, &run->stop_trigger, 0);

		seq_puts(m, "\nFilter Counters:\n");

		if (filters->min_delta || filters->max_delta) {
			seq_printf(m, "\nExecution time filter count = %d\n",
				 filters->cnt.delta);
		}
		if (filters->no_ints) {
			seq_printf(m,
				"No Interrupt functions filter count = %d\n",
				filters->cnt.no_ints);
		}
		if (filters->only_ints) {
			seq_printf(m,
				"Only Interrupt functions filter count = %d\n",
				filters->cnt.only_ints);
		}
		if (filters->func_list_size) {
			seq_printf(m, "Function List filter count = %d\n",
				filters->cnt.func_list);
		}
		seq_printf(m, "Total entries filtered = %d\n",
			 filters->cnt.delta + filters->cnt.no_ints +
			 filters->cnt.only_ints + filters->cnt.func_list);
		seq_printf(m, "Entries not found = %d\n", run->notfound);

		seq_printf(m, "\nNumber of entries after filters = %d\n\n",
			 run->next_entry);
	
		seq_puts(m, "\n Entry    Delta     PID        Function                        Caller");
#ifdef CONFIG_KFT_SAVE_ARGS
		seq_puts(m,"                          Frame ptr     Arg 1       Arg 2       Arg 3");
#endif

		seq_puts(m, "\n-------- -------- -------- ----------------                 ------------");
#ifdef CONFIG_KFT_SAVE_ARGS
		seq_puts(m,"                       ----------  ----------  ----------  ----------");
#endif
		seq_puts(m, "\n");

	}
	if (n >= run->next_entry) {
		return NULL;
	}
	return run->log + n;
}

static void * __noinstrument k_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct kft_run* run = run_curr;

	if (++*pos >= run->next_entry) {
		return NULL;
	}
	return run->log + *pos;
}

static void __noinstrument k_stop(struct seq_file *m, void *p)
{
	up(&kft_run_mutex);
}

static int __noinstrument k_show(struct seq_file *m, void *p)
{
	struct kft_entry *entry;
	long delta;
	char cpu_str[3];
	int pid;

	entry = p;
	if (entry->delta==TIME_NOT_SET) {
		delta = -1;
	} else {
		delta = (long)entry->delta;
	}
	pid = entry->pid;
#ifdef CONFIG_SMP
	int cpu;

	/* cpu is encoded in pid in bits 24-31 */
	cpu_str[0] = '.'; 
	/* this single-digit trick only works up to 8-way */
	cpu = (pid >> 24) & 0xff;
	if (cpu==0xff) cpu = 9; /* unknown */
	cpu_str[1] = '0' + cpu; 
	if ((pid | (0xff<<24)) == INTR_CONTEXT ) {
		pid = INTR_CONTEXT;
	} else {
		pid &= 0xffffff;
	}
	cpu_str[2] = 0;
#else
	cpu_str[0] = ' '; 
	cpu_str[1] = 0;
#endif

#if BITS_PER_LONG == 64
	seq_printf(m, "%8lu %8ld %7d%s 0x%016lx               0x%016lx",
#else
	seq_printf(m, "%8lu %8ld %7d%s 0x%08lx                       0x%08lx",
#endif
			 entry->time, delta, pid, cpu_str,
			 (unsigned long)entry->va,
			 (unsigned long)entry->call_site);
#ifdef CONFIG_KFT_SAVE_ARGS
	seq_printf(m, "                         0x%08x  0x%08x  0x%08x  0x%08x", (unsigned int)entry->fp,
		(unsigned int)entry->a1, (unsigned int)entry->a2,
		(unsigned int)entry->a3);
#endif
	seq_printf(m, "\n");
	return 0;
}

struct seq_operations kft_data_op = {
	.start	= k_start,
	.next	= k_next,
	.stop	= k_stop,
	.show	= k_show
};

/*
 * end of stuff for /proc/kft_data
 */

static int __init __noinstrument kft_init(void)
{
	int rcode = 0;

	kft_proc_file = create_proc_entry("kft", 0644, NULL);
	if (kft_proc_file==NULL) {
		rcode = -ENOMEM;
		goto out;
	}
	
	kft_proc_file->data = NULL;
	kft_proc_file->read_proc = proc_read_kft;
	kft_proc_file->write_proc = proc_write_kft;
	kft_proc_file->owner = THIS_MODULE;
out:
	return rcode;
}

static void __exit __noinstrument kft_exit(void)
{
        remove_proc_entry("kft", NULL);
}

module_init(kft_init);
module_exit(kft_exit);

EXPORT_SYMBOL(__cyg_profile_func_enter);
EXPORT_SYMBOL(__cyg_profile_func_exit);

