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
#include "../logmagic/logmagic_server.h"
#include <WiFiUDP.h>
#include "chitu_service.h"

#define UNKNOW_STATE 0x0
#define ERROR_STATE 0x1
#define SUCCESS_STATE 0x2

#define CHITU_INIT_BAUD_RATE 115200
#define CHITU_POSTINIT_BAUD_RATE 2250000

//#define SUPER_CHATTY 1

extern HardwareSerial *Serials[];

bool ChituService::_started = false;
uint8_t ChituService::_uploadStatus = UNKNOW_STATE;
bool ChituService::_uploadMode = false;
bool ChituService::_inDatagram = false;
WiFiUDP ChituService::_udp;

//
// Message bus message coming in from the esp3d core
//
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
    doGcodeMessage((const char*)message->data,message->size,IPAddress(0,0,0,0),0);
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
  _inDatagram=false;
  _udp.begin(3000);
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
  _started = true;
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

//
// REFACTOR -- used by http upload mode
//
bool ChituService::sendGcodeFrame(const char *cmd) {
  LOGMAGIC("sendGcodeFrame!!!\r\n");
  return true;
}

//
// Waits up for a second for a full line from Chitu and returns it in the provided buffer.  
// returns bytes read or zero in case of timeout or overflow.
//
int ChituService::pullChituLine(char *obuf, int max) {
  //
  // Wait up to two seconds for a response.  response ends with newline
  //
  uint8_t ser=esp3d_serial_service.serialIndex();
  uint32_t t1=millis();
  obuf[0]=0;
  int olen;
  bool newline=false;
  //
  // Allow up to max bytes from Chitu
  //
  for(olen=0;olen<max && !newline;olen++) {
    while(!Serials[ser]->available()) {
      ESP3DHal::wait(5);
      if((millis()-t1)>2000) {
        LOGMAGIC("ERROR - CHITU SERIAL GCODE ... NO RESPONSE COMPLETION IN 2s\r\n");
        LOGMAGIC(obuf,olen);
        LOGMAGIC("\r\n");
        obuf[0]=0;
        return 0;
      }
    }
    // byte by byte to not accidently read past the newline
    //
    obuf[olen]=Serials[ser]->read();
    if(obuf[olen]=='\n') { newline=true; }
  }
  //
  // Check for overflow
  //
  if(olen==max) {
    LOGMAGIC("ERROR - CHITU SERIAL GCODE OVERFLOW...\r\n");
    LOGMAGIC(obuf,max);
    LOGMAGIC("\r\n");
    obuf[0]=0;
    return 0;
  }
  obuf[olen]=0;
  //
  //
  //
  char ctmp[32];
  sprintf(ctmp,"CHITU OUTPUT (%d)\r\n",olen);
#ifdef SUPER_CHATTY
  LOGMAGIC(ctmp);
  LOGMAGIC(obuf,olen);
  LOGMAGIC("\r\n");
#endif
  //
  return olen;
}

//
// Send the GCode response off where it belongs.  Either to the Chitu HB via datagram or to the ESP3D message manager
//
void ChituService::sendResponseHome(const char *buf, int len, IPAddress ip, int port) {
  if(port) {
    //
    // datagram GCode.  Send out as a datagram
    //
#ifdef SUPER_CHATTY
    LOGMAGIC("CHITU RESPONSE TO UDP GUEST\r\n");
    LOGMAGIC(buf,len);
#endif
    _udp.beginPacket(ip,port);
    _udp.write(buf,len);
    _udp.endPacket();
  } else {
    //
    // Send the payload where it belongs (based on ESP3DSerialService::flushData for now)
    //
    ESP3DMessage *message=esp3d_message_manager.newMsg(ESP3DClientType::chitu_serial,ESP3DClientType::all_clients,(uint8_t*)buf,len,ESP3DAuthenticationLevel::admin);
    if(message) {
      message->type=ESP3DMessageType::unique;
      esp3d_commands.process(message);
#ifdef SUPER_CHATTY
      LOGMAGIC("PROCESSED OUTGOING PAYLOAD: ");
      LOGMAGIC(buf,len);
      LOGMAGIC("\r\n");
#endif
    } else {
      LOGMAGIC("COULD NOT CREATE ESP3D MESSAGE FROM PAYLOAD: ");
      LOGMAGIC(buf,len);
      LOGMAGIC("\r\n");
    }
  }
}

//
// Handle a Gcode request to the printer giving reasonable change for the printer to respond
//
void ChituService::doGcodeMessage(const char *msg, size_t len, IPAddress ip, int port) {
  char obuf[256];
  char ctmp[128];
  int olen;
  bool okfound=false;

#ifdef SUPER_CHATTY
  LOGMAGIC("Gcode request: ");
  LOGMAGIC(msg,len);
  LOGMAGIC("\r\n");
#endif

  //
  // Send to Chitu (a) +IPD header, (b) payload, (c) trailer
  //
  sprintf(obuf,"\r\n+IPD,4,%d:",len);
  esp3d_serial_service.writeBytes((const uint8_t*)obuf, strlen(obuf)); // header
  esp3d_serial_service.writeBytes((const uint8_t*)msg, len);           // payload
  sprintf(obuf,"\r\nOK,recv\r\n",len,msg);
  esp3d_serial_service.writeBytes((const uint8_t*)obuf, strlen(obuf)); // trailer
  esp3d_serial_service.flush();

  //
  // We expect CIPSEND(s) from Chitu with last one starting with ok.
  // CIPSEND ... Chitu IP Send?
  // AT+CIPSEND=4,<SIZE>\r<PAYLOAD>
  // incoming message length should equal the size plus the header bit up to the \r
  //
  //
  while(!okfound) {
    olen=ChituService::pullChituLine(obuf, 255);
    //
    // Verify we got a cipsend or bail.
    //
    if(obuf!=strstr(obuf,"AT+CIPSEND=4,")) {
      sprintf(ctmp,"ERROR - CHITU GCODE RESPONSE NOT CIPSEND len=%d olen=%d\r\n--",len,olen);
      LOGMAGIC(ctmp);
      LOGMAGIC(msg,len);
      LOGMAGIC("--\r\n--");
      LOGMAGIC(obuf,olen);
      LOGMAGIC("--\r\n");
      return;
    }
    // extract the payload length
    int paylen=atoi(&obuf[13]);
    // determine the payload start
    char *paystart=strchr(obuf,'\r');
    if(paystart) { paystart++; }
    // Sanity check log messages
    if(!paystart) { 
      LOGMAGIC("ERROR - CHITU CANNOT DETECT PAYLOAD START\r\n"); 
      LOGMAGIC(msg,len);
      LOGMAGIC("\r\n");
      LOGMAGIC(obuf,olen);
      LOGMAGIC("\r\n");
      return;
    }
    int expected=paylen+int(paystart-obuf);
    if(expected!=olen) { 
      sprintf(ctmp,"ERROR UNEXPECTED LENGTH msg_len=%d expected=%d (payload_len=%d header_len=%d)\r\n",len,expected,paylen,int(paystart-msg));
      LOGMAGIC(ctmp);
      LOGMAGIC(msg,len);
      LOGMAGIC("\r\n");
      LOGMAGIC(obuf,olen);
      return;
    }
    sendResponseHome(paystart, paylen, ip, port);
    sprintf(ctmp,"OK,SEND DONE\r\n");
    esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp));
    esp3d_serial_service.flush();
    //
    // Is this the last expected line in this series?
    //
    if(paylen>2 && paystart[0]=='o' && paystart[1]=='k') { okfound=true; }
  }
}

//
// Handle unsolicited traffic coming from the Chitu printer via serial
//
void ChituService::doChituMessage(const char *msg, size_t len) {
  char ctmp[256];

#ifdef SUPER_CHATTY
  LOGMAGIC("Chitu request: ");
  LOGMAGIC(msg,len);
  LOGMAGIC("\r\n");
#endif

  ctmp[0]=0;
  if(msg==strstr(msg,"AT+CIPSEND=4,")) {
    LOGMAGIC("ERROR - DROP UNSOLICITED CHITU CIPSEND...: ");
    LOGMAGIC(msg,len);
    LOGMAGIC("\r\n");
    sprintf(ctmp,"OK,SEND DONE\r\n");
  } else if(msg==strstr(msg,"AT+GMR\r\n")) {
    //
    // Handle AT+GMR request
    //
    //
    // real Chitu ESP returns an authentication related code followed by the firmware version.
    // return 0s for auth code seed and the ESP3D version
    // Based on experimentation, version must start with V and can contain up to 8 additional 
    //                           characters if followed by a \r\n or 9 addition if followed by just \n
    //
    //sprintf(ctmp,"+GMR:00,00,00,00,00,00,00,00 V10.0.12a\r\n\r\nOK\r\n");
    //sprintf(ctmp,"+GMR:00,00,00,00,00,00,00,00 V%s\n\r\nOK\r\n",FW_VERSION);
    sprintf(ctmp,"+GMR:00,00,00,00,00,00,00,00 V%s\n\r\nOK\r\n","ESP3d-3ce");
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
#ifdef SUPER_CHATTY
    LOGMAGIC("RESPONSE: ");
    LOGMAGIC(ctmp);
    LOGMAGIC("\r\n");
#endif
    esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp));
    esp3d_serial_service.flush();
  } else {
    LOGMAGIC("UNKNOWN REQUEST!!!!!  IGNORE!!!\r\n");
    LOGMAGIC(msg,len);
    LOGMAGIC("\r\n");
  }
  return;
}

//
// Handle Chitu HB incoming datagram.  It is either a special request handled by the ESP or gcode
//
void ChituService::doDatagram(char*buf, int sz, IPAddress srcIp, int srcPort) {
  char ctmp[128];
  ctmp[0]=0;
  //
  // Primordial message from client multicasts a M99999.  Respond with contact info
  // Format determined from wireshark of communication between ChituHB and actual Chitu ESP01
  //
  // ChituHB seems to have some extra special commands as well including
  // V102&102& - requests AP mode SSID and password
  // V103&103& - requests Station mode SSID and password
  // 
  // There are others for setting the AP/STA details and renaming the device but we will probably not provide those
  // since the UI can be used
  //
  if(sz==9 && strstr(buf,"U102&102&")) {
    sprintf(ctmp,"ok SSID:[SEE UI] PWD:[SEE UI] \r\n");
#ifdef SUPER_CHATTY
    LOGMAGIC("Respond to U102&102&: ");
    LOGMAGIC(ctmp);
#endif
  } else if(sz==9 && strstr(buf,"U103&103&")) {
    sprintf(ctmp,"ok SSID:[SEE UI] PWD:[SEE UI] \r\n");
#ifdef SUPER_CHATTY
    LOGMAGIC("Respond to U103&103&: ");
    LOGMAGIC(ctmp);
#endif
  } else if(sz==6 && strstr(buf,"M99999")) {
    //
    // extract local IP and MAC
    //
    IPAddress staip=WiFi.localIP();
    byte mac[6];
    WiFi.macAddress(mac);
    //
    // build the payload packet
    //
    sprintf(ctmp,"ok MAC:%02x:%02x:%02x:%02x:%02x:%02x IP:%d.%d.%d.%d VER:%s ID:00,00,00,00,00,00,00,00 NAME:%s\r\n",mac[5],mac[4],mac[3],mac[2],mac[1],mac[0],staip[0],staip[1],staip[2],staip[3],FW_VERSION,"ESP3d-3ce");
#ifdef SUPER_CHATTY
    LOGMAGIC("Respond to M99999: ");
    LOGMAGIC(ctmp);
#endif
  }
  if(ctmp[0]) {
    //
    // send special payload packet
    //
    _udp.beginPacket(srcIp,srcPort);
    _udp.write(ctmp);
    _udp.endPacket();
  } else {
    doGcodeMessage(buf,sz,srcIp,srcPort);
  }
}

void ChituService::handle() {
  //
  // For now receive datagrams from Chitu HB.  May add more later
  //
  char buf[1460];
  int rct,avail;
  //
  // dont try to process before we are started
  // dont try to process another if we are alreay handling one
  //
  if (_started && !_inDatagram) {
     while(avail=_udp.parsePacket()) {
       _inDatagram=true;
       IPAddress remoteIp=_udp.remoteIP();
       int remotePort=_udp.remotePort();
       rct=_udp.read(buf,1450);
#ifdef SUPER_CHATTY
       char ctmp[128];
       sprintf(ctmp,"CHITU RECEIVED datagram size %d [no trailing newlines]:",rct);
       LOGMAGIC(ctmp);
       LOGMAGIC(buf,rct);
       LOGMAGIC("\r\n");
#endif
       buf[rct]=0;
       doDatagram(buf,rct,remoteIp,remotePort);
       _inDatagram=false;
     }
  }
}

void ChituService::end() { 
  _started = false; 
  _udp.stop();
  _inDatagram=false;
}

#endif  // COMMUNICATION_PROTOCOL == CHITU_SERIAL
