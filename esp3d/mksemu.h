/*
  mksemu.h - ESP3D configuration class
      based on command.h

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef MKSEMU_h
#define MKSEMU_h
#include <Arduino.h>
#include "espcom.h"

extern bool can_accept_mksemu_packets;

class MKSEMU
{
public:
    static String buffer_serial;
    static String buffer_tcp;
    static uint8_t bufT2S[256];
    static char filename[64];
    static size_t bufT2Ssz, T2Sct;
    static int theOp;
    static int payloadSz;      // how large is the POST payload
    static int payloadOffset;  // how far into the POST payload are we?
    static int timeoutCt;      // timeout sanity during upload
#ifdef MKS_UPLOAD_M28EMU
    static void tcp_connection_reset ();
    static void read_buffer_serial (uint8_t *bytes, size_t len);
    static void read_buffer_tcp (uint8_t *bytes, size_t len);
#endif
};

#endif
