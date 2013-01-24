
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>

#include "rcu.h"

/// List of elements; will be used as a queue.
template <typename T>
struct queue {
    queue<T> *next;
    T value;
};


namespace granary { namespace smp {

    /// Specify the RCU protocol for lists.
    RCU_GENERIC_PROTOCOL((typename T), queue, (T),
        RCU_REFERENCE(next),
        RCU_VALUE(value))

}}


/// Number of items in the queue.
static std::atomic<unsigned> QUEUE_LEN(ATOMIC_VAR_INIT(0U));


/// How many operations each writer thread should perform.
static int MAX_WRITE_PER_THREAD(5);


/// The shared queue that readers and writers will operate on.
static granary::smp::rcu_protected<queue<int>> QUEUE(
    granary::smp::RCU_INIT_NULL);


/// The number of total threads currently running.
static std::atomic<unsigned> NUM_ACTIVE_THREADS(ATOMIC_VAR_INIT(0U));


/// The number of writer threads currently running.
static std::atomic<unsigned> NUM_ACTIVE_WRITERS(ATOMIC_VAR_INIT(0U));


/// Find and return the minimum value in the queue.
int find_min(granary::smp::rcu_read_reference<queue<int>> item) throw() {
    int min_elem = -1;
    for(; item; ) {
        min_elem = std::min(min_elem, int(item.value));
        item = item.next;
    }
    return min_elem;
}


/// Add an element with a random value to the head of the queue.
struct enqueue_random : public granary::smp::rcu_writer<queue<int> > {

    queue<int> *new_item;

    /// Allocate a new list head.
    virtual void setup(void) throw() {
        new_item = new queue<int>;
        new_item->value = rand();
        new_item->next = nullptr;
    }

    /// Change the list head
    virtual void while_readers_exist(
        write_ref_type head,
        publisher_type &publisher
    ) throw() {
        write_ref_type new_head(publisher.promote(new_item));
        new_head.next = head;
        publisher.publish(new_head);
    }
};


/// Remove an element from the end of the queue.
struct dequeue : public granary::smp::rcu_writer<queue<int> > {

    write_ref_type removed_elem;

    /// Find and remove the last element from the queue.
    virtual void while_readers_exist(
        write_ref_type ref,
        publisher_type &publisher
    ) throw() {

        // nothing in the list
        if(!ref) {
            return;
        }

        write_ref_type prev_ref;
        for(; ; prev_ref = ref, ref = ref.next) {
            if(!ref.next) {
                break;
            }
        }

        removed_elem = ref;

        // only one element in the queue
        if(!prev_ref) {
            publisher.publish(publisher.promote(nullptr));

        // more than one element in the queue
        } else {
            prev_ref.next = publisher.promote(nullptr);
        }
    }

    /// Delete the removed element.
    virtual void teardown(collector_type &collector) throw() {
        if(removed_elem) {
            delete collector.demote(removed_elem);
        }
    }
};


/// Remove all elements from the queue.
struct empty : public granary::smp::rcu_writer<queue<int> > {

    queue<int> *head;

    /// Make all elements unreachable.
    virtual void while_readers_exist(
        write_ref_type ref,
        publisher_type &publisher
    ) throw() {
        head = publisher.publish(publisher.promote(nullptr));
    }

    /// Delete the elements.
    virtual void teardown(collector_type &collector) throw() {
        for(queue<int> *next(nullptr); head; head = next) {
            next = head->next;
            delete head;
        }
    }
};


/// Reader thread implementation.
void *reader_thread(void *p) {
    sleep(1);
    for(;;) {
        QUEUE.read(find_min);
        if(!NUM_ACTIVE_WRITERS.load()) {
            break;
        }
    }

    NUM_ACTIVE_THREADS.fetch_sub(1);

    return nullptr;
}


/// Writer thread implementation.
void *writer_thread(void *p) {
    sleep(1);
    for(int i(0); i < MAX_WRITE_PER_THREAD; ++i) {
        if(rand() % 2) {
            enqueue_random adder;
            QUEUE.write(adder);
        } else {
            dequeue remover;
            QUEUE.write(remover);
        }
    }

    NUM_ACTIVE_THREADS.fetch_sub(1);
    NUM_ACTIVE_WRITERS.fetch_sub(1);

    return nullptr;
}


/// Main thread implementation.
int main(int argc, char **argv) throw() {
    unsigned num_readers(0);
    unsigned num_writers(0);

    if(3 != argc) {
        printf("Format: %s <num_readers> <num_writers>\n", argv[0]);
        return 0;
    }

    sscanf(argv[1], "%u", &num_readers);
    sscanf(argv[2], "%u", &num_writers);

    NUM_ACTIVE_THREADS.store(num_writers + num_readers);
    NUM_ACTIVE_WRITERS.store(num_writers);

    pthread_t *readers = new pthread_t[num_readers];
    pthread_t *writers = new pthread_t[num_writers];

    pthread_attr_t writer_attr;
    pthread_attr_init(&writer_attr);
    pthread_attr_setschedpolicy(&writer_attr, SCHED_OTHER);

    pthread_attr_t reader_attr;
    pthread_attr_init(&reader_attr);
    pthread_attr_setschedpolicy(&reader_attr, SCHED_RR);

    // make writer threads
    for(unsigned i = 0; i < num_writers; ++i) {
        pthread_create(&(writers[i]), &writer_attr, writer_thread, &(writers[i]));
    }

    // make reader threads
    for(unsigned i = 0; i < num_readers; ++i) {
        pthread_create(&(readers[i]), &reader_attr, reader_thread, &(readers[i]));
    }

    for(;;) {
        sleep(1);
        if(!NUM_ACTIVE_THREADS.load()) {
            break;
        }
    }

    delete [] readers;
    delete [] writers;

    // free up all memory in the queue.
    empty element_remover;
    QUEUE.write(element_remover);

    return 0;
}
