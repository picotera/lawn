/* This is a stand-alone implementation of a real-time expiration data store.
 * It is basically a min heap <key, exp_version> sorted by expiration,
 * with a map of [key] -> <exp_version, exp> on the side
 */
#include "../lawn2.h"

#include "../utils/millisecond_time.h"

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SUCCESS 0
#define FAIL 1

/*################### Test State Manegemet #########################*/

typedef struct test_state_store {
    lawn2       *l;
    store       *st;
} state;

static state *init(void) {
    state *s = calloc(1, sizeof *s);
    s->l = lawn2_new();
    s->st = init_store();
    return s;
}

static void destroy(state *s) {
    lawn2_free(s->l);
    destroy_store(s->st);
    free(s);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static int fail_with_error(state *s, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    // cleanup
    destroy(s);
    return FAIL;
}


static int tick_and_check(state *s, uint64_t expected_expired_count, const uint64_t expected_expired_ids[]) {
  lawn2_timer *expired_head = NULL;
  uint64_t count = lawn2_tick(s->l, &expired_head);

  uint64_t actual = 0;
  // check all expired ids are expected
  for (lawn2_timer *curr = expired_head; curr != NULL; curr = curr->next) {
    int found = 0;
    for (int i=0; (i < expected_expired_count && !found); ++i) {
      if (curr->id == expected_expired_ids[i])
        found = 1;
    }
    if (!found)
      return fail_with_error(s,"ERROR: unexpected id %llu on tick %llu\n", curr->id, lawn2_now(s->l));
    ++actual;
  }
  if (actual != count)
    return fail_with_error(s,"ERROR: counted expired timers and actual diverged reported: %llu actual: %llu\n", count, actual);

  if (actual != expected_expired_count)
    return fail_with_error(s,"ERROR: expected %llu but found to have %llu items\n", expected_expired_count, actual);
  
  // check all expected ids are accounted for
  for (int i=0; i < expected_expired_count; ++i) {
    int found = 0;
    for (lawn2_timer *curr = expired_head; (curr != NULL  && !found); curr = curr->next) {
      if (curr->id == expected_expired_ids[i])
        found = 1;
    }
    if (!found)
      return fail_with_error(s,"ERROR: missing id %llu not found on tick %llu\n", expected_expired_ids[i], lawn2_now(s->l));
  }

  return SUCCESS;
}

/*############################################################################*/


int constructor_distructore_test() {
  int retval = FAIL;
  lawn2* lwn = lawn2_new();
  uint64_t base_size = lawn2_size(lwn);
  if (base_size != 0)
    printf("ERROR: unexpected lawn size: expected 0 got %llu", base_size);
  else {
    uint64_t now = lawn2_now(lwn);
    if (now != 0)
      printf("ERROR: unexpected internal clock: expected 0 got %llu", now);
    else 
      retval = SUCCESS;
  }
  lawn2_free(lwn);
  return retval;
}


int test_set_element_ttl() {
  state* s = init();
  mstime_t ttl_ticks = 10000;
  mstime_t expected = lawn2_now(s->l) + ttl_ticks;
  uint64_t key = 42;
  lawn2_timer *n = timer_for(s->st, key);
  if (n->in_store) 
    return fail_with_error(s,"ERROR: node in store before being inserted");

  lawn2_add(s->l, timer_for(s->st, key), ttl_ticks);
  n = timer_for(s->st, key);
  if (!n->in_store) 
    return fail_with_error(s,"ERROR: node NOT in store after being inserted");

  destroy(s);
  return SUCCESS;
}


int test_set_get_element_exp() {
  state* s = init();
  int retval = FAIL;
  mstime_t ttl_ticks = 10000;
  uint64_t key = 67;
  uint64_t base_size = lawn2_size(s->l);
  
  lawn2_timer *n = timer_for(s->st, key);
  mstime_t expected = lawn2_now(s->l) + ttl_ticks;
  lawn2_add(s->l, n, ttl_ticks);

  
  uint64_t expected_size = base_size + 1;
  uint64_t actual_size = lawn2_size(s->l);
  
  if (actual_size != expected_size)
    return fail_with_error(s,"ERROR: unexpected lawn size: expected %llu got %llu", expected_size, actual_size);
  
  n = timer_for(s->st, key);
  if (n->expiration != expected) 
    return fail_with_error(s,"ERROR: expected %llu but found %llu\n", expected, n->expiration);
  
  destroy(s);
  return SUCCESS;
}


int test_del_element_exp() {
  state* s = init();
  int retval = FAIL;
  mstime_t ttl_ticks = 10000;
  uint64_t key = 69;
  uint64_t base_size = lawn2_size(s->l);

  lawn2_add(s->l, timer_for(s->st, key), ttl_ticks);
  lawn2_timer *n = timer_for(s->st, key);
  if (!n->in_store) 
    retval = FAIL;
  else {
    lawn2_del(s->l, n);
    n = timer_for(s->st, key);
    if (n->in_store)
      return fail_with_error(s,"ERROR: node still in store after being deleted");
    else {
      uint64_t actual_size = lawn2_size(s->l);
      if (actual_size != 0)
        return fail_with_error(s,"ERROR: unexpected lawn size: expected 0 got %llu", actual_size);
    }
  }

  destroy(s);
  return SUCCESS;
}


int test_next_at() {
  state* s = init();
  int retval = FAIL;

  mstime_t ttl_ticks1 = 100;
  uint64_t key1 = 1;

  mstime_t ttl_ticks2 = 2;
  uint64_t key2 = 2;

  mstime_t ttl_ticks3 = 3;
  uint64_t key3 = 3;

  mstime_t ttl_ticks4 = 40;
  uint64_t key4 = 4;

  mstime_t expected = lawn2_now(s->l) + ttl_ticks2;
  lawn2_add(s->l, timer_for(s->st, key1), ttl_ticks1);
  lawn2_add(s->l, timer_for(s->st, key2), ttl_ticks2);
  lawn2_add(s->l, timer_for(s->st, key3), ttl_ticks3);
  lawn2_add(s->l, timer_for(s->st, key4), ttl_ticks4);

  
  mstime_t next_at = lawn2_next_expiration(s->l);
  if (next_at != expected)
    return fail_with_error(s,"ERROR: expected %llu but found %llu (diff: %llu)\n", expected, next_at, expected - next_at);

  destroy(s);
  return SUCCESS;
}


int test_pop_expired() {
  state* s = init();
  int retval = FAIL;

  mstime_t ttl_ticks1 = 100000;
  uint64_t key1 = 1;

  mstime_t ttl_ticks2 = 2;
  uint64_t key2 = 2;

  mstime_t ttl_ticks3 = 30;
  uint64_t key3 = 3;

  mstime_t ttl_ticks4 = 400;
  uint64_t key4 = 4;

  lawn2_add(s->l, timer_for(s->st, key1), ttl_ticks1);
  lawn2_add(s->l, timer_for(s->st, key2), ttl_ticks2);
  lawn2_add(s->l, timer_for(s->st, key3), ttl_ticks3);
  lawn2_del(s->l, timer_for(s->st, key2));
  lawn2_add(s->l, timer_for(s->st, key4), ttl_ticks4);


  if (tick_and_check(s, 0, NULL) == FAIL)
    return FAIL;
  
  lawn2_set_now(s->l, ttl_ticks2);
  if (tick_and_check(s, 0, NULL) == FAIL) // still 0 because key2 was deleted
    return FAIL;

  lawn2_set_now(s->l, ttl_ticks3);
  if (tick_and_check(s, 1, (uint64_t[]){3}) == FAIL)
    return FAIL;

  lawn2_set_now(s->l, ttl_ticks1);
  if (tick_and_check(s, 2, (uint64_t[]){4,1}) == FAIL)
    return FAIL;

  if (tick_and_check(s, 0, NULL) == FAIL)
    return FAIL;

  destroy(s);
  return SUCCESS;
}


int main(int argc, char* argv[]) {
  mstime_t start_time = current_time_ms();
  int num_of_failed_tests = 0;
  int num_of_passed_tests = 0;
  printf("-------------------\n  STARTING TESTS\n-------------------\n\n");

  printf("-> constructor-distructore test\n");
  if (constructor_distructore_test() == FAIL) {
    ++num_of_failed_tests;
    printf(" FAILED constructor-distructore\n");
  } else {
    printf(" PASSED\n");
    ++num_of_passed_tests;
  }

  printf("-> set test\n");
  if (test_set_element_ttl() == FAIL) {
    ++num_of_failed_tests;
    printf(" FAILED on set\n");
  } else {
    printf(" PASSED\n");
    ++num_of_passed_tests;
  }

  printf("-> set-get test\n");
  if (test_set_get_element_exp() == FAIL) {
    ++num_of_failed_tests;
    printf(" FAILED on set-get\n");
  } else {
    printf(" PASSED\n");
    ++num_of_passed_tests;
  }

  printf("-> del test\n");
  if (test_del_element_exp() == FAIL) {
    ++num_of_failed_tests;
    printf(" FAILED on del\n");
  } else {
    printf(" PASSED\n");
    ++num_of_passed_tests;
  }

  printf("-> next_at\n");
  if (test_next_at() == FAIL) {
    ++num_of_failed_tests;
    printf(" FAILED on next_at\n");
  } else {
    printf(" PASSED\n");
    ++num_of_passed_tests;
  }

  printf("-> pop_expired\n");
  if (test_pop_expired() == FAIL) {
    ++num_of_failed_tests;
    printf("FAILED on pop_expired\n");
  } else {
    printf("PASSED\n");
    ++num_of_passed_tests;
  }

  double total_time_ms = current_time_ms() - start_time;
  printf("\n-------------\n");
  if (num_of_failed_tests) {
    printf("Failed (%d tests failed and %d passed in %.2f sec)\n", num_of_failed_tests,
           num_of_passed_tests, total_time_ms / 1000);
    return FAIL;
  } else {
    printf("OK (%d tests passed in %.2f sec)\n\n", num_of_passed_tests, total_time_ms / 1000);
    return SUCCESS;
  }
}
