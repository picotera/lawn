#include "cts.h"

/* Registration order fixes column/line order in outputs. */
const cts_vtable *const cts_algos[] = {
    &cts_lawn_vtable,   /* [0] = reference for the differential gate */
    &cts_lawn2_vtable,
    &cts_wahern_vtable,
    &cts_naive_vtable,
};
const int cts_nalgos = (int)(sizeof(cts_algos) / sizeof(cts_algos[0]));
