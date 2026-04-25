// scheduler.cpp
#include "scheduler.h"
#include <Arduino.h>
#include <string.h>

static Task    _tasks[MAX_TASKS];
static uint8_t _count = 0;
static uint32_t _busy_us = 0, _total_us = 1;

void sched_init() { memset(_tasks, 0, sizeof(_tasks)); _count = 0; }

int8_t sched_add(const char *name, TaskFn fn,
                  uint32_t iv, uint8_t prio) {
    if (_count >= MAX_TASKS) return -1;
    Task *t  = &_tasks[_count];
    t->fn    = fn;
    t->interval_ms = iv;
    t->last_ms     = 0;
    t->priority    = prio;
    t->enabled     = true;
    strncpy(t->name, name, 11); t->name[11] = 0;
    // insertion sort by priority
    for (int i = _count; i > 0; i--) {
        if (_tasks[i-1].priority > _tasks[i].priority)
            { Task tmp=_tasks[i-1]; _tasks[i-1]=_tasks[i]; _tasks[i]=tmp; }
        else break;
    }
    return _count++;
}

void sched_enable (int8_t id) { if(id>=0&&id<_count) _tasks[id].enabled=true; }
void sched_disable(int8_t id) { if(id>=0&&id<_count) _tasks[id].enabled=false; }
void sched_remove (int8_t id) {
    if(id<0||id>=_count) return;
    for(int i=id;i<_count-1;i++) _tasks[i]=_tasks[i+1];
    _count--;
}

void sched_tick() {
    uint32_t now = millis();
    uint32_t start = micros();
    for (uint8_t i = 0; i < _count; i++) {
        Task *t = &_tasks[i];
        if (!t->enabled) continue;
        if (t->interval_ms == 0 || now - t->last_ms >= t->interval_ms) {
            uint32_t t0 = micros();
            t->fn();
            _busy_us += micros() - t0;
            t->last_ms = now;
        }
    }
    _total_us += micros() - start + 1;
    if (_total_us > 1000000) {
        _total_us >>= 1; _busy_us >>= 1;
    }
}

uint8_t sched_load() {
    return (uint8_t)((_busy_us * 100) / _total_us);
}