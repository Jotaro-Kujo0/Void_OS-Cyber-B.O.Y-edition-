#ifndef APP_BASE_H
#define APP_BASE_H

#include <stdint.h>
#include "../os/events.h"

// This is the definition the compiler cannot find.
// Ensure this structure exists in this file:
typedef struct {
    const char *name;
    const char *desc;
    void (*init)();
    void (*tick)();
    void (*draw)();
    void (*event)(Event e);
    void (*suspend)();
    void (*resume)();
} AppDef;

#endif // APP_BASE_H