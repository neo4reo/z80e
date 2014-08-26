#include "runloop/runloop.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

long long get_time_nsec() {
	struct timespec sp;
	clock_gettime(CLOCK_MONOTONIC, &sp);

	return sp.tv_sec * 1000000000 + sp.tv_nsec;
}

typedef struct {
	int index;
	int after_cycle;
} timer_tick_t;

struct runloop_state {
	asic_t *asic;
	long long last_end;
	int spare_cycles;
	timer_tick_t *ticks;
	int max_tick_count;
};

runloop_state_t *runloop_init(asic_t *asic) {
	runloop_state_t *state = malloc(sizeof(runloop_state_t));

	state->asic = asic;
	state->last_end = get_time_nsec();
	int i;
	for (i = 0; i < asic->timers->max_timers; i++) {
		z80_hardware_timer_t *timer = &asic->timers->timers[i];
		if (timer->flags & TIMER_IN_USE) {
			timer->cycles_until_tick = asic->clock_rate / timer->frequency;
		}
	}

	state->ticks = malloc(sizeof(timer_tick_t) * 40);
	state->max_tick_count = 40;

	return state;
}

int runloop_compare(const void *first, const void *second) {
	const timer_tick_t *a = first;
	const timer_tick_t *b = second;

	return a->after_cycle - b->after_cycle;
}

void runloop_tick_cycles(runloop_state_t *state, int cycles) {
	int total_cycles = 0;
	int cycles_until_next_tick = cycles;
	int current_tick = 0;
	int i;
	for (i = 0; i < state->asic->timers->max_timers; i++) {
		z80_hardware_timer_t *timer = &state->asic->timers->timers[i];

		if (!(timer->flags & TIMER_IN_USE)) {
			continue;
		}

		int tot_cycles = cycles;
		if (timer->cycles_until_tick < tot_cycles) {
			retry:
			state->ticks[current_tick].index = i;
			state->ticks[current_tick].after_cycle = timer->cycles_until_tick + (cycles - tot_cycles);
			tot_cycles -= timer->cycles_until_tick;
			timer->cycles_until_tick = state->asic->clock_rate / timer->frequency;
			current_tick++;

			if (current_tick == state->max_tick_count) {
				state->max_tick_count += 10;
				state->ticks = realloc(state->ticks, sizeof(timer_tick_t) * state->max_tick_count);
			}

			if (timer->cycles_until_tick <= tot_cycles) {
				goto retry;
			}
		} else {
			timer->cycles_until_tick -= tot_cycles;
		}
	}

	qsort(state->ticks, current_tick, sizeof(timer_tick_t), runloop_compare);

	if (current_tick > 0) {
		cycles_until_next_tick = state->ticks[0].after_cycle;
	}

	int tick_i = 0;
	while (cycles > 0) {
		int ran = cycles_until_next_tick - cpu_execute(state->asic->cpu, cycles_until_next_tick);

		total_cycles += ran;
		cycles -= ran;

		if (total_cycles >= state->ticks[tick_i].after_cycle) {
			tick_i++;
			if (tick_i <= current_tick) {
				int index = state->ticks[tick_i - 1].index;
				z80_hardware_timer_t *timer = &state->asic->timers->timers[index];
				timer->on_tick(state->asic, timer->data);
				cycles_until_next_tick = state->ticks[tick_i].after_cycle - total_cycles;
			} else {
				cycles_until_next_tick = cycles;
			}
		}
	}
	state->spare_cycles = cycles;
}

void runloop_tick(runloop_state_t *state) {
	long long now = get_time_nsec();
	long long ticks_between = now - state->last_end;

	float seconds = (float)ticks_between / (float)1000000000;
	int cycles = seconds * (float)state->asic->clock_rate;

	if (cycles == 0)
		return;

	runloop_tick_cycles(state, cycles);
	state->last_end = now;
}
