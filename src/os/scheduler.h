// scheduler.h
#pragma once
#include <stdint.h>

#define MAX_TASKS 12

typedef void (*TaskFn)();

typedef struct {
    TaskFn   fn;
    uint32_t interval_ms;   // how often to call (0 = every frame)
    uint32_t last_ms;
    uint8_t  priority;      // lower number = runs first
    bool     enabled;
    char     name[12];
} Task;

void    sched_init();
int8_t  sched_add(const char *name, TaskFn fn,
                   uint32_t interval_ms, uint8_t priority);
void    sched_enable (int8_t id);
void    sched_disable(int8_t id);
void    sched_remove (int8_t id);
void    sched_tick();        // call from loop()
uint8_t sched_load();        // CPU load 0–100%