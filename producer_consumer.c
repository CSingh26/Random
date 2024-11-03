#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Zombie Killer Kernel Module");

// Module parameters
static int prod = 1; // Number of producer threads
static int cons = 5; // Number of consumer threads
static int size = 10; // Size of the buffer
static int uid = 1000; // User ID for filtering zombie processes

module_param(prod, int, 0);
module_param(cons, int, 0);
module_param(size, int, 0);
module_param(uid, int, 0);

#define BUFFER_SIZE 10

// Shared buffer
static struct task_struct *buffer[BUFFER_SIZE];
static int in = 0;
static int out = 0;
static int count = 0;

// Synchronization
static DEFINE_MUTEX(buffer_mutex);
static DECLARE_WAIT_QUEUE_HEAD(buffer_not_empty);
static DECLARE_WAIT_QUEUE_HEAD(buffer_not_full);

// Thread structures
static struct task_struct *producer;
static struct task_struct *consumers[10];
static int *consumer_ids[10]; // Array to store dynamically allocated IDs

// Producer thread function
static int producer_thread(void *data) {
    struct task_struct *task;

    while (!kthread_should_stop()) {
        mutex_lock(&buffer_mutex);

        for_each_process(task) {
            if (task->exit_state == EXIT_ZOMBIE && task->cred->uid.val == uid) {
                while (count == BUFFER_SIZE) {
                    mutex_unlock(&buffer_mutex);
                    if (wait_event_interruptible(buffer_not_full, count < BUFFER_SIZE || kthread_should_stop())) {
                        return 0; // Exit if interrupted
                    }
                    mutex_lock(&buffer_mutex);
                }

                buffer[in] = task;
                in = (in + 1) % BUFFER_SIZE;
                count++;

                printk(KERN_INFO "[Producer] Produced zombie process with pid %d\n", task->pid);
                wake_up_interruptible(&buffer_not_empty);
            }
        }

        mutex_unlock(&buffer_mutex);
        msleep(250); // Sleep to avoid busy-waiting
    }

    return 0;
}

// Consumer thread function
static int consumer_thread(void *data) {
    struct task_struct *zombie;
    int consumer_id = *(int *)data;

    while (!kthread_should_stop()) {
        mutex_lock(&buffer_mutex);

        while (count == 0) {
            mutex_unlock(&buffer_mutex);
            if (wait_event_interruptible(buffer_not_empty, count > 0 || kthread_should_stop())) {
                return 0; // Exit if interrupted
            }
            mutex_lock(&buffer_mutex);
        }

        zombie = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        count--;

        printk(KERN_INFO "[Consumer-%d] Consumed zombie process with pid %d\n", consumer_id, zombie->pid);

        // Kill the parent of the zombie process
        kill_pid(task_pid(zombie->parent), SIGKILL, 0);

        wake_up_interruptible(&buffer_not_full);
        mutex_unlock(&buffer_mutex);
    }

    return 0;
}

// Module init and cleanup functions
static int __init zombie_killer_init(void) {
    int i;

    producer = kthread_run(producer_thread, NULL, "producer_thread");

    for (i = 0; i < cons; i++) {
        consumer_ids[i] = kmalloc(sizeof(int), GFP_KERNEL);
        *consumer_ids[i] = i + 1;
        consumers[i] = kthread_run(consumer_thread, consumer_ids[i], "consumer_thread-%d", i + 1);
    }

    printk(KERN_INFO "Zombie Killer Module Loaded\n");
    return 0;
}

static void __exit zombie_killer_exit(void) {
    int i;

    printk(KERN_INFO "Zombie Killer Module: Starting exit cleanup...\n");

    // Stop the producer thread
    if (producer) {
        printk(KERN_INFO "Stopping producer thread...\n");
        kthread_stop(producer);
        printk(KERN_INFO "Producer thread stopped.\n");
    }

    // Stop each consumer thread and free associated memory
    for (i = 0; i < cons; i++) {
        if (consumers[i]) {
            printk(KERN_INFO "Stopping consumer thread %d...\n", i);
            kthread_stop(consumers[i]);
            printk(KERN_INFO "Consumer thread %d stopped.\n", i);
        }
        if (consumer_ids[i]) {
            kfree(consumer_ids[i]);
            printk(KERN_INFO "Freed memory for consumer thread ID %d.\n", i);
        }
    }

    printk(KERN_INFO "Zombie Killer Module Unloaded\n");
}

module_init(zombie_killer_init);
module_exit(zombie_killer_exit);
