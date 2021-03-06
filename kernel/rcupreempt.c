/*
 * Read-Copy Update mechanism for mutual exclusion, realtime implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2006
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
 *		With thanks to Esben Nielsen, Bill Huey, and Ingo Molnar
 *		for pushing me away from locks and towards counters, and
 *		to Suparna Bhattacharya for pushing me completely away
 *		from atomic instructions on the read side.
 *
 *  - Added handling of Dynamic Ticks
 *      Copyright 2007 - Paul E. Mckenney <paulmck@us.ibm.com>
 *                     - Steven Rostedt <srostedt@redhat.com>
 *
 * Papers:  http://www.rdrop.com/users/paulmck/RCU
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/cpumask.h>

/*
 * PREEMPT_RCU data structures.
 */

#define GP_STAGES 2
struct rcu_data {
	raw_spinlock_t	lock;		/* Protect rcu_data fields. */
	long		completed;	/* Number of last completed batch. */
	int		waitlistcount;
	struct rcu_head *nextlist;
	struct rcu_head **nexttail;
	struct rcu_head *waitlist[GP_STAGES];
	struct rcu_head **waittail[GP_STAGES];
	struct rcu_head *donelist;
	struct rcu_head **donetail;
};
struct rcu_ctrlblk {
	raw_spinlock_t	fliplock;	/* Protect state-machine transitions. */
	long		completed;	/* Number of last completed batch. */
};
static DEFINE_PER_CPU(struct rcu_data, rcu_data);
static struct rcu_ctrlblk rcu_ctrlblk = {
	.fliplock = RAW_SPIN_LOCK_UNLOCKED(rcu_ctrlblk.fliplock),
	.completed = 0,
};
static DEFINE_PER_CPU(int [2], rcu_flipctr) = { 0, 0 };

/*
 * States for rcu_try_flip() and friends.
 */

enum rcu_try_flip_states {
	rcu_try_flip_idle_state,	/* "I" */
	rcu_try_flip_waitack_state, 	/* "A" */
	rcu_try_flip_waitzero_state,	/* "Z" */
	rcu_try_flip_waitmb_state	/* "M" */
};
static enum rcu_try_flip_states rcu_try_flip_state = rcu_try_flip_idle_state;
static char *rcu_try_flip_state_names[] =
	{ "idle", "waitack", "waitzero", "waitmb" };

/*
 * Enum and per-CPU flag to determine when each CPU has seen
 * the most recent counter flip.
 */

enum rcu_flip_flag_values {
	rcu_flip_seen,		/* Steady/initial state, last flip seen. */
				/* Only GP detector can update. */
	rcu_flipped		/* Flip just completed, need confirmation. */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU(enum rcu_flip_flag_values, rcu_flip_flag) = rcu_flip_seen;

/*
 * Enum and per-CPU flag to determine when each CPU has executed the
 * needed memory barrier to fence in memory references from its last RCU
 * read-side critical section in the just-completed grace period.
 */

enum rcu_mb_flag_values {
	rcu_mb_done,		/* Steady/initial state, no mb()s required. */
				/* Only GP detector can update. */
	rcu_mb_needed		/* Flip just completed, need an mb(). */
				/* Only corresponding CPU can update. */
};
static DEFINE_PER_CPU(enum rcu_mb_flag_values, rcu_mb_flag) = rcu_mb_done;

static cpumask_t rcu_cpu_online_map = CPU_MASK_NONE;

/*
 * Macro that prevents the compiler from reordering accesses, but does
 * absolutely -nothing- to prevent CPUs from reordering.  This is used
 * only to mediate communication between mainline code and hardware
 * interrupt and NMI handlers.
 */
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

/*
 * RCU_DATA_ME: find the current CPU's rcu_data structure.
 * RCU_DATA_CPU: find the specified CPU's rcu_data structure.
 */
#define RCU_DATA_ME()		(&__get_cpu_var(rcu_data))
#define RCU_DATA_CPU(cpu)	(&per_cpu(rcu_data, cpu))

/*
 * Return the number of RCU batches processed thus far.  Useful
 * for debug and statistics.
 */
long rcu_batches_completed(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed);

/*
 * Return the number of RCU batches processed thus far.  Useful for debug
 * and statistics.  The _bh variant is identical to straight RCU.
 */
long rcu_batches_completed_bh(void)
{
	return rcu_ctrlblk.completed;
}
EXPORT_SYMBOL_GPL(rcu_batches_completed_bh);

void __rcu_read_lock(void)
{
	int idx;
	struct task_struct *me = current;
	int nesting;

	nesting = ACCESS_ONCE(me->rcu_read_lock_nesting);
	if (nesting != 0) {

		/* An earlier rcu_read_lock() covers us, just count it. */

		me->rcu_read_lock_nesting = nesting + 1;

	} else {
		unsigned long oldirq;

		/*
		 * Disable local interrupts to prevent the grace-period
		 * detection state machine from seeing us half-done.
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock().
		 */

		local_irq_save(oldirq);

		/*
		 * Outermost nesting of rcu_read_lock(), so increment
		 * the current counter for the current CPU.  Use volatile
		 * casts to prevent the compiler from reordering.
		 */

		idx = ACCESS_ONCE(rcu_ctrlblk.completed) & 0x1;
		smp_read_barrier_depends();  /* @@@@ might be unneeded */
		ACCESS_ONCE(__get_cpu_var(rcu_flipctr)[idx])++;

		/*
		 * Now that the per-CPU counter has been incremented, we
		 * are protected from races with rcu_read_lock() invoked
		 * from NMI handlers on this CPU.  We can therefore safely
		 * increment the nesting counter, relieving further NMIs
		 * of the need to increment the per-CPU counter.
		 */

		ACCESS_ONCE(me->rcu_read_lock_nesting) = nesting + 1;

		/*
		 * Now that we have preventing any NMIs from storing
		 * to the ->rcu_flipctr_idx, we can safely use it to
		 * remember which counter to decrement in the matching
		 * rcu_read_unlock().
		 */

		ACCESS_ONCE(me->rcu_flipctr_idx) = idx;
		local_irq_restore(oldirq);
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_lock);

void __rcu_read_unlock(void)
{
	int idx;
	struct task_struct *me = current;
	int nesting;

	nesting = ACCESS_ONCE(me->rcu_read_lock_nesting);
	if (nesting > 1) {

		/*
		 * We are still protected by the enclosing rcu_read_lock(),
		 * so simply decrement the counter.
		 */

		me->rcu_read_lock_nesting = nesting - 1;

	} else {
		unsigned long oldirq;

		/*
		 * Disable local interrupts to prevent the grace-period
		 * detection state machine from seeing us half-done.
		 * NMIs can still occur, of course, and might themselves
		 * contain rcu_read_lock() and rcu_read_unlock().
		 */

		local_irq_save(oldirq);

		/*
		 * Outermost nesting of rcu_read_unlock(), so we must
		 * decrement the current counter for the current CPU.
		 * This must be done carefully, because NMIs can
		 * occur at any point in this code, and any rcu_read_lock()
		 * and rcu_read_unlock() pairs in the NMI handlers
		 * must interact non-destructively with this code.
		 * Lots of volatile casts, and -very- careful ordering.
		 *
		 * Changes to this code, including this one, must be
		 * inspected, validated, and tested extremely carefully!!!
		 */

		/*
		 * First, pick up the index.  Enforce ordering for
		 * DEC Alpha.
		 */

		idx = ACCESS_ONCE(me->rcu_flipctr_idx);
		smp_read_barrier_depends();  /* @@@ Needed??? */

		/*
		 * Now that we have fetched the counter index, it is
		 * safe to decrement the per-task RCU nesting counter.
		 * After this, any interrupts or NMIs will increment and
		 * decrement the per-CPU counters.
		 */
		ACCESS_ONCE(me->rcu_read_lock_nesting) = nesting - 1;

		/*
		 * It is now safe to decrement this task's nesting count.
		 * NMIs that occur after this statement will route their
		 * rcu_read_lock() calls through this "else" clause, and
		 * will thus start incrementing the per-CPU coutner on
		 * their own.  They will also clobber ->rcu_flipctr_idx,
		 * but that is OK, since we have already fetched it.
		 */

		ACCESS_ONCE(__get_cpu_var(rcu_flipctr)[idx])--;

		local_irq_restore(oldirq);

		__rcu_preempt_unboost();
	}
}
EXPORT_SYMBOL_GPL(__rcu_read_unlock);

/*
 * If a global counter flip has occurred since the last time that we
 * advanced callbacks, advance them.  Hardware interrupts must be
 * disabled when calling this function.
 */
static void __rcu_advance_callbacks(struct rcu_data *rdp)
{
	int cpu;
	int i;
	int wlc = 0;

	if (rdp->completed != rcu_ctrlblk.completed) {
		if (rdp->waitlist[GP_STAGES - 1] != NULL) {
			*rdp->donetail = rdp->waitlist[GP_STAGES - 1];
			rdp->donetail = rdp->waittail[GP_STAGES - 1];
			trace_mark(rcupreempt_trace_move2done, "NULL");
		}
		for (i = GP_STAGES - 2; i >= 0; i--) {
			if (rdp->waitlist[i] != NULL) {
				rdp->waitlist[i + 1] = rdp->waitlist[i];
				rdp->waittail[i + 1] = rdp->waittail[i];
				wlc++;
			} else {
				rdp->waitlist[i + 1] = NULL;
				rdp->waittail[i + 1] =
					&rdp->waitlist[i + 1];
			}
		}
		if (rdp->nextlist != NULL) {
			rdp->waitlist[0] = rdp->nextlist;
			rdp->waittail[0] = rdp->nexttail;
			wlc++;
			rdp->nextlist = NULL;
			rdp->nexttail = &rdp->nextlist;
			trace_mark(rcupreempt_trace_move2wait, "NULL");
		} else {
			rdp->waitlist[0] = NULL;
			rdp->waittail[0] = &rdp->waitlist[0];
		}
		rdp->waitlistcount = wlc;
		rdp->completed = rcu_ctrlblk.completed;
	}

	/*
	 * Check to see if this CPU needs to report that it has seen
	 * the most recent counter flip, thereby declaring that all
	 * subsequent rcu_read_lock() invocations will respect this flip.
	 */

	cpu = raw_smp_processor_id();
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}
}

#ifdef CONFIG_NO_HZ

DEFINE_PER_CPU(long, dynticks_progress_counter) = 1;
static DEFINE_PER_CPU(long, rcu_dyntick_snapshot);
static DEFINE_PER_CPU(int, rcu_update_flag);

/**
 * rcu_irq_enter - Called from Hard irq handlers and NMI/SMI.
 *
 * If the CPU was idle with dynamic ticks active, this updates the
 * dynticks_progress_counter to let the RCU handling know that the
 * CPU is active.
 */
void rcu_irq_enter(void)
{
	int cpu = smp_processor_id();

	if (per_cpu(rcu_update_flag, cpu))
		per_cpu(rcu_update_flag, cpu)++;

	/*
	 * Only update if we are coming from a stopped ticks mode
	 * (dynticks_progress_counter is even).
	 */
	if (!in_interrupt() && (per_cpu(dynticks_progress_counter, cpu) & 0x1) == 0) {
		/*
		 * The following might seem like we could have a race
		 * with NMI/SMIs. But this really isn't a problem.
		 * Here we do a read/modify/write, and the race happens
		 * when an NMI/SMI comes in after the read and before
		 * the write. But NMI/SMIs will increment this counter
		 * twice before returning, so the zero bit will not
		 * be corrupted by the NMI/SMI which is the most important
		 * part.
		 *
		 * The only thing is that we would bring back the counter
		 * to a postion that it was in during the NMI/SMI.
		 * But the zero bit would be set, so the rest of the
		 * counter would again be ignored.
		 *
		 * On return from the IRQ, the counter may have the zero
		 * bit be 0 and the counter the same as the return from
		 * the NMI/SMI. If the state machine was so unlucky to
		 * see that, it still doesn't matter, since all
		 * RCU read-side critical sections on this CPU would
		 * have already completed.
		 */
		per_cpu(dynticks_progress_counter, cpu)++;
		/*
		 * The following memory barrier ensures that any
		 * rcu_read_lock() primitives in the irq handler
		 * are seen by other CPUs to follow the above
		 * increment to dynticks_progress_counter. This is
		 * required in order for other CPUs to correctly
		 * determine when it is safe to advance the RCU
		 * grace-period state machine.
		 */
		smp_mb(); /* see above block comment. */
		/*
		 * Since we can't determine the dynamic tick mode from
		 * the dynticks_progress_counter after this routine,
		 * we use a second flag to acknowledge that we came
		 * from an idle state with ticks stopped.
		 */
		per_cpu(rcu_update_flag, cpu)++;
		/*
		 * If we take an NMI/SMI now, they will also increment
		 * the rcu_update_flag, and will not update the
		 * dynticks_progress_counter on exit. That is for
		 * this IRQ to do.
		 */
	}
}

/**
 * rcu_irq_exit - Called from exiting Hard irq context.
 *
 * If the CPU was idle with dynamic ticks active, update the
 * dynticks_progress_counter to put let the RCU handling be
 * aware that the CPU is going back to idle with no ticks.
 */
void rcu_irq_exit(void)
{
	int cpu = smp_processor_id();

	/*
	 * rcu_update_flag is set if we interrupted the CPU
	 * when it was idle with ticks stopped.
	 * Once this occurs, we keep track of interrupt nesting
	 * because a NMI/SMI could also come in, and we still
	 * only want the IRQ that started the increment of the
	 * dynticks_progress_counter to be the one that modifies
	 * it on exit.
	 */
	if (per_cpu(rcu_update_flag, cpu)) {
		if (--per_cpu(rcu_update_flag, cpu))
			return;

		/* This must match the interrupt nesting */
		WARN_ON(in_interrupt());

		/*
		 * If an NMI/SMI happens now we are still
		 * protected by the dynticks_progress_counter being odd.
		 */

		/*
		 * The following memory barrier ensures that any
		 * rcu_read_unlock() primitives in the irq handler
		 * are seen by other CPUs to preceed the following
		 * increment to dynticks_progress_counter. This
		 * is required in order for other CPUs to determine
		 * when it is safe to advance the RCU grace-period
		 * state machine.
		 */
		smp_mb(); /* see above block comment. */
		per_cpu(dynticks_progress_counter, cpu)++;
		WARN_ON(per_cpu(dynticks_progress_counter, cpu) & 0x1);
	}
}

static void dyntick_save_progress_counter(int cpu)
{
	per_cpu(rcu_dyntick_snapshot, cpu) =
		per_cpu(dynticks_progress_counter, cpu);
}

static inline int
rcu_try_flip_waitack_needed(int cpu)
{
	long curr;
	long snap;

	curr = per_cpu(dynticks_progress_counter, cpu);
	snap = per_cpu(rcu_dyntick_snapshot, cpu);
	smp_mb(); /* force ordering with cpu entering/leaving dynticks. */

	/*
	 * If the CPU remained in dynticks mode for the entire time
	 * and didn't take any interrupts, NMIs, SMIs, or whatever,
	 * then it cannot be in the middle of an rcu_read_lock(), so
	 * the next rcu_read_lock() it executes must use the new value
	 * of the counter.  So we can safely pretend that this CPU
	 * already acknowledged the counter.
	 */

	if ((curr == snap) && ((curr & 0x1) == 0))
		return 0;

	/*
	 * If the CPU passed through or entered a dynticks idle phase with
	 * no active irq handlers, then, as above, we can safely pretend
	 * that this CPU already acknowledged the counter.
	 */

	if ((curr - snap) > 2 || (snap & 0x1) == 0)
		return 0;

	/* We need this CPU to explicitly acknowledge the counter flip. */

	return 1;
}

static inline int
rcu_try_flip_waitmb_needed(int cpu)
{
	long curr;
	long snap;

	curr = per_cpu(dynticks_progress_counter, cpu);
	snap = per_cpu(rcu_dyntick_snapshot, cpu);
	smp_mb(); /* force ordering with cpu entering/leaving dynticks. */

	/*
	 * If the CPU remained in dynticks mode for the entire time
	 * and didn't take any interrupts, NMIs, SMIs, or whatever,
	 * then it cannot have executed an RCU read-side critical section
	 * during that time, so there is no need for it to execute a
	 * memory barrier.
	 */

	if ((curr == snap) && ((curr & 0x1) == 0))
		return 0;

	/*
	 * If the CPU either entered or exited an outermost interrupt,
	 * SMI, NMI, or whatever handler, then we know that it executed
	 * a memory barrier when doing so.  So we don't need another one.
	 */
	if (curr != snap)
		return 0;

	/* We need the CPU to execute a memory barrier. */

	return 1;
}

#else /* !CONFIG_NO_HZ */

# define dyntick_save_progress_counter(cpu)	do { } while (0)
# define rcu_try_flip_waitack_needed(cpu)	(1)
# define rcu_try_flip_waitmb_needed(cpu)	(1)

#endif /* CONFIG_NO_HZ */

/*
 * Get here when RCU is idle.  Decide whether we need to
 * move out of idle state, and return non-zero if so.
 * "Straightforward" approach for the moment, might later
 * use callback-list lengths, grace-period duration, or
 * some such to determine when to exit idle state.
 * Might also need a pre-idle test that does not acquire
 * the lock, but let's get the simple case working first...
 */

static int
rcu_try_flip_idle(void)
{
	int cpu;

	trace_mark(rcupreempt_trace_try_flip_i1, "NULL");
	if (!rcu_pending(smp_processor_id())) {
		trace_mark(rcupreempt_trace_try_flip_ie1, "NULL");
		return 0;
	}

	/*
	 * Do the flip.
	 */

	trace_mark(rcupreempt_trace_try_flip_g1, "NULL");
	rcu_ctrlblk.completed++;  /* stands in for rcu_try_flip_g2 */

	/*
	 * Need a memory barrier so that other CPUs see the new
	 * counter value before they see the subsequent change of all
	 * the rcu_flip_flag instances to rcu_flipped.
	 */

	smp_mb();	/* see above block comment. */

	/* Now ask each CPU for acknowledgement of the flip. */

	for_each_cpu_mask(cpu, rcu_cpu_online_map) {
		per_cpu(rcu_flip_flag, cpu) = rcu_flipped;
		dyntick_save_progress_counter(cpu);
	}

	return 1;
}

/*
 * Wait for CPUs to acknowledge the flip.
 */

static int
rcu_try_flip_waitack(void)
{
	int cpu;

	trace_mark(rcupreempt_trace_try_flip_a1, "NULL");
	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		if (rcu_try_flip_waitack_needed(cpu) &&
		    per_cpu(rcu_flip_flag, cpu) != rcu_flip_seen) {
			trace_mark(rcupreempt_trace_try_flip_ae1, "NULL");
			return 0;
		}

	/*
	 * Make sure our checks above don't bleed into subsequent
	 * waiting for the sum of the counters to reach zero.
	 */

	smp_mb();	/* see above block comment. */
	trace_mark(rcupreempt_trace_try_flip_a2, "NULL");
	return 1;
}

/*
 * Wait for collective ``last'' counter to reach zero,
 * then tell all CPUs to do an end-of-grace-period memory barrier.
 */

static int
rcu_try_flip_waitzero(void)
{
	int cpu;
	int lastidx = !(rcu_ctrlblk.completed & 0x1);
	int sum = 0;

	/* Check to see if the sum of the "last" counters is zero. */

	trace_mark(rcupreempt_trace_try_flip_z1, "NULL");
	for_each_possible_cpu(cpu)
		sum += per_cpu(rcu_flipctr, cpu)[lastidx];
	if (sum != 0) {
		trace_mark(rcupreempt_trace_try_flip_ze1, "NULL");
		return 0;
	}

	smp_mb();  /* Don't call for memory barriers before we see zero. */

	/* Call for a memory barrier from each CPU. */

	for_each_cpu_mask(cpu, rcu_cpu_online_map) {
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_needed;
		dyntick_save_progress_counter(cpu);
	}

	trace_mark(rcupreempt_trace_try_flip_z2, "NULL");
	return 1;
}

/*
 * Wait for all CPUs to do their end-of-grace-period memory barrier.
 * Return 0 once all CPUs have done so.
 */

static int
rcu_try_flip_waitmb(void)
{
	int cpu;

	trace_mark(rcupreempt_trace_try_flip_m1, "NULL");
	for_each_cpu_mask(cpu, rcu_cpu_online_map)
		if (rcu_try_flip_waitmb_needed(cpu) &&
		    per_cpu(rcu_mb_flag, cpu) != rcu_mb_done) {
			trace_mark(rcupreempt_trace_try_flip_me1, "NULL");
			return 0;
		}

	smp_mb(); /* Ensure that the above checks precede any following flip. */
	trace_mark(rcupreempt_trace_try_flip_m2, "NULL");
	return 1;
}

/*
 * Attempt a single flip of the counters.  Remember, a single flip does
 * -not- constitute a grace period.  Instead, the interval between
 * at least GP_STAGES+2 consecutive flips is a grace period.
 *
 * If anyone is nuts enough to run this CONFIG_PREEMPT_RCU implementation
 * on a large SMP, they might want to use a hierarchical organization of
 * the per-CPU-counter pairs.
 */
static void rcu_try_flip(void)
{
	unsigned long oldirq;

	trace_mark(rcupreempt_trace_try_flip_1, "NULL");
	if (unlikely(!spin_trylock_irqsave(&rcu_ctrlblk.fliplock, oldirq))) {
		trace_mark(rcupreempt_trace_try_flip_e1, "NULL");
		return;
	}

	/*
	 * Take the next transition(s) through the RCU grace-period
	 * flip-counter state machine.
	 */

	switch (rcu_try_flip_state) {
	case rcu_try_flip_idle_state:
		if (rcu_try_flip_idle())
			rcu_try_flip_state = rcu_try_flip_waitack_state;
		break;
	case rcu_try_flip_waitack_state:
		if (rcu_try_flip_waitack())
			rcu_try_flip_state = rcu_try_flip_waitzero_state;
		break;
	case rcu_try_flip_waitzero_state:
		if (rcu_try_flip_waitzero())
			rcu_try_flip_state = rcu_try_flip_waitmb_state;
		break;
	case rcu_try_flip_waitmb_state:
		if (rcu_try_flip_waitmb())
			rcu_try_flip_state = rcu_try_flip_idle_state;
	}
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);
}

/*
 * Check to see if this CPU needs to do a memory barrier in order to
 * ensure that any prior RCU read-side critical sections have committed
 * their counter manipulations and critical-section memory references
 * before declaring the grace period to be completed.
 */
static void rcu_check_mb(int cpu)
{
	if (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed) {
		smp_mb();  /* Ensure RCU read-side accesses are visible. */
		per_cpu(rcu_mb_flag, cpu) = rcu_mb_done;
	}
}

void rcu_check_callbacks_rt(int cpu, int user)
{
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	rcu_check_mb(cpu);
	if (rcu_ctrlblk.completed == rdp->completed)
		rcu_try_flip();
	spin_lock_irqsave(&rdp->lock, oldirq);
	trace_mark(rcupreempt_trace_check_callbacks, "NULL");
	__rcu_advance_callbacks(rdp);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

/*
 * Needed by dynticks, to make sure all RCU processing has finished
 * when we go idle.  (Currently unused, needed?)
 */
void rcu_advance_callbacks_rt(int cpu, int user)
{
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	if (rcu_ctrlblk.completed == rdp->completed) {
		rcu_try_flip();
		if (rcu_ctrlblk.completed == rdp->completed)
			return;
	}
	spin_lock_irqsave(&rdp->lock, oldirq);
	trace_mark(rcupreempt_trace_check_callbacks, "NULL");
	__rcu_advance_callbacks(rdp);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

#ifdef CONFIG_HOTPLUG_CPU

#define rcu_offline_cpu_rt_enqueue(srclist, srctail, dstlist, dsttail) do { \
		*dsttail = srclist; \
		if (srclist != NULL) { \
			dsttail = srctail; \
			srclist = NULL; \
			srctail = &srclist;\
		} \
	} while (0)


void rcu_offline_cpu_rt(int cpu)
{
	int i;
	struct rcu_head *list = NULL;
	unsigned long oldirq;
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);
	struct rcu_head **tail = &list;

	/* Remove all callbacks from the newly dead CPU, retaining order. */

	spin_lock_irqsave(&rdp->lock, oldirq);
	rcu_offline_cpu_rt_enqueue(rdp->donelist, rdp->donetail, list, tail);
	for (i = GP_STAGES - 1; i >= 0; i--)
		rcu_offline_cpu_rt_enqueue(rdp->waitlist[i], rdp->waittail[i],
					   list, tail);
	rcu_offline_cpu_rt_enqueue(rdp->nextlist, rdp->nexttail, list, tail);
	spin_unlock_irqrestore(&rdp->lock, oldirq);
	rdp->waitlistcount = 0;

	/* Disengage the newly dead CPU from grace-period computation. */

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, oldirq);
	rcu_check_mb(cpu);
	if (per_cpu(rcu_flip_flag, cpu) == rcu_flipped) {
		smp_mb();  /* Subsequent counter accesses must see new value */
		per_cpu(rcu_flip_flag, cpu) = rcu_flip_seen;
		smp_mb();  /* Subsequent RCU read-side critical sections */
			   /*  seen -after- acknowledgement. */
	}
	cpu_clear(cpu, rcu_cpu_online_map);
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);

	/*
	 * Place the removed callbacks on the current CPU's queue.
	 * Make them all start a new grace period: simple approach,
	 * in theory could starve a given set of callbacks, but
	 * you would need to be doing some serious CPU hotplugging
	 * to make this happen.  If this becomes a problem, adding
	 * a synchronize_rcu() to the hotplug path would be a simple
	 * fix.
	 */

	rdp = RCU_DATA_ME();
	spin_lock_irqsave(&rdp->lock, oldirq);
	*rdp->nexttail = list;
	if (list)
		rdp->nexttail = tail;
	spin_unlock_irqrestore(&rdp->lock, oldirq);
}

void __devinit rcu_online_cpu_rt(int cpu)
{
	unsigned long oldirq;

	spin_lock_irqsave(&rcu_ctrlblk.fliplock, oldirq);
	cpu_set(cpu, rcu_cpu_online_map);
	spin_unlock_irqrestore(&rcu_ctrlblk.fliplock, oldirq);
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

void rcu_offline_cpu(int cpu)
{
}

void __devinit rcu_online_cpu_rt(int cpu)
{
}

#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */

void rcu_process_callbacks_rt(struct softirq_action *unused)
{
	unsigned long flags;
	struct rcu_head *next, *list;
	struct rcu_data *rdp = RCU_DATA_ME();

	spin_lock_irqsave(&rdp->lock, flags);
	list = rdp->donelist;
	if (list == NULL) {
		spin_unlock_irqrestore(&rdp->lock, flags);
		return;
	}
	rdp->donelist = NULL;
	rdp->donetail = &rdp->donelist;
	trace_mark(rcupreempt_trace_done_remove, "NULL");
	spin_unlock_irqrestore(&rdp->lock, flags);
	while (list) {
		next = list->next;
		list->func(list);
		list = next;
		trace_mark(rcupreempt_trace_invoke, "NULL");
	}
}

void fastcall call_rcu_preempt(struct rcu_head *head,
			       void (*func)(struct rcu_head *rcu))
{
	unsigned long oldirq;
	struct rcu_data *rdp;

	head->func = func;
	head->next = NULL;
	local_irq_save(oldirq);
	rdp = RCU_DATA_ME();
	spin_lock(&rdp->lock);
	__rcu_advance_callbacks(rdp);
	*rdp->nexttail = head;
	rdp->nexttail = &head->next;
	trace_mark(rcupreempt_trace_next_add, "NULL");
	spin_unlock(&rdp->lock);
	local_irq_restore(oldirq);
}
EXPORT_SYMBOL_GPL(call_rcu_preempt);

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  Assumes that notifiers would take care of handling any
 * outstanding requests from the RCU core.
 *
 * This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 */
int rcu_needs_cpu_rt(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	return (rdp->donelist != NULL ||
		!!rdp->waitlistcount ||
		rdp->nextlist != NULL);
}

int rcu_pending_rt(int cpu)
{
	struct rcu_data *rdp = RCU_DATA_CPU(cpu);

	/* The CPU has at least one callback queued somewhere. */

	if (rdp->donelist != NULL ||
	    !!rdp->waitlistcount ||
	    rdp->nextlist != NULL)
		return 1;

	/* The RCU core needs an acknowledgement from this CPU. */

	if ((per_cpu(rcu_flip_flag, cpu) == rcu_flipped) ||
	    (per_cpu(rcu_mb_flag, cpu) == rcu_mb_needed))
		return 1;

	/* This CPU has fallen behind the global grace-period number. */

	if (rdp->completed != rcu_ctrlblk.completed)
		return 1;

	/* Nothing needed from this CPU. */

	return 0;
}

void __init rcu_init_rt(void)
{
	int cpu;
	int i;
	struct rcu_data *rdp;

/*&&&&*/printk(KERN_NOTICE "WARNING: experimental RCU implementation.\n");
	for_each_possible_cpu(cpu) {
		rdp = RCU_DATA_CPU(cpu);
		spin_lock_init(&rdp->lock);
		rdp->completed = 0;
		rdp->waitlistcount = 0;
		rdp->nextlist = NULL;
		rdp->nexttail = &rdp->nextlist;
		for (i = 0; i < GP_STAGES; i++) {
			rdp->waitlist[i] = NULL;
			rdp->waittail[i] = &rdp->waitlist[i];
		}
		rdp->donelist = NULL;
		rdp->donetail = &rdp->donelist;
	}
	rcu_preempt_boost_init();
}

/*
 * Deprecated, use synchronize_rcu() or synchronize_sched() instead.
 */
void synchronize_kernel(void)
{
	synchronize_rcu();
}

int *rcupreempt_flipctr(int cpu)
{
	return &per_cpu(rcu_flipctr, cpu)[0];
}
EXPORT_SYMBOL_GPL(rcupreempt_flipctr);

int rcupreempt_flip_flag(int cpu)
{
	return per_cpu(rcu_flip_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_flip_flag);

int rcupreempt_mb_flag(int cpu)
{
	return per_cpu(rcu_mb_flag, cpu);
}
EXPORT_SYMBOL_GPL(rcupreempt_mb_flag);

char *rcupreempt_try_flip_state_name(void)
{
	return rcu_try_flip_state_names[rcu_try_flip_state];
}
EXPORT_SYMBOL_GPL(rcupreempt_try_flip_state_name);
