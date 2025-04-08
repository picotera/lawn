#ifndef TIMERWHEEL_TIMERSTORE_H
#define TIMERWHEEL_TIMERSTORE_H

#include "timerstore.h"

/**
 * Initialize the TimerStore using the Linux kernel timer wheel implementation.
 * 
 * @return TIMERSTORE_OK on success, TIMERSTORE_ERR on failure
 */
int timerwheel_timerstore_init(void);

/**
 * Clean up resources used by the TimerStore.
 * 
 * @return TIMERSTORE_OK on success, TIMERSTORE_ERR on failure
 */
int timerwheel_timerstore_cleanup(void);

/**
 * wrapping the timerstore functions
 */
int timerwheel_timerstore_start_timer(mstime_t interval, char* requestId, void* expiryAction);

int timerwheel_timerstore_stop_timer(char* requestId);

int timerwheel_timerstore_per_tick_bookkeeping();

int timerwheel_timerstore_expiry_proccessing(char* requestId);


#endif /* TIMERWHEEL_TIMERSTORE_H */ 