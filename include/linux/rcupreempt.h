/*
 * Read-Copy Update mechanism for mutual exclusion (RT implementation)
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
 * Copyright (C) IBM Corporation, 2006
 *
 * Author:  Paul McKenney <paulmck@us.ibm.com>
 *
 * Based on the original work by Paul McKenney <paul.mckenney@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU
 *
 */

#ifndef __LINUX_RCUPREEMPT_H
#define __LINUX_RCUPREEMPT_H

#ifdef __KERNEL__

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

#ifdef CONFIG_PREEMPT_RCU_BOOST
/*
 * Task state with respect to being RCU-boosted.  This state is changed
 * by the task itself in response to the following three events:
 * 1. Preemption (or block on lock) while in RCU read-side critical section.
 * 2. Outermost rcu_read_unlock() for blocked RCU read-side critical section.
 *
 * The RCU-boost task also updates the state when boosting priority.
 */
enum rcu_boost_state {
	RCU_BOOST_IDLE = 0,	   /* Not yet blocked if in RCU read-side. */
	RCU_BOOST_BLOCKED = 1,	   /* Blocked from RCU read-side. */
	RCU_BOOSTED = 2,	   /* Boosting complete. */
	RCU_BOOST_INVALID = 3,	   /* For bogus state sightings. */
};

#define N_RCU_BOOST_STATE (RCU_BOOST_INVALID + 1)

int __init rcu_preempt_boost_init(void);

#else /* CONFIG_PREEPMT_RCU_BOOST */

#define rcu_preempt_boost_init() do { } while (0)

#endif /* CONFIG_PREEMPT_RCU_BOOST */

/*
 * Someone might want to pass call_rcu_bh as a function pointer.
 * So this needs to just be a rename and not a macro function.
 *  (no parentheses)
 */
#define call_rcu_bh	 	call_rcu_preempt
#define rcu_bh_qsctr_inc(cpu)	do { } while (0)
#define __rcu_read_lock_bh()	{ rcu_read_lock(); local_bh_disable(); }
#define __rcu_read_unlock_bh()	{ local_bh_enable(); rcu_read_unlock(); }
#define __rcu_read_lock_nesting()	(current->rcu_read_lock_nesting)

extern void FASTCALL(call_rcu_classic(struct rcu_head *head,
		     void (*func)(struct rcu_head *head)));
extern void FASTCALL(call_rcu_preempt(struct rcu_head *head,
		     void (*func)(struct rcu_head *head)));
extern void __rcu_read_lock(void);
extern void __rcu_read_unlock(void);
extern void __synchronize_sched(void);
extern void rcu_advance_callbacks_rt(int cpu, int user);
extern void rcu_check_callbacks_rt(int cpu, int user);
extern void rcu_init_rt(void);
extern int  rcu_needs_cpu_rt(int cpu);
extern void rcu_offline_cpu_rt(int cpu);
extern void rcu_online_cpu_rt(int cpu);
extern int  rcu_pending_rt(int cpu);
struct softirq_action;
extern void rcu_process_callbacks_rt(struct softirq_action *unused);

struct softirq_action;

#ifdef CONFIG_NO_HZ
DECLARE_PER_CPU(long, dynticks_progress_counter);

static inline void rcu_enter_nohz(void)
{
	__get_cpu_var(dynticks_progress_counter)++;
	if (unlikely(__get_cpu_var(dynticks_progress_counter) & 0x1)) {
		printk("BUG: bad accounting of dynamic ticks\n");
		printk("   will try to fix, but it is best to reboot\n");
		WARN_ON(1);
		/* try to fix it */
		__get_cpu_var(dynticks_progress_counter)++;
	}
	mb();
}

static inline void rcu_exit_nohz(void)
{
	mb();
	__get_cpu_var(dynticks_progress_counter)++;
	if (unlikely(!(__get_cpu_var(dynticks_progress_counter) & 0x1))) {
		printk("BUG: bad accounting of dynamic ticks\n");
		printk("   will try to fix, but it is best to reboot\n");
		WARN_ON(1);
		/* try to fix it */
		__get_cpu_var(dynticks_progress_counter)++;
	}
}

#else /* CONFIG_NO_HZ */
#define rcu_enter_nohz()	do { } while (0)
#define rcu_exit_nohz()		do { } while (0)
#endif /* CONFIG_NO_HZ */

extern void rcu_process_callbacks(struct softirq_action *unused);

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPREEMPT_H */
