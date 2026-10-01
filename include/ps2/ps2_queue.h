#ifndef PS2_QUEUE_H
#define PS2_QUEUE_H

/* Call only once the owner has stopped using queues in this allocation. */
void ps2_os_release_queues(const void* start, unsigned int size);

#endif
