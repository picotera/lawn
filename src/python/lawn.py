def current_time_ms():
    millis = int(round(time.time() * 1000))
    return millis

class Lawn(object):
    """
    Base implementation of a timing wheel. In fact, noone forces you to even
    use time here.
    """

    def __init__(self, slots, initial_slot=0):
        """
        :type slots: int
        :param slots: how many slots the wheel will have. Keep in mind that the
                      furthest slot you can add to is less by one.
        :type initial_slot: int
        :param initial_slot: the index of a starting slot. Notice that it will
                             be fitted into the size of the wheel by ignoring
                             all full circles.
        """
        self.slots = [{} for _ in xrange(slots)]
        self.position = initial_slot % slots
        self.size = 0

    def _next_step(self, slots=1):
        """
        Will return the index of the Nth following slot.

        :type slots: int
        :param slots: the offset. Default: 1 (so, the index of the very next
                      slot will be returned)
        """
        return (self.position + slots) % len(self.slots)

    def add(self, key, callback, *args, **kwargs):
        """
        Add an observed item to the last slot (counting from the current one).

        :param key: any hashable object, that will be used as the key.
                    Needs to be unique.
        :param callback: a callable object that will be invoked upon
                         expiration; args and kwargs will be passed to it.
        """
        self.insert(key, len(self.slots) - 1, callback, *args, **kwargs)

    def insert(self, key, slot_offset, callback, *args, **kwargs):
        """
        Add an observed item to Nth slot (counting from the current one).

        :param key: any hashable object, that will be used as the key.
                    Needs to be unique.
        :type slot_offset: int
        :param slot_offset: specifies which slot, starting with the current
                            one, will be used.
        :param callback: a callable object that will be invoked upon
                         expiration; args and kwargs will be passed to it.

        :raises ValueError: when the provided offset is larger than the size
                            of the wheel.
        """
        if slot_offset <= 0:
            raise ValueError(
                'Can\'t insert entries in the past.'
            )

        if slot_offset >= len(self.slots):
            raise ValueError(
                'Cannot add to the {}(st/nd/rd/th) following slot because '
                'there are only {} slots available.'
                .format(slot_offset, len(self.slots))
            )
        slot = self._next_step(slot_offset)
        self.slots[slot][key] = (callback, args, kwargs)
        self.size += 1

    def DeleteTimer(self, timer_id):
        timer = self.timers.get(timer_id)
        if timer:
            ttl = timer.ttl
            q = self.queues.get(ttl)
            while (q and q[0].expiration <= now):
                    self.TimerExpired(q[0].id)
                    del(q[] # TODO: get index of timer in queue and pop it out

            if (len(q) == 0):
                del(self.queues[ttl])
                self.ttls.remove(ttl)


            del(self.timers[timer_id])            
            self.size -= 1
            return

        raise KeyError('Key {} was not found in the wheel.'.format(key))

    def PerTickBookkeeping(self):
        now = current_time_ms()
        for ttl in self.ttls:
            q = self.queues.get(ttl)
            while (q and q[0].expiration <= now):
                    self.TimerExpired(q[0].id)
                    q.pop(0)

            if (len(q) == 0):
                del(self.queues[ttl])
                self.ttls.remove(ttl)


    def TimerExpired(self, timer_id):
        timer = self.timers.get(timer_id)
        callback, args, kwargs = timer.payload
        
        if callback:
            callback(*args, **kwargs)

        del(self.timers[timer_id])            
        self.size -= 1


    def clear(self, new_position):
        for ttl in self.ttls:
            del(self.queues[ttl])
        self.timers = {}
        self.size = 0