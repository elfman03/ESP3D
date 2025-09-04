/*
  logmagic_server.h -  logmagic service functions class

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

#ifndef _LOGMAGIC_SERVER_H
#define _LOGMAGIC_SERVER_H

#if !defined(LOGMAGIC_FEATURE)
#define LOGMAGIC(...) 
#else

#include <WiFiClient.h>
#include <WiFiServer.h>

#include "../../core/esp3d_message.h"

#define ESP3D_LOGMAGIC_BUFFER_SIZE 1200

#ifdef LOGMAGIC_FEATURE
  #define LOGMAGIC(...) logmagic_server.post(__VA_ARGS__);
#else
#endif

class LogMagic_Server {
 public:
  LogMagic_Server();
  ~LogMagic_Server();
  bool begin(uint16_t port = 0, bool debug = false);
  void end();
  void handle();
  bool reset();
  bool started();
  bool isConnected();
  size_t writeBytes(const uint8_t* buffer, size_t size);
  size_t post(const char *str);
  size_t post(const char *buf, size_t size);
  int available();
  int availableForWrite();
  uint16_t port() { return _port; }
  void closeClient();

 private:
  bool _started;
  WiFiServer* _logmagicserver;
  WiFiClient _logmagicClients;
  uint16_t _port;
  uint32_t _lastflush;
};

extern LogMagic_Server logmagic_server;

#endif // LOGMAGIC_FEATURE
#endif
