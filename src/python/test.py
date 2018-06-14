#! /usr/bin/python
import redis
import time
import random
import sys
import timingwheel

wheel_total_div = 0
wheel_max_div = 0

lawn_total_div = 0
lawn_max_div = 0


# MS_COMPARISON_ACCURACY = 2

# def compare_ms(a, b):
    # return (abs(a-b) <= MS_COMPARISON_ACCURACY)


def current_time_ms():
    millis = int(round(time.time() * 1000))
    return millis


def callback(caller, expiration, now=current_time_ms()):
    div = abs(expiration - now)
    if (div):
        if (caller == "wheel"):
            wheel_total_div += div
            if (div > wheel_max_div)
                wheel_max_div = div
        if (caller == "lawn"):
            lawn_total_div += div
            if (div > wheel_max_div)
                wheel_max_div = div


def initstore(store_type):
    if (store_type == "lawn"):
        return new_lawn()
    if (store_type == "wheel"):
        ms_sec = 0.001
        ten_min_ms = 10 * 60 * 1000
        return TimingWheel(ms_sec, ten_min_ms)
    print "ERROR: no such store type ", store_type
    exit()


def load_test(timers=1000000, timeouts_ms=[1, 2, 4, 16, 32, 100, 200, 1000, 2000, 4000, 10000]):
    print("starting load tests:")
    print("store: timers: {}\nTTL count: {}\nTTLs: {}\n".format(store_type, timers, len(timeouts) ,timeouts))
    
    ms_sec = 0.001
    ten_min_ms = 10 * 60 * 1000
    wheel = TimingWheel(ms_sec, ten_min_ms)

    lawn = Lawn()


    wheel_total_insert_time_ms = 0
    wheel_max_insert_time_ms = 0

    lawn_total_insert_time_ms = 0
    lawn_max_insert_time_ms = 0
    
    start = time.time()

    # start timers
    for i in range(timers):
        if (i%100000 == 0):
            print("set {} timers".format(i))
        ttl_ms = random.choice(timeouts)
        key = "timer_{}_for_{}_ms".format(i, ttl_ms)
        wheel_start_insert_time_ms = current_time_ms()
        wheel.insert(key=key, slot_offset=ttl_ms, callback=callback, caller="wheel", expiration=wheel_start_insert_time_ms+ttl_ms)
        
        # collect data 
        wheel_end_insert_time_ms = current_time_ms()
        lawn_start_insert_time_ms = wheel_end_insert_time_ms
        #start a timer in lawn (key=key, ttl=ttl_ms, callback=callback)
        lawn_end_insert_time_ms = current_time_ms()
        
        # log insert times
        wheel_insert_time_ms = wheel_end_insert_time_ms - wheel_start_insert_time_ms
        wheel_total_insert_time_ms += wheel_insert_time_ms
        if (wheel_insert_time_ms > wheel_max_insert_time_ms)
            wheel_max_insert_time = wheel_insert_time

        lawn_insert_time_ms_ms = lawn_end_insert_time_ms - lawn_start_insert_time_ms
        lawn_total_insert_time_ms += lawn_insert_time_ms
        if (lawn_insert_time_ms > lawn_max_insert_time_ms)
            lawn_max_insert_time_ms = lawn_insert_time_ms

    # print insert report
    print("All timers inserted!")

    timer_count = wheel.size + lawn.size
    i = 0
    while (timer_count):
        i += 1
        if (i%5 == 0): # 5 seconds
            timer_count = wheel.size + lawn.size
            print("Waiting for remaining {} to expire".format(timer_count))
        time.sleep(1) #sleep 1 s
        

    end = time.time()
    print("All timers expired!")

    print("Wheel  - insert: {} timers in {} ms (max {}), expiration avg div {} ms (max {})".format(
        timers, 
        wheel_total_insert_time, 
        wheel_max_insert_time,
        wheel_total_div/timers,
        wheel_max_div
        ))
    
    print("Lawn  - insert: {} timers in {} ms (max {}), expiration avg div {} ms (max {})".format(
        timers, 
        lawn_total_insert_time, 
        lawn_max_insert_time,
        lawn_total_div/timers,
        lawn_max_div
        ))

    return True



if __name__ == "__main__":
    args = sys.argv[1:]
    # base_test = False
    ttl_test = False
    timer_test = False
    # port = 6379
    if not args:
        # base_test = True
        timer_test = True
        ttl_test = True

    else:
        for i, arg in enumerate(args):
            if arg == "--port":
                port = args[i+1]

    # print("starting load tests with redis module connected to port:{}".format(port))
    # r = redis.StrictRedis(host='localhost', port=port, db=0)
    # if base_test:
    #     print("Running base test: some short TTLs, some long")
    #     load_test(r)

    if timer_test:
        for timers in range(10, 10000 ,3000000):
            load_test(timers=timers)


    if ttl_test:
        for ttl_power in range(14):
            load_test(timeouts_ms=[2**p for p in range(ttl_power)])