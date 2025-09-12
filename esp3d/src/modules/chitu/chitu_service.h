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
  static void handle();
  static void end();
  static bool started() { return _started; }
  static bool uploadBegin(const char* filename, size_t filesize);
  static bool uploadMiddle(const char* buf, size_t offset, size_t len);
  static bool uploadEnd(const char* filename, bool printit);
  static void uploadAbort();
  static size_t uploadStartTime();

 private:
  static char _gcode_rbuf[256];
  static const char *doGcodeMessage(const char* msg, size_t len, unsigned char *sixpack, IPAddress udpIP, int udpPort, ESP3DClientType toType);
  static void sendResponseHome(const char* buf, int len, IPAddress udpIP, int udpPort, ESP3DClientType toType);
  static int pullChituLine(char* obuf, int maxlen);
  static void doChituMessage(const char* msg, size_t len);
  static void doDatagram(char* buf, int sz, IPAddress srcIp, int srcPort);
  static bool lock();
  static bool unlock();
  static void resetLockTimeout(uint32_t t);
  static bool _started;
  static bool _inDatagram;
  static WiFiUDP _udp;
  static bool _uploadInprogress;
  static bool _uploadSuccess;
  static uint32_t _uploadStartts;
  static size_t _uploadSz;
  static bool _locked;
  static uint32_t _lockTs;
};

#endif  //_CHITU_SERVICES_H
