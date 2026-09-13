#ifndef MIGA80_DEMO_STOP_TEST_H
#define MIGA80_DEMO_STOP_TEST_H

#include "demo/supervisor.h"

struct miga80_stop_injector;
struct miga80_stop_injector *miga80_stop_injector_start(
    struct miga80_supervisor_events *events);
int miga80_stop_injector_finish(struct miga80_stop_injector *injector,
                               int expect_escape);

#endif
