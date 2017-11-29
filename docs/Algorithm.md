# Dehydration and Timer Storing Algorithms
> Dehydrators are simplistic time machines. They transport data elements that arrived prematurely in terms of their context right to the future where they might be needed, without loading the system while waiting. This concept is achieved by attaching a time-indexed data store to a clock, storing elements as they arrive to the dehydrator and re-introducing them as inputs to the system once a predetermined time period has passed.                                                      *From:* [Fast Data](https://goo.gl/DDFFPO)

In general, the goal of a timer-store mechanism is to store data for a predetermined time, and releasing it upon request without loading the using system.
That is, beyond being speedy and resilient, we want all operations on this time centric data-store to be have a minimal signature, using just IDs when possible (minimizing cross-process communication).

Storing Data in a DB based on a key of id and indexed by time is a common technique in construction of all time dependent systems, but using Redis as a Key-Value store enabled more flexibility when constructing a data-store for the specific goal of being a timer-store.

## Naive Algorithm 1

These goals can be achieved relatively easily using naive implementation of a unified list where items are being stored as an `(id, element, insert time + TTL)`. tuple and are added to the list (Push) in O(1). The obvious down side is that pulling a specific element from the list (Pull) is done at O(n) and polling for expired items is also done at O(n), where n is the number of all stored elements in the list.
so:
* Push - O(1)
* Pull - O(n)
* Poll - O(n)


## Naive Algorithm 2

An improvement can be done to this algorithm using a queue instead of a the list where items are being stored as the same tuple, but are sorted based on expiration time, not insertion time. This would skew runtimes to be:
* Push - O(log n), since now we need to scan the queue in order to find where to insert the new element.
* Pull - still done at O(n)
* Poll - is now improved to O(m) where m is the number of **expired** items.


## Adding an element map
Using a hash map to help us locate elements faster would improve runtimes to be:
* Push - O(log n)
* Pull - O(1) now that we just need to check the map to find a specific element.
* Poll - O(m)


## Timer Wheel
Timer Wheel assumes we will have timers uniformly spread into the future and limited in span. It works by holding a slot for every time period, or Tick (be is a minute, a second or millisecond, depending on the wheel's resolution) up to the maximal timer allowed. Every Tick the corrisponding slot is cleared of all events held in it. 
Runtimes:
* Push - O(1)
* Pull - O(n)/O(1) depending the use of element map.
* Poll - O(n) where n is minimized to just the number of **expired** elements. 

you can read the full article [here](ton97-timing-wheels.pdf) and some slided [here](TimingWheels.ppt)

## Queue-Map Algorithm (Lawn)

**this is the algorithm that is used in this project**
This Algorithm takes the best from both naive algorithms 1 and 2, and is less limiting then Timer Wheel - assuming a system will have a set of different TTL which is significantly smaller (<<) in size then the amount of items that are stored, but with no limitation on uniformity or span. Having a pseudo fixed number of different TTLs  we could now store items in a queue based on their TTL. Each queue would be self-sorted by expiration due to the fact that each two consecutive events with the same TTL would have expiration times that correspond with the order in which they have arrived. Polling is done by iterating over the queues and from each queue pop all the elements which expired, once you see an element that has not expired yet, move to next queue.
Using these rules, and by holding a map of sorted TTL queues we can now:

* Push in O(1) since pulling the TTL Queue from the map takes O(1) and inserting at the head of this queue is also O(1).
* Pull in O(1).
* Poll in O(n) - where n is minimized to just the number of **expired** elements (that's an avarge of O(1)!), notice we regard the number of different TTLs to be a constant and << total number of timers elements in the system.
