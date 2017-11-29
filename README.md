[![GitHub version](https://img.shields.io/github/release/tamarlabs/rede.svg?style=flat-square)](https://github.com/TamarLabs/ReDe/releases/latest)
[![issues count](https://img.shields.io/github/issues/tamarlabs/rede.svg?style=flat-square)](https://github.com/TamarLabs/ReDe/issues)
[![Build Status](https://img.shields.io/travis/TamarLabs/ReDe/master.svg?style=flat-square)](https://travis-ci.org/TamarLabs/ReDe)
<h1>  ReDe - The <img src="https://upload.wikimedia.org/wikipedia/en/6/6b/Redis_Logo.svg" alt="redis" height="47" align="top"/> Element Dehydration Module</h1>

:rocket:**TL;DR - A Lawn is a timer data store, not unlike Timer-Wheel, but with unlimited timer span with no degrigation in performance with large sets of timers**



**Lawn** is a high troughput data structure that is based on the assumption that most timers are set to a small set of TTLs to boost overall DS performance. It can assist when handling a large set of timers with relativly small variance in TTL by effectivly using minimal queues to store timer data. Achieving O(1) for insertion and deletion of timers, and O(t->1) where t is the number of different TTLs (which is assumed to be roughly a fixed size set).

Lawns can be used for anything from keeping track of multipile real-time TTLs of elements, to implement a straightforward dehydration system as depicted in the article "[Fast Data](https://goo.gl/DDFFPO)". 

*You can read further on the algorithm behind Lawn [here](docs/Algorithm.md).*

## Common Use Cases

* **Stream Coordination** -  Make data from one stream wait for the corresponding data from another (preferebly using sliding-window style timing).
* **Event Rate Limitation** - Delay any event beyond current max throughput to the next available time slot, while preserving order.
* **Self Cleaning Claims-Check** - Store data for a well known period, without the need to search for it when it is expired or clear it from the data-store yourself.
* **Task Timer** - Postpone actions and their respective payloads to a specific point in time.

## About This Project

This Project is based off a python version of the same concepts designed by Adam Lev-Libfeld and developed in Tamar Labs by Adam Lev-Libfeld and Alexander Margolin in mid 2015.

A Redis module using these concepts was created by Adam Lev-Libfeld during the RedisModulesHackathon in late 2016.

This code is meant for public use and is maintaind solely by Adam Lev-Libfled.
