#include "ti/asic.h"
#include "log/log.h"
#include "ti/memory.h"
#include "ti/hardware/link.h"

#include <stdlib.h>
#include <string.h>

uint8_t read_link_port(void *device) {
	link_state_t *state = device;
	switch (state->asic->device) {
	case TI73:
	case TI83p:
		// TODO: Link assist for 83+ models
		return (
			(state->them.tip | state->us.tip) |
			((state->them.ring | state->us.ring) << 1) |
			(state->us.tip << 4) |
			(state->us.ring << 5)
		);
	case TI83pSE:
	case TI84p:
	case TI84pSE:
	case TI84pCSE:
		return (
			(state->them.tip | state->us.tip) |
			((state->them.ring | state->us.ring) << 1) |
			(state->us.tip << 4) |
			(state->us.ring << 5)
		);
	default:
		return 0;
	}
}

void write_link_port(void *device, uint8_t val) {
	link_state_t *state = device;
	uint8_t tip = val & 1;
	uint8_t ring = val & 2;
	state->us.tip = tip;
	state->us.ring = ring;
}

uint8_t read_link_assist_enable_port(void *device) {
	link_state_t *state = device;
	switch (state->asic->device) {
	case TI73:
	case TI83p:
		return 0;
	default:
		return state->interrupts.mask;
	}
}

void write_link_assist_enable_port(void *device, uint8_t val) {
	link_state_t *state = device;
	switch (state->asic->device) {
	case TI73:
	case TI83p:
		break;
	default:
		state->interrupts.mask = val;
		break;
	}
}

uint8_t read_link_assist_buffer_port(void *device) {
	link_state_t *state = device;
	switch (state->asic->device) {
	case TI73:
	case TI83p:
		return 0;
	default:
		if (!state->assist.status.rx_ready) {
			return 0;
		}
		if (state->assist.status.int_tx_ready) {
			state->asic->cpu->interrupt = 0;
		}
		state->assist.status.rx_ready = false;
		state->assist.status.int_rx_ready = false;
		uint8_t val = state->assist.rx_buffer;
		state->assist.rx_buffer = 0;
		return val;
	}
}

void write_link_assist_buffer_port(void *device, uint8_t val) {
	// Not emualted by z80e
}

uint8_t read_link_assist_status_port(void *device) {
	link_state_t *state = device;
	switch (state->asic->device) {
	case TI73:
	case TI83p:
		return 0;
	default:
		return state->assist.status.u8;
	}
}

void write_link_assist_status_port(void *device, uint8_t val) {
	// Not emualted by z80e
}

void init_link_ports(asic_t *asic) {
	link_state_t *state = malloc(sizeof(link_state_t));

	memset(state, 0, sizeof(link_state_t));
	state->asic = asic;

	z80iodevice_t link_port = { state, read_link_port, write_link_port };
	z80iodevice_t link_assist_enable = { state, read_link_assist_enable_port, write_link_assist_enable_port };
	z80iodevice_t link_assist_status = { state, read_link_assist_status_port, write_link_assist_status_port };
	z80iodevice_t link_assist_buffer_read = { state, read_link_assist_buffer_port, write_link_assist_buffer_port };

	asic->cpu->devices[0x00] = link_port;
	asic->cpu->devices[0x08] = link_assist_enable;
	asic->cpu->devices[0x09] = link_assist_status;
	asic->cpu->devices[0x0A] = link_assist_buffer_read;
}

void free_link_ports(asic_t *asic) {
	free(asic->cpu->devices[0x00].device);
}

bool link_recv_byte(asic_t *asic, uint8_t val) {
	link_state_t *state = asic->cpu->devices[0x00].device;
	if (state->assist.status.rx_ready) {
		return false;
	}
	state->assist.status.rx_ready = state->assist.status.int_rx_ready = true;
	state->assist.rx_buffer = val;

	if (!state->interrupts.disabled && !state->interrupts.rx) {
		asic->cpu->interrupt = 1;
	}
	return true;
}

int link_read_tx_buffer(asic_t *asic) {
	link_state_t *state = asic->cpu->devices[0x00].device;
	if (state->assist.status.tx_active) {
		state->assist.status.tx_active = false;
		state->assist.status.tx_ready = state->assist.status.int_tx_ready = true;
		if (!state->interrupts.disabled && !state->interrupts.tx) {
			asic->cpu->interrupt = 1;
		}
		return state->assist.tx_buffer;
	}
	return EOF;
}
