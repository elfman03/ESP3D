/*
 handle-chitu-files.cpp - ESP3D http handle
    based on handle-mks-files.cpp

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
#include "../../../include/esp3d_config.h"
#if defined(HTTP_FEATURE) && (COMMUNICATION_PROTOCOL == CHITU_SERIAL)
#include "../http_server.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <WebServer.h>
#endif  // ARDUINO_ARCH_ESP32
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WebServer.h>
#endif  // ARDUINO_ARCH_ESP8266
#include "../../authentication/authentication_service.h"
#include "../../chitu/chitu_service.h"
#include "../../logmagic/logmagic_server.h"

void HTTP_Server::handleChituUpload() {
LOGMAGIC("start handleChituUpload()\r\n");
  ESP3DAuthenticationLevel auth_level =
      AuthenticationService::getAuthenticatedLevel();
  if (auth_level == ESP3DAuthenticationLevel::guest) {
LOGMAGIC("guest auth error\r\n");
    _upload_status = UPLOAD_STATUS_NONE;
    _webserver->send(401, "text/plain", "Wrong authentication!");
    return;
  }
  if ((_upload_status == UPLOAD_STATUS_FAILED) ||
      (_upload_status == UPLOAD_STATUS_CANCELLED)) {
LOGMAGIC("cancel fail error\r\n");
    _webserver->send(500, "text/plain", "Upload failed!");
    _upload_status = UPLOAD_STATUS_NONE;
    return;
  }
  // no error
LOGMAGIC("success\r\n");
  _webserver->send(200, "text/plain", "{\"status\":\"ok\"}");
  _upload_status = UPLOAD_STATUS_NONE;
}

#endif  // HTTP_FEATURE && (COMMUNICATION_PROTOCOL == CHITU_SERIAL)
