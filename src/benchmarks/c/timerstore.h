/* ###################################################################
 * This is a C definition based on the the Model section of the article:
 * Hashed and hierarchical timing wheels: efficient data structures for 
 * implementing a timer facility by Varghese, George and Lauck, Anthony.
 * 
 * The first two routines are activated on client calls while
 * the last two are invoked on timer ticks. The timer is often an
 * external hardware clock.
 *
 * The implementation diverges from the Model specified in the article 
 * only by having the stop_timer routine not call expiry_proccessing for
 * the requested timer, but just remove it from the Timer Store. In this
 * case per_tick_bookkeeping calls expiry_proccessing directly in order 
 * to perform expiryAction for the requested timer as set by start_timer.
 * 
 * ################################################################### */
 
 
#ifndef TIMERSTORE_H
#define TIMERSTORE_H

#include "../../utils/millisecond_time.h"

#define TIMERSTORE_OK 0
#define TIMERSTORE_ERR 1

/**
 * The client calls this routine to start a timer that will 
 * expire after "Interval" units of time. 
 * The client supplies a RequestId which is used to distinguish this timer 
 * from other timers that the client has outstanding. Finally, the client 
 * can specify what action must be taken on expiry: for instance, 
 * calling a client-specified routine, or setting an event flag.
 * 
 */
int start_timer(mstime_t interval, char* requestId, void* expiryAction);


/**
 * This routine uses its knowledge of the client and RequestId to 
 * locate the timer and stop it.
 */
int stop_timer(char* requestId);

/**
 * Let the granularity of the timer be T units. Then every T 
 * units this routine checks whether any outstanding timers have 
 * expired; if this is the case, it calls the expiry_proccessing routine.
 */
int per_tick_bookkeeping();


/**
 *  This routine does the ExpiryAction specified in the start_timer call.
 */
int expiry_proccessing(char* requestId);

#endif
