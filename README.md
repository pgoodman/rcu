Read-Copy-Update Implementation
===============================

This is a simple and restricted implementation of the RCU protocol.
The goals of this implementation are:

  * Being very explicit in how RCU-protected objects are meant to be
    used.
  * Remove the possibility of errors where `rcu_assign_pointer` or
    `rcu_dereference` are missing.
  * Implement RCU in a way that does not depend on any particular
    implementation of threads or schedulers. Thus, this implementation
    should work both in user and kernel space.
  * Implement sleepable RCU.

How it Works
------------
Each data structure has its own locking primitive (default is a spin lock)
and a pointer to an atomic reference counter. Read critical sections are
delimited by increments and decrements to this reference counter.
Synchronizations operate by switching the reference counter pointed to by
the data structure (and so later readers operate on a different counter),
and then waiting for the previously active counter to hit zero.

Nitty-Gritty Details
--------------------

### Reference Counting
The reference counters operate by incrementing and decrementing by `2`.
This is so that the value of a reference counter encodes both the count
and whether or not it is valid/stale.

Because a pointer to a reference counter is used, it means that there is
potential for a reader to read the address of the counter, but be descheduled
or interrupted before having a chance to increment the counter. By the time
that it might want to increment the counter, a writer could have already
finished synchronizing using that counter.

To mitigate this, the operation of waiting on a reference counter is to
loop and compare-and-swap from `0` to `1`. Thus, a reference counter is
valid iff its value is even, and it is stale iff its value is odd. The
increment/decrement by `2` maintains this property.

Thus, reader threads can unconditionally increment their counters, but can
always detect if an increment was stale and recover by trying to get the
newest reference counter.

### Stale Reference Counters
Hazard pointers are used to reclaim stale reference counters. The hazard
pointer implementation is simple: the main RCU-protected data structure
maintains two lists: one of "hazardous" reference counters (currently
being operated on by some reader thread), and another of stale reference
counters. Writer threads periodically compare these lists to find reference
counters that can be reclaimed.