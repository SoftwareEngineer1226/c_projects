#pragma once
#include <stdbool.h>
#include <stdlib.h>

/**
 * Struct representing a queue
 */
typedef struct queue queue;

/**
 * Note:
 * queue_empty(), queue_size(), and queue_front() are not provided with the
 * thread-safe version of this queue.
 * Those values are owned by the queue and can not be passed off to any
 * individual thread.
 * Therefore it is impossible to expose them in a thread-safe manner.
 *
 * Consider the following timeline of two threads with a brand new queue:
 *
 *   time |            thread 1            |         thread 2
 * --------------------------------------------------------------------
 *    0  | size_t size = queue_size(queue);|
 *    1  |                                 |  queue_push(queue, blah);
 *    2  | if(size == 0) {do_something()}; |
 *
 * Notice that do_something will execute when the queue in fact has 1 element
 * (not 0).
 * This is a race condition and is caused even if queue_size() is implemented to
 * be
 * thread-safe.
 *
 * A similiar argument can be made for queue_empty() and queue_front().
 */

/**
 * Allocate and return a pointer to a new queue (on the heap).
 *
 * 'max_size' determines how many elements the user can add to the queue before
 * it blocks.
 * If non-positive, the queue will never block upon a push (the queue does not
 * have a 'max_size').
 * Else, the queue will block if the user tries to push when the queue is full.
 */
queue *queue_create(ssize_t max_size);

/**
 * Destroys all queue elements by deallocating all the storage capacity
 * allocated by the 'queue'.
 */
void queue_destroy(queue *this);

/**
 * Adds a new 'element' to the end of the queue in constant time.
 * Note: Can be called by multiple threads.
 * Note: Blocks if the queue is full (defined by it's max_size).
 */
void queue_push(queue *this, void *element);

/**
 * Removes the element at the front of the queue in constant time.
 * Note: Can be called by multiple threads.
 * Note: Blocks if the queue is empty.
 */
void *queue_pull(queue *this);
