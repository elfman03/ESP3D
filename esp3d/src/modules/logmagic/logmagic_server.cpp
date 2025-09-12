/*
  logmagic_server.cpp -  logmagic server functions class

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

#include "../../include/esp3d_config.h"

#if defined(LOGMAGIC_FEATURE) 

#include "../../core/esp3d_commands.h"
#include "../../core/esp3d_message.h"
#include "../../core/esp3d_settings.h"
#include "../../core/esp3d_string.h"
#include "../../include/esp3d_version.h"
#include "logmagic_server.h"

LogMagic_Server logmagic_server;

#define TIMEOUT_LOGMAGIC_FLUSH 1500

#define LOGMAGIC_WELCOME_MESSAGE ";Welcome to ESP3D V" FW_VERSION " logmagic.\r\n"

void LogMagic_Server::closeClient() {
  if (_logmagicClients) {
    _logmagicClients.stop();
  }
}

bool LogMagic_Server::isConnected() {
  if (!_started || _logmagicserver == NULL) {
    return false;
  }
  // check if there are any new clients
  if (_logmagicserver->hasClient()) {
    // find free/disconnected spot
    if (!_logmagicClients || !_logmagicClients.connected()) {
      if (_logmagicClients) {
        _logmagicClients.stop();
      }
      _logmagicClients = _logmagicserver->accept();
  #ifndef DISABLE_LOGMAGIC_WELCOME_MESSAGE
      // new client
      writeBytes((uint8_t *)LOGMAGIC_WELCOME_MESSAGE, strlen(LOGMAGIC_WELCOME_MESSAGE));
  #endif  // DISABLE_LOGMAGIC_WELCOME_MESSAGE
    }
  }
  if (_logmagicserver->hasClient()) {
    // no free/disconnected spot so reject
    _logmagicserver->accept().stop();
  }
  return _logmagicClients.connected();
}

LogMagic_Server::LogMagic_Server() {
  _started = false;
  _port = 0;
  _logmagicserver = nullptr;
}
LogMagic_Server::~LogMagic_Server() { end(); }

/**
 * begin LogMagic setup
 */
bool LogMagic_Server::begin(uint16_t port, bool debug) {
  end();
  // Get logmagic port
  if (port == 0) {
    // 8023 if telnet bridge is at 23
    _port = 8000+ESP3DSettings::readUint32(ESP_TELNET_PORT);
  } else {
    _port = port;
  }
  // create instance
  _logmagicserver = new WiFiServer(_port);
  if (!_logmagicserver) {
    return false;
  }
  _logmagicserver->setNoDelay(true);
  // start logmagic server
  _logmagicserver->begin();
  _started = true;
  return _started;
}
/**
 * End LogMagic
 */
void LogMagic_Server::end() {
  _started = false;
  _port = 0;
  closeClient();
  if (_logmagicserver) {
    delete _logmagicserver;
    _logmagicserver = nullptr;
  }
}

/**
 * Reset LogMagic
 */
bool LogMagic_Server::reset() {
  // nothing to reset
  return true;
}

bool LogMagic_Server::started() { return _started; }

void LogMagic_Server::handle() {
  ESP3DHal::wait(0);
  if (isConnected()) {
    // check clients for data
    size_t len = _logmagicClients.available();
    if (len > 0) {
      // if yes read them
      uint8_t *sbuf = (uint8_t *)malloc(len);
      if (sbuf) {
        size_t count = _logmagicClients.read(sbuf, len);
	// discard.  write only channel
        free(sbuf);
      }
    }
  }
}

size_t LogMagic_Server::writeBytes(const uint8_t *buffer, size_t size) {
  if (isConnected() && (size > 0) && _started) {
    if ((size_t)availableForWrite() >= size) {
      // push data to connected logmagic client
      return _logmagicClients.write(buffer, size);
    } else {
      size_t sizetosend = size;
      size_t sizesent = 0;
      uint8_t *buffertmp = (uint8_t *)buffer;
      uint32_t starttime = millis();
      // loop until all is sent or timeout
      while (sizetosend > 0 && ((millis() - starttime) < 100)) {
        size_t available = availableForWrite();
        if (available > 0) {
          // in case less is sent
          available = _logmagicClients.write(
              &buffertmp[sizesent],
              (available >= sizetosend) ? sizetosend : available);
          sizetosend -= available;
          sizesent += available;
          starttime = millis();
        } else {
          ESP3DHal::wait(5);
        }
      }
      return sizesent;
    }
  }
  return 0;
}

size_t LogMagic_Server::post(const char *buf, size_t size) {
   return writeBytes((const uint8_t*)buf,size);
}

size_t LogMagic_Server::post(const char *str) {
   return writeBytes((const uint8_t*)str,strlen(str));
}

int LogMagic_Server::availableForWrite() {
  if (!isConnected()) {
    return 0;
  }
#ifdef ARDUINO_ARCH_ESP32
  return 128;  // hard code for esp32
#endif         // ARDUINO_ARCH_ESP32
#ifdef ARDUINO_ARCH_ESP8266
  return _logmagicClients.availableForWrite();
#endif  // ARDUINO_ARCH_ESP8266
}

int LogMagic_Server::available() {
  if (isConnected()) {
    return _logmagicClients.available();
  }
  return 0;
}

#endif  // LOGMAGIC_FEATURE
