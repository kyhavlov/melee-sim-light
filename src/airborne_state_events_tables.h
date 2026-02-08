#ifndef MSL_AIRBORNE_STATE_EVENTS_TABLES_H
#define MSL_AIRBORNE_STATE_EVENTS_TABLES_H

#include <stdint.h>

// Optional runtime tables extracted from fighter movescripts (opcode 25 / set_airborne_state).
//
// Data source:
// - tools/extraction/extract_fighter_airborne_state_events.py
// - output: data/airborne_state_events/{fox,falco}.bin
//
// Return values for `airborne_state_event_get`:
// - 0: event present for (char, msid, frame), `*out_state` set (0/1/2).
// - 1: table missing / no entry / no event at this frame.
// - -1: invalid arguments.
int airborne_state_events_tables_init(void);
int airborne_state_event_get(uint8_t char_id, uint16_t msid, uint16_t frame, uint8_t* out_state);

#endif  // MSL_AIRBORNE_STATE_EVENTS_TABLES_H
