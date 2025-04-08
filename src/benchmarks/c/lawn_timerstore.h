#ifndef LAWN_TIMERSTORE_H
#define LAWN_TIMERSTORE_H

#include "timerstore.h"
#include "../../lawn.h"

/**
 * Initialize the Lawn-based TimerStore adapter
 * 
 * @return TIMERSTORE_OK on success, TIMERSTORE_ERR on error
 */
int lawn_timerstore_init(void);

/**
 * Clean up resources used by the Lawn-based TimerStore adapter
 * 
 * @return TIMERSTORE_OK on success, TIMERSTORE_ERR on error
 */
int lawn_timerstore_cleanup(void);


/**
 * wrapping the timerstore functions
 */
int lawn_timerstore_start_timer(mstime_t interval, char* requestId, void* expiryAction);

int lawn_timerstore_stop_timer(char* requestId);

int lawn_timerstore_per_tick_bookkeeping();

int lawn_timerstore_expiry_proccessing(char* requestId);

#endif /* LAWN_TIMERSTORE_H */ 