#! /usr/bin/python
import redis
import time
import random
import sys

MS_COMPARISON_ACCURACY = 2

def compare_ms(a, b):
    return (abs(a-b) <= MS_COMPARISON_ACCURACY)


def current_time_ms():
    millis = int(round(time.time() * 1000))
    return millis


def load_test(redis_service, timers=1000000, timeouts=[1, 10000]):#, 2, 4, 16, 32, 100, 200, 1000, 2000, 4000, 10000]):
    print("starting load tests:")
    print("timers: {}\nTTL count: {}\nTTLs: {}\n".format(timers, len(timeouts) ,timeouts))
    start = time.time()
    for i in range(timers):
        if (i%100000 == 0):
            print("set {} timers".format(i))
        ttl_ms = random.choice(timeouts)
        key = "timer_{}_for_{}_ms".format(i, ttl_ms)
        redis_service.execute_command("SET", key, 1)
        redis_service.execute_command("REXPIRE", key, ttl_ms)

    timer_count = redis_service.execute_command("RCOUNT")
    while (timer_count):
        print("Done setting timers, waiting for remaining {} to expire".format(redis_service.execute_command("RCOUNT")))
        time.sleep(5)
        timer_count = redis_service.execute_command("RCOUNT")

    end = time.time()
    print("All timers expired, printing results")
    redis_service.execute_command("RPROFILE")
    print "mesured {} timers in {} sec".format(timers, end-start)
    return True

    # print "mean push velocity =", cycles/(push_end-start), "per second"
    # print "mean push(generating ids) velocity =", cycles/(gid_push_end-push_end), "per second"
    # print "mean pull velocity =", cycles/(pull_end-gid_push_end), "per second"
    # print "mean poll velocity = ", cycles/poll_sum, "per second"

if __name__ == "__main__":
    args = sys.argv[1:]
    base_test = False
    ttl_test = False
    timer_test = False
    port = 6379
    if not args:
        base_test = True
        timer_test = True
        ttl_test = True

    else:
        for i, arg in enumerate(args):
            if arg == "--port":
                port = args[i+1]

    print("starting load tests with redis module connected to port:{}".format(port))
    r = redis.StrictRedis(host='localhost', port=port, db=0)
    if base_test:
        print("Running base test: some short TTLs, some long")
        load_test(r)

    if timer_test:
        for timers in range(1, 10000 ,3000000):
            load_test(timers=timers)


    if ttl_test:
        for ttl_power in range(14):
            load_test(timeouts=[2**p for p in range(ttl_power)])