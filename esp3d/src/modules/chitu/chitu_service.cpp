/*
  chitu_service.cpp -  chitu communication service functions class
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

#include "../../include/esp3d_config.h"
#if COMMUNICATION_PROTOCOL == CHITU_SERIAL
#include "../../core/esp3d_message.h"
#include "../../core/esp3d_settings.h"
#include "../http/http_server.h"
#include "../network/netconfig.h"
#include "../serial/serial_service.h"
#include "../telnet/telnet_server.h"
#include "../wifi/wificonfig.h"
#ifdef LOGMAGIC_FEATURE
#include "../logmagic/logmagic_server.h"
#endif // LOGMAGIC_FEATURE
#include "chitu_service.h"

#define UNKNOW_STATE 0x0
#define ERROR_STATE 0x1
#define SUCCESS_STATE 0x2

#define CHITU_INIT_BAUD_RATE 115200
#define CHITU_POSTINIT_BAUD_RATE 2250000

bool ChituService::_started = false;
uint8_t ChituService::_uploadStatus = UNKNOW_STATE;
bool ChituService::_uploadMode = false;

bool ChituService::dispatch(ESP3DMessage *message) {
  char ctmp[128];
  //
  // sanity to avoid responding when impossible
  //
  if (!message || !_started) {
    return false;
  }
  //
  // If message originates from chitu serial
  //
  if(message->origin==ESP3DClientType::serial) {
    doChituMessage((const char*)message->data,message->size);
    esp3d_message_manager.deleteMsg(message);
    return true;
  }
  if(message->origin==ESP3DClientType::http) {
    doGcodeMessage((const char*)message->data,message->size);
    esp3d_message_manager.deleteMsg(message);
    return true;
  }
  //
  // Message originates from unhandled direction
  //
  sprintf(ctmp,"chitu dispatch: origin=%d .. ignore...: ",message->origin);
  LOGMAGIC(ctmp);
  LOGMAGIC((const char*)message->data,message->size);
  LOGMAGIC("\r\n");
  return false;
}

bool ChituService::begin() {
  _started = true;
  //
  // Analysis indicates that this payload is sent by an official Chitu ESP01 (Qidi X-Plus)
  // https://github.com/elfman03/ChituAnalyzer
  // The chitu will send a AT+UART_CUR=2250000,8,1,0,0 but because we already send 
  // the OK for that command here, we should not need to look for that...
  //
  esp3d_serial_service.updateBaudRate(CHITU_INIT_BAUD_RATE);
  const char *ctmp="\r\n;auth ok 2\r\n\r\nready\r\n;CONNECT,4\r\n\r\nOK\r\n";
  if(esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp)) == strlen(ctmp)) {
    esp3d_log("ChituService Begin Message Sent");
  } else {
    esp3d_log("ChituService Begin Message Failure");
    return false;
  }
  commandMode(true);
  return true;
}

//
// used by http upload mode
//
void ChituService::commandMode(bool fromSettings) {
  //if (fromSettings) {
  //  _commandBaudRate = ESP3DSettings::readUint32(ESP_BAUD_RATE);
  //}
  esp3d_log("Cmd Mode");
  LOGMAGIC("CHITU -- Cmd Mode\r\n");
  _uploadMode = false;
  esp3d_serial_service.updateBaudRate(CHITU_POSTINIT_BAUD_RATE);
}
//
// REFACTOR -- used by http upload mode
//
void ChituService::uploadMode() {
  esp3d_log("Upload Mode");
  LOGMAGIC("CHITU -- Upload Mode\r\n");
  _uploadMode = true;
  //esp3d_serial_service.updateBaudRate(UPLOAD_BAUD_RATE);
}

//
// REFACTOR -- used by http upload mode
//
uint ChituService::getFragmentID(uint32_t fragmentNumber, bool isLast) {
  LOGMAGIC("getFragmentID\r\n");
  esp3d_log("Fragment: %d %s", fragmentNumber, isLast ? " is last" : "");
  if (isLast) {
    fragmentNumber |= (1 << 31);
  } else {
    fragmentNumber &= ~(1 << 31);
  }
  esp3d_log("Fragment is now: %d", fragmentNumber);
  return fragmentNumber;
}

//
// REFACTOR -- used by http upload mode
//
bool ChituService::sendFirstFragment(const char *filename, size_t filesize) {
  uint fileNameLen = strlen(filename);
  uint dataLen = fileNameLen + 5;
  LOGMAGIC("sendFirstFragment\r\n");
  esp3d_log("Filename: %s  Filesize: %d", filename, filesize);
  esp3d_log("Ok");
  return true;
}

//
// REFACTOR -- used by http upload mode
//
bool ChituService::sendFragment(const uint8_t *dataFrame, const size_t dataSize,
                              uint fragmentID) {
  uint dataLen = dataSize + 4;
  esp3d_log("Fragment datalen:%d", dataSize);
  LOGMAGIC("sendFragment\r\n");
  esp3d_log("Ok");
  return true;
}

void ChituService::doGcodeMessage(const char *msg, size_t len) {
  char ctmp[128];

  LOGMAGIC("Gcode request: ");
  LOGMAGIC(msg,len);
  LOGMAGIC("\r\n");
  if(len<100) {
    sprintf(ctmp,"\r\n+IPD,4,%d:%s\r\nOK,recv\r\n",len,msg);
    esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp));
    LOGMAGIC(ctmp);
  } else {
    sprintf(ctmp,"MESSAGE TOO LONG! len=%d\r\n",len);
    LOGMAGIC(ctmp);
  }
}

void ChituService::doChituMessage(const char *msg, size_t len) {
  char ctmp[256];

  LOGMAGIC("Chitu request: ");
  LOGMAGIC(msg,len);
  LOGMAGIC("\r\n");
  ctmp[0]=0;

  //
  // CIPSEND ... Chitu IP Send?
  // AT+CIPSEND=4,<SIZE>\r<PAYLOAD>
  // incoming message length should equal the size plus the header bit up to the \r
  //
  if(msg==strstr(msg,"AT+CIPSEND=4,")) {
    // extract the payload length
    int paylen=atoi(&msg[13]);
    // determine the payload start
    char *paystart=strchr(msg,'\r');
    if(paystart) { paystart++; }
    // Sanity check log messages
    if(!paystart) { LOGMAGIC("CANNOT DETECT PAYLOAD LENGTH\r\n"); }
    int expected=paylen+int(paystart-msg);
    if(expected!=len) { 
      sprintf(ctmp,"UNEXPECTED LENGTH msg_len=%d expected=%d (payload_len=%d header_len=%d)\r\n",len,expected,paylen,int(paystart-msg));
      LOGMAGIC(ctmp);
    }
    //
    // Send the payload where it belongs (based on ESP3DSerialService::flushData for now)
    //
    ESP3DMessage *message=esp3d_message_manager.newMsg(ESP3DClientType::chitu_serial,ESP3DClientType::all_clients,(uint8_t*)paystart,paylen,ESP3DAuthenticationLevel::admin);
    if(message) {
      message->type=ESP3DMessageType::unique;
      esp3d_commands.process(message);
      LOGMAGIC("PROCESSED OUTGOING PAYLOAD: ");
      LOGMAGIC(paystart,paylen);
      LOGMAGIC("\r\n");
    } else {
      LOGMAGIC("COULD NOT CREATE ESP3D MESSAGE FROM PAYLOAD: ");
      LOGMAGIC(paystart,paylen);
      LOGMAGIC("\r\n");
    }
    sprintf(ctmp,"OK,SEND DONE\r\n");
  } else if(msg==strstr(msg,"AT+GMR\r\n")) {
    //
    // Handle AT+GMR request
    //
    //
    // real Chitu ESP returns an authentication related code followed by the firmware version.
    // return xx for the authenticaion code nd the ESP3D version
    //
    sprintf(ctmp,"+GMR:xx,xx,xx,xx,xx,xx,xx,xx V10.0.12aa\r\n","FW_VERSION");
    //sprintf(ctmp,"+GMR:xx,xx,xx,xx,xx,xx,xx,xx V%s\r\n",FW_VERSION);
  } else if(msg==strstr(msg,"AT+CIFSR\r\n")) {
    //
    // real ESP returns these lines.  
    //
    // APIP (standalone access point mode IP address)
    // APMAC (standalone access point mac).  Not used by qidi gui so I do not populate now
    // STAIP (station IP address).  
    // STAMAC (station access point mac).  Not used by qidi gui so I do not populate now
    //
    IPAddress apip(ESP3DSettings::read_IP(ESP_AP_IP_VALUE));
    IPAddress staip=WiFi.localIP();
    sprintf(ctmp,"+CIFSR:APIP,%d.%d.%d.%d\r\n+CIFSR:APMAC,%s\r\n+CIFSR:STAIP,%d.%d.%d.%d\r\n+CIFSR:STAMAC,%s\r\n\r\nOK\r\n",apip[0],apip[1],apip[2],apip[3],"00:00:00:00:00:00",staip[0],staip[1],staip[2],staip[3],"00:00:00:00:00:00");
  } else if(msg==strstr(msg,"AT+CWJAP?\r\n")) {
    //
    // Station mode access point
    //
    sprintf(ctmp,"+CWJAP:\"%s\"\r\n\r\nOK\r\n",ESP3DSettings::readString(ESP_STA_SSID));
  } else if(msg==strstr(msg,"AT+CWSAP?\r\n")) {
    //
    // Standalone access point details.  insert [e] and [d] to indicate enablement
    //
    String savedSsid = ESP3DSettings::readString(ESP_AP_SSID);
    String savedPassword = ESP3DSettings::readString(ESP_AP_PASSWORD);
    const char *rmc="[d]";
    uint8_t rm=ESP3DSettings::readByte(ESP_RADIO_MODE);
    if(rm==ESP_WIFI_AP) { rmc="[e]"; }
    sprintf(ctmp,"+CWSAP:%s\"%s\",\"%s\",1,0\r\n\r\nOK\r\n",rmc,savedSsid.c_str(),savedPassword.c_str());
  }
  //
  // If we have built up a response write it out.
  //
  if(ctmp[0]) {
    LOGMAGIC("RESPONSE: ");
    LOGMAGIC(ctmp);
    LOGMAGIC("\r\n");
    esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp));
  } else {
    LOGMAGIC("UNKNOWN REQUEST!!!!!  IGNORE!!!\r\n");
  }
  return;
}

//
// REFACTOR -- used by http upload mode
//
bool ChituService::sendGcodeFrame(const char *cmd) {
  LOGMAGIC("sendGcodeFrame!!!\r\n");
  return true;
}

void ChituService::handle() {
  if (_started) {
     // TODO every 10 seconds
  }
}
void ChituService::end() { _started = false; }

#endif  // COMMUNICATION_PROTOCOL == CHITU_SERIAL
