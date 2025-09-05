/*
  chitu_service.h -  chitu communication service functions class
      based on mks_service

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with This code; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _CHITU_SERVICES_H
#define _CHITU_SERVICES_H

#define CHITU_FRAME_SIZE 1024
#define CHITU_FRAME_DATA_MAX_SIZE (CHITU_FRAME_SIZE - 5 - 4)
#include "../../core/esp3d_message.h"
#include <WiFiUDP.h>

class ChituService {
 public:
  static bool begin();
  static bool dispatch(ESP3DMessage* message);
  static bool sendGcodeFrame(const char* cmd);
  static void handle();
  static void end();
  static bool started() { return _started; }
  static bool sendFirstFragment(const char* filename, size_t filesize);
  static bool sendFragment(const uint8_t* dataFrame, const size_t dataSize,
                           uint fragmentID);
  static uint getFragmentID(uint32_t fragmentNumber, bool isLast = false);
  static void commandMode(bool fromSettings = false);
  static void uploadMode();

 private:
  static uint8_t _uploadStatus;
  static void doGcodeMessage(const char* msg, size_t len, IPAddress ip, int port);
  static void sendResponseHome(const char* buf, int len, IPAddress ip, int port);
  static int pullChituLine(char* obuf, int maxlen);
  static void doChituMessage(const char* msg, size_t len);
  static void doDatagram(const char* buf, int sz, IPAddress srcIp, int srcPort);
  static bool _started;
  static WiFiUDP _udp;
  static bool _uploadMode;
};

#endif  //_CHITU_SERVICES_H
