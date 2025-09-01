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

#if defined(LOGMAGIC_FEATURE) || \
    (defined(ESP_LOG_FEATURE) && ESP_LOG_FEATURE == LOG_OUTPUT_LOGMAGIC)


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
      writeBytes((uint8_t *)LOGMAGIC_WELCOME_MESSAGE,
                 strlen(LOGMAGIC_WELCOME_MESSAGE));
  #endif  // DISABLE_LOGMAGIC_WELCOME_MESSAGE
    }
  }
  if (_logmagicserver->hasClient()) {
    // no free/disconnected spot so reject
    _logmagicserver->accept().stop();
  }
  return _logmagicClients.connected();
}

const char *LogMagic_Server::clientIPAddress() {
  static String res;
  res = "0.0.0.0";
  if (_logmagicClients && _logmagicClients.connected()) {
    res = _logmagicClients.remoteIP().toString();
  }
  return res.c_str();
}

LogMagic_Server::LogMagic_Server() {
  _buffer_size = 0;
  _started = false;
  _isdebug = false;
  _port = 0;
  _buffer = nullptr;
  _logmagicserver = nullptr;
  initAuthentication();
}
LogMagic_Server::~LogMagic_Server() { end(); }

/**
 * begin LogMagic setup
 */
bool LogMagic_Server::begin(uint16_t port, bool debug) {
  end();
  //if (ESP3DSettings::readByte(ESP_LOGMAGIC_ON) != 1) {
  //  return true;
  //}
  // Get logmagic port
  if (port == 0) {
    // 8023
    _port = 8000+ESP3DSettings::readUint32(ESP_TELNET_PORT);
  } else {
    _port = port;
  }
  _isdebug = debug;
  if (!_isdebug) {
    _buffer = (uint8_t *)malloc(ESP3D_LOGMAGIC_BUFFER_SIZE + 1);
    if (!_buffer) {
      return false;
    }
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
  _lastflush = millis();
  return _started;
}
/**
 * End LogMagic
 */
void LogMagic_Server::end() {
  _started = false;
  _buffer_size = 0;
  _port = 0;
  _isdebug = false;
  closeClient();
  if (_logmagicserver) {
    delete _logmagicserver;
    _logmagicserver = nullptr;
  }

  if (_buffer) {
    free(_buffer);
    _buffer = nullptr;
  }
#if defined(AUTHENTICATION_FEATURE)
  _auth = ESP3DAuthenticationLevel::guest;
#else
  _auth = ESP3DAuthenticationLevel::admin;
#endif  // AUTHENTICATION_FEATURE
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
	// CLE - Comment out because this is a write only channel
        // push to buffer
        //if (count > 0) {
        //  push2buffer(sbuf, count);
        //}
        // free buffer
        free(sbuf);
      }
    }
  }
  // we cannot left data in buffer too long
  // in case some commands "forget" to add \n
  if (((millis() - _lastflush) > TIMEOUT_LOGMAGIC_FLUSH) && (_buffer_size > 0)) {
    flushBuffer();
  }
}

bool LogMagic_Server::dispatch(ESP3DMessage *message) {
  if (!message || !_started) {
    return false;
  }
  if (message->size > 0 && message->data) {
    size_t sentcnt = writeBytes(message->data, message->size);
    if (sentcnt != message->size) {
      return false;
    }
    esp3d_message_manager.deleteMsg(message);
    return true;
  }
  return false;
}

void LogMagic_Server::initAuthentication() {
#if defined(AUTHENTICATION_FEATURE)
  _auth = ESP3DAuthenticationLevel::guest;
#else
  _auth = ESP3DAuthenticationLevel::admin;
#endif  // AUTHENTICATION_FEATURE
}
ESP3DAuthenticationLevel LogMagic_Server::getAuthentication() { return _auth; }



void LogMagic_Server::flushData(const uint8_t *data, size_t size, ESP3DMessageType type) {
  ESP3DMessage *message = esp3d_message_manager.newMsg(
      ESP3DClientType::logmagic, esp3d_commands.getOutputClient(), data,
      size, _auth);

  if (message) {
    message->type = type;
    esp3d_log("Process Message");
    esp3d_commands.process(message);
  } else {
    esp3d_log_e("Cannot create message");
  }
  _lastflush = millis();
}


void LogMagic_Server::flushChar(char c) { flushData((uint8_t *)&c, 1, ESP3DMessageType::realtimecmd); }

void LogMagic_Server::flushBuffer() {
  _buffer[_buffer_size] = 0x0;
  flushData((uint8_t *)_buffer, _buffer_size, ESP3DMessageType::unique);
  _buffer_size = 0;
}


void LogMagic_Server::push2buffer(uint8_t *sbuf, size_t len) {
  if (!_buffer || !_started) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    _lastflush = millis();
    if (esp3d_string::isRealTimeCommand(sbuf[i])) {
      flushChar(sbuf[i]);
    } else {
      _buffer[_buffer_size] = sbuf[i];
      _buffer_size++;
      if (_buffer_size > ESP3D_LOGMAGIC_BUFFER_SIZE ||
          _buffer[_buffer_size - 1] == '\n') {
        flushBuffer();
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

size_t LogMagic_Server::post(const char *buf, size_t size, bool isFinal) {
   size_t ret=writeBytes((const uint8_t*)buf,size);
   if(isFinal) { closeClient(); }
   return ret;
}

size_t LogMagic_Server::post(const char *str, bool isFinal) {
   return post(str,strlen(str),isFinal);
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

size_t LogMagic_Server::readBytes(uint8_t *sbuf, size_t len) {
  if (isConnected()) {
    if (_logmagicClients.available() > 0) {
      return _logmagicClients.read(sbuf, len);
    }
  }
  return 0;
}

void LogMagic_Server::flush() { _logmagicClients.flush(); }

#endif  // LOGMAGIC_FEATURE
