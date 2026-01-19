#include "config.h"

void config_default(MslConfig* out, int num_players) {
  if (out == 0) {
    return;
  }
  out->num_players = (uint8_t)num_players;
  out->ucf_enabled = 1;
  out->ucf_cardinals_1_0_enabled = 0;
  out->_pad0[0] = 0;
}
