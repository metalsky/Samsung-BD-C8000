#
# Makefile for the linux kernel.
#

obj-y     = lspinfo.o sched.o fork.o exec_domain.o panic.o printk.o profile.o \
	    exit.o itimer.o time.o softirq.o resource.o \
	    sysctl.o capability.o ptrace.o timer.o user.o user_namespace.o \
	    signal.o sys.o kmod.o workqueue.o pid.o \
	    rcupdate.o extable.o params.o posix-timers.o \
	    kthread.o wait.o kfifo.o sys_ni.o posix-cpu-timers.o \
	    hrtimer.o rwsem.o latency.o nsproxy.o srcu.o \
	    utsname.o notifier.o

#Add for KDEBUGD
obj-$(CONFIG_KDEBUGD)   += kdebugd/

#Add for DTVLOGD
obj-$(CONFIG_DTVLOGD)   += dtvlogd.o

#Add for RAMZSWAP
obj-$(CONFIG_RAMZSWAP)  += ramzswap/

obj-$(CONFIG_SYSCTL) += sysctl_check.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-y += time/
ifneq ($(CONFIG_PREEMPT_RT),y)
obj-y += mutex.o
obj-$(CONFIG_DEBUG_MUTEXES) += mutex-debug.o
endif
obj-$(CONFIG_LOCKDEP) += lockdep.o
ifeq ($(CONFIG_PROC_FS),y)
obj-$(CONFIG_LOCKDEP) += lockdep_proc.o
endif
obj-$(CONFIG_FUTEX) += futex.o
ifeq ($(CONFIG_COMPAT),y)
obj-$(CONFIG_FUTEX) += futex_compat.o
endif
obj-$(CONFIG_RT_MUTEXES) += rtmutex.o
obj-$(CONFIG_DEBUG_RT_MUTEXES) += rtmutex-debug.o
obj-$(CONFIG_RT_MUTEX_TESTER) += rtmutex-tester.o
obj-$(CONFIG_PREEMPT_RT) += rt.o
obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o
obj-$(CONFIG_SMP) += cpu.o spinlock.o
obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock.o
obj-$(CONFIG_PROVE_LOCKING) += spinlock.o
obj-$(CONFIG_UID16) += uid16.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_KALLSYMS) += kallsyms.o
obj-$(CONFIG_PM) += power/
obj-$(CONFIG_BSD_PROCESS_ACCT) += acct.o
obj-$(CONFIG_KEXEC) += kexec.o
obj-$(CONFIG_COMPAT) += compat.o
obj-$(CONFIG_CGROUPS) += cgroup.o
obj-$(CONFIG_CGROUP_DEBUG) += cgroup_debug.o
obj-$(CONFIG_CPUSETS) += cpuset.o
obj-$(CONFIG_CGROUP_NS) += ns_cgroup.o
obj-$(CONFIG_IKCONFIG) += configs.o
obj-$(CONFIG_STOP_MACHINE) += stop_machine.o
obj-$(CONFIG_DEBUG_PREEMPT) += latency_trace.o
obj-$(CONFIG_WAKEUP_TIMING) += latency_trace.o
obj-$(CONFIG_EVENT_TRACE) += latency_trace.o
obj-$(CONFIG_CRITICAL_TIMING) += latency_trace.o
obj-$(CONFIG_LATENCY_HIST) += latency_hist.o
obj-$(CONFIG_AUDIT) += audit.o auditfilter.o
obj-$(CONFIG_AUDITSYSCALL) += auditsc.o
obj-$(CONFIG_AUDIT_TREE) += audit_tree.o
obj-$(CONFIG_KPROBES) += kprobes.o
obj-$(CONFIG_KGDB) += kgdb.o
obj-$(CONFIG_SYSFS) += ksysfs.o
obj-$(CONFIG_DETECT_SOFTLOCKUP) += softlockup.o
obj-$(CONFIG_KFT) += kft.o
obj-$(CONFIG_KFT_STATIC_RUN) += kftstatic.o
obj-$(CONFIG_GENERIC_HARDIRQS) += irq/
obj-$(CONFIG_SECCOMP) += seccomp.o
obj-$(CONFIG_RCU_TORTURE_TEST) += rcutorture.o
obj-$(CONFIG_CLASSIC_RCU) += rcuclassic.o
obj-$(CONFIG_PREEMPT_RCU) += rcuclassic.o rcupreempt.o
obj-$(CONFIG_PREEMPT_RCU_BOOST) += rcupreempt-boost.o
ifeq ($(CONFIG_PREEMPT_RCU),y)
obj-$(CONFIG_RCU_TRACE) += rcupreempt_trace.o
endif
obj-$(CONFIG_RELAY) += relay.o
obj-$(CONFIG_SYSCTL) += utsname_sysctl.o
obj-$(CONFIG_TASK_DELAY_ACCT) += delayacct.o
obj-$(CONFIG_TASKSTATS) += taskstats.o tsacct.o
obj-$(CONFIG_IMMEDIATE) += immediate.o
obj-$(CONFIG_MARKERS) += marker.o
obj-$(CONFIG_SMP) += sched_cpupri.o

ifneq ($(CONFIG_SCHED_NO_NO_OMIT_FRAME_POINTER),y)
# According to Alan Modra <alan@linuxcare.com.au>, the -fno-omit-frame-pointer is
# needed for x86 only.  Why this used to be enabled for all architectures is beyond
# me.  I suspect most platforms don't need this, but until we know that for sure
# I turn this off for IA-64 only.  Andreas Schwab says it's also needed on m68k
# to get a correct value for the wait-channel (WCHAN in ps). --davidm
CFLAGS_sched.o := $(PROFILING) -fno-omit-frame-pointer -fno-instrument-functions
endif

# On ppc64, if instrumtation is turned on for kft.c, then kernel won't boot.
# It's OK to turn off instrumentation for other arches, since we really don't
# want any of the functions in kft.c traced anyway.
CFLAGS_kft.o := -fno-instrument-functions

$(obj)/configs.o: $(obj)/config_data.h

# config_data.h contains the same information as ikconfig.h but gzipped.
# Info from config_data can be extracted from /proc/config*
targets += config_data.gz
$(obj)/config_data.gz: .config FORCE
	$(call if_changed,gzip)

quiet_cmd_ikconfiggz = IKCFG   $@
      cmd_ikconfiggz = (echo "static const char kernel_config_data[] = MAGIC_START"; cat $< | scripts/bin2c; echo "MAGIC_END;") > $@
targets += config_data.h
$(obj)/config_data.h: $(obj)/config_data.gz FORCE
	$(call if_changed,ikconfiggz)

# Files generated that shall be removed upon make clean
clean-files := kftstatic.c

$(obj)/kftstatic.o: $(obj)/kftstatic.c

$(obj)/kftstatic.c: $(src)/kftstatic.conf
	chmod +x $(srctree)/scripts/mkkftrun.pl
	$(srctree)/scripts/mkkftrun.pl $< > $@
