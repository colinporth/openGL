// udp.h
#pragma once

void udpOpen();
void udpReset();

int udpSetFilter (uint16_t i_pid );
void udpUnsetFilter (int i_fd, uint16_t i_pid );
