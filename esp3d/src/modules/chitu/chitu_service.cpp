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

// ESP01 starts at 115200
#define CHITU_INIT_BAUD_RATE 115200

// Qidi XPlus gen1 pushes baud to 2250000
#define CHITU_POSTINIT_BAUD_RATE 2250000

// dont hold lock for more than 2s
#define MAXLOCK 2000
// How long to wait for bytes from Chitu before giving up.  
// NOTE: if keep pressing refresh file listing on ChituHB while doing info 
// checkbox in the background 1s is not enough.  Settling on 2s for now
#define MAX_SERIAL_WAIT 2000

//#define SUPER_CHATTY 1

extern HardwareSerial *Serials[];

bool ChituService::_started = false;
bool ChituService::_uploadInprogress = false;
bool ChituService::_uploadSuccess = false;
size_t ChituService::_uploadSz = 0;
bool ChituService::_inDatagram = false;
WiFiUDP ChituService::_udp;
bool ChituService::_locked = false;
uint32_t ChituService::_lockTs = 0;
char ChituService::_gcode_rbuf[256];

//
// Message bus message coming in from the esp3d core
//
bool ChituService::dispatch(ESP3DMessage *message) {
  char ctmp[128];
  bool ret=false;
  bool done=false;

  if(!lock()) {
    LOGMAGIC("ChituService::dispatch fail to acquire lock.\r\n");
    return false;
  }

  //
  // sanity to avoid responding when impossible
  //
  if (!message || !_started) {
    ret=false; done=true;
  }

  //
  // If message originates from chitu serial
  //
  if(!done && message->origin==ESP3DClientType::serial) {
    doChituMessage((const char*)message->data,message->size);
    esp3d_message_manager.deleteMsg(message);
    ret=true; done=true;
  }
  if(!done && message->origin==ESP3DClientType::http) {
    doGcodeMessage((const char*)message->data,message->size,IPAddress(0,0,0,0),0,ESP3DClientType::webui_websocket);
    esp3d_message_manager.deleteMsg(message);
    ret=true; done=true;
  }
  if(!done && message->origin==ESP3DClientType::telnet) {
    sprintf(ctmp,"telnet dispatch len=%d:",message->size);
    LOGMAGIC(ctmp);
    LOGMAGIC((const char*)message->data,message->size);
    LOGMAGIC("\r\n");
    doGcodeMessage((const char*)message->data,message->size,IPAddress(0,0,0,0),0,ESP3DClientType::telnet);
    esp3d_message_manager.deleteMsg(message);
    ret=true; done=true;
  }
  //
  // Message originates from unhandled direction
  //
  if(!done) {
    sprintf(ctmp,"chitu dispatch: origin=%d .. ignore...: ",message->origin);
    LOGMAGIC(ctmp);
    LOGMAGIC((const char*)message->data,message->size);
    LOGMAGIC("\r\n");
    ret=false; done=true;
  }

  unlock();
  return ret;
}

bool ChituService::begin() {
  unlock();  // intentionally unpaired unlock
  _uploadInprogress = false;
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
  esp3d_serial_service.updateBaudRate(CHITU_POSTINIT_BAUD_RATE);
  _started = true;
  return true;
}

//
// used by http upload mode... upload is beginning for a named file
//
bool ChituService::uploadBegin(const char *filename, size_t filesize) {
  char ctmp[128];
#ifdef SUPER_CHATTY
  sprintf(ctmp,"uploadBegin fn=%s approx_size=%d\r\n",filename,filesize);
  LOGMAGIC(ctmp);
#endif

  // take out a lock.  giving ourselves a few chances as needed
  //
  bool gotlock=false;
  for(int i=0;( i<5) && !gotlock;i++) { gotlock=lock(); }
  if(!gotlock) {
    LOGMAGIC("Error: uploadBegin failed to acquire lock.\r\n");
    _uploadSuccess=false;
    _uploadInprogress=false;
    return false;
  }
  //
  // Initialize upload state
  //
  _uploadInprogress = true;
  _uploadSuccess = true;
  _uploadSz = 0;
  //
  // Send M28 to create file and start sending to it
  //
  sprintf(ctmp,"M28 %s\r\n",filename);
  const char *result=doGcodeMessage(ctmp,strlen(ctmp),IPAddress(0,0,0,0),0,ESP3DClientType::no_client);
  if(!result) 
  {
    LOGMAGIC(ctmp);
    LOGMAGIC("ERROR - NULL response to M28\r\n");
    _uploadInprogress=false;
    _uploadSuccess=false;
    // sanity... probably wont do anything but just in case
    sprintf(ctmp,"M29\r\n");
    doGcodeMessage(ctmp,strlen(ctmp),IPAddress(0,0,0,0),0,ESP3DClientType::no_client);
    return false;
  }

//#ifdef SUPER_CHATTY
  LOGMAGIC(ctmp);
  LOGMAGIC(result);
//#endif
  return true;
}

//
// used by http upload mode... a payload with an offset.  should be either
// 2048 bytes or less for the final packet
//
bool ChituService::uploadMiddle(const char *buf, size_t offset, size_t len) {
  char ctmp[128];
  sprintf(ctmp,"uploadMiddle offset=%d len=%d\r\n",offset,len);
  LOGMAGIC(ctmp);
  return true;
}

//
// used by http upload mode... Upload is complete and we need to report whether
// or not it was successful.  Also.  the incoming boolean indicates if we should
// start the print of the newly uploaded file.
//
bool ChituService::uploadEnd(bool printit) {
  char ctmp[128];

  // send M29 to stop recording to file
  //
  sprintf(ctmp,"M29\r\n");
  const char *result=doGcodeMessage(ctmp,strlen(ctmp),IPAddress(0,0,0,0),0,ESP3DClientType::no_client);
  if(!result) {
    LOGMAGIC("NULL response to M29");
    _uploadSuccess=false;
    _uploadInprogress=false;
    return false;
  }
//#ifdef SUPER_CHATTY
  LOGMAGIC(ctmp);
  LOGMAGIC(result);
//#endif

  if(printit) {
    LOGMAGIC("uploadEnd print\r\n");
  } else {
    LOGMAGIC("uploadEnd noprint\r\n");
  }
  unlock();
  return _uploadSuccess;
}

//
// Waits up to MAX_SERIAL_WAIT for a full line from Chitu and returns it in the provided buffer.  
// returns bytes read or zero in case of timeout or overflow.
// NOTE:
//   * response ends with \n
//
int ChituService::pullChituLine(char *obuf, int max) {
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
      uint32_t now=millis();

      resetLockTimeout(now);  // we might be pulling lines for a while. Reset the lock timeout as needed

      if((now-t1)>MAX_SERIAL_WAIT) {
        LOGMAGIC("ERROR - CHITU SERIAL GCODE ... NO RESPONSE COMPLETION IN MAX_SERIAL_WAIT\r\n");
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
#ifdef SUPER_CHATTY
  char ctmp[32];
  sprintf(ctmp,"CHITU OUTPUT (%d)\r\n",olen);
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
void ChituService::sendResponseHome(const char *buf, int len, IPAddress udpIP, int udpPort, ESP3DClientType toType) {
  if(udpPort) {
    //
    // datagram GCode.  Send out as a datagram
    //
#ifdef SUPER_CHATTY
    LOGMAGIC("CHITU RESPONSE TO UDP GUEST\r\n");
    LOGMAGIC(buf,len);
#endif
    _udp.beginPacket(udpIP,udpPort);
    _udp.write(buf,len);
    _udp.endPacket();
  } else if(toType!=ESP3DClientType::no_client) {
    //
    // Send the payload where it belongs (based on ESP3DSerialService::flushData for now)
    //
    ESP3DMessage *message=esp3d_message_manager.newMsg(ESP3DClientType::chitu_serial,toType,(uint8_t*)buf,len,ESP3DAuthenticationLevel::admin);
    if(message) {
      message->type=ESP3DMessageType::unique;
      esp3d_commands.process(message);
#ifdef SUPER_CHATTY
      char ctmp[128];
      sprintf(ctmp,"PROCESSED OUTGOING PAYLOAD TO dst=%d: ",toType);
      LOGMAGIC(ctmp);
      LOGMAGIC(buf,len);
      LOGMAGIC("\r\n");
#endif
    } else {
      LOGMAGIC("COULD NOT CREATE ESP3D MESSAGE FROM PAYLOAD: ");
      LOGMAGIC(buf,len);
      LOGMAGIC("\r\n");
    }
  } else {
    // response should be handled via return path not via messages.
//#ifdef SUPER_CHATTY
    LOGMAGIC("NO DESTINATION TO SEND RESPONSE HOME TO.\r\n");
    LOGMAGIC(buf,len);
//#endif
  }
}

//
// Handle a Gcode request to the printer giving reasonable change for the printer to respond
// returns pointer to the last response from gcode
//
const char *ChituService::doGcodeMessage(const char *msg, size_t len, IPAddress udpIP, int udpPort, ESP3DClientType toType) {
  char ctmp[128];
  char *paystart=0;
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
  sprintf(ctmp,"\r\n+IPD,4,%d:",len);
  esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp)); // header
  esp3d_serial_service.writeBytes((const uint8_t*)msg, len);           // payload
  sprintf(ctmp,"\r\nOK,recv\r\n",len,msg);
  esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp)); // trailer
  esp3d_serial_service.flush();

  //
  // We expect CIPSEND(s) from Chitu with last one starting with ok.
  // CIPSEND ... Chitu IP Send?
  // AT+CIPSEND=4,<SIZE>\r<PAYLOAD>
  // incoming message length should equal the size plus the header bit up to the \r
  // NOTEs:
  //    All found Chitu responses end with a CIPSEND line that starts with "ok"
  //    At least 1 CIPSEND response includes a \r\n in the payload (M29 response) so have to handle multiline payloads
  //
  //
  while(!okfound) {
    int offset=0;
    for(bool completeCIP=false;!completeCIP;) {
      olen=ChituService::pullChituLine(&_gcode_rbuf[offset], 255-offset);
      if(offset) {
#ifdef SUPER_CHATTY
        sprintf(ctmp,"assemble multiline cipsend progress... offset=%d + olen=%d = %d\r\n--",offset,olen,offset+olen);
        LOGMAGIC(ctmp);
        LOGMAGIC(_gcode_rbuf,olen+offset);
#endif
        olen+=offset;  // if this is a line with multiple \r\n from chitu, assemble them
      }
      //
      // Verify we got a cipsend or bail.
      //
      if(_gcode_rbuf!=strstr(_gcode_rbuf,"AT+CIPSEND=4,")) {
        sprintf(ctmp,"ERROR - CHITU GCODE RESPONSE NOT CIPSEND len=%d olen=%d\r\n--",len,olen);
        LOGMAGIC(ctmp);
        LOGMAGIC(msg,len);
        LOGMAGIC("--\r\n--");
        LOGMAGIC(_gcode_rbuf,olen);
        LOGMAGIC("--\r\n");
        return 0;
      }
      // extract the payload length
      int paylen=atoi(&_gcode_rbuf[13]);
      // determine the payload start
      paystart=strchr(_gcode_rbuf,'\r');
      if(paystart) { paystart++; }
      // Sanity check log messages
      if(!paystart) { 
        LOGMAGIC("ERROR - CHITU CANNOT DETECT PAYLOAD START\r\n"); 
        LOGMAGIC(msg,len);
        LOGMAGIC("\r\n");
        LOGMAGIC(_gcode_rbuf,olen);
        LOGMAGIC("\r\n");
        return 0;
      }
      int expected=paylen+int(paystart-_gcode_rbuf);
      if(expected==olen) { 
	completeCIP=true; 
      } else if(expected>olen) {
#ifdef SUPER_CHATTY
        sprintf(ctmp,"WARN insufficient CIPSEND bytes (%d).  pull another line.\r\n--",olen);
        LOGMAGIC(ctmp);
        LOGMAGIC("--\r\n--");
        LOGMAGIC(_gcode_rbuf);
        LOGMAGIC("--\r\n");
#endif
        offset=olen;
        completeCIP=false;
      } else {
        sprintf(ctmp,"ERROR UNEXPECTED LENGTH msg_len=%d expected=%d (payload_len=%d header_len=%d)\r\n",len,expected,paylen,int(paystart-_gcode_rbuf));
        LOGMAGIC(ctmp);
        LOGMAGIC(msg,len);
        LOGMAGIC("\r\n");
        LOGMAGIC(_gcode_rbuf,olen);
        return 0;
      }
      if(completeCIP) {
        sendResponseHome(paystart, paylen, udpIP, udpPort, toType);
        sprintf(ctmp,"OK,SEND DONE\r\n");
        esp3d_serial_service.writeBytes((const uint8_t*)ctmp, strlen(ctmp));
        esp3d_serial_service.flush();
        //
        // Is this the last expected line in this series?
        //
        if(paylen>2 && paystart[0]=='o' && paystart[1]=='k') { okfound=true; }
      }
    }
  }
  return paystart;
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
void ChituService::doDatagram(char*buf, int sz, IPAddress udpIP, int udpPort) {
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
    //_udp.beginPacket(udpIP,udpPort);
    //_udp.write(ctmp);
    //_udp.endPacket();
    sendResponseHome(ctmp, strlen(ctmp), udpIP, udpPort, ESP3DClientType::no_client);
  } else {
    doGcodeMessage(buf,sz,udpIP,udpPort,ESP3DClientType::no_client);
  }
}

//
// reset the lock forward progress timestamp if want to hold for possibly longer time
// NOTE: reader of _lockTS is not protected by noInterrupts.  I believe this is okay
// on a single core ESP8266.
//
void ChituService::resetLockTimeout(uint32_t t) {
  noInterrupts();
  _lockTs=t;
  interrupts();
}

bool ChituService::lock() {
  bool success;
  uint32_t now=millis();

  //
  // Handle lock that has been locked more than designated time
  //
  if(_locked && ((now-_lockTs) > MAXLOCK)) {
    noInterrupts();
    //
    // repetitive check but this time inside of a noInterrupts block to be pedantic
    // intentionally unpaired unlock
    //
    if(_locked && ((now-_lockTs) > MAXLOCK)) { unlock(); }
    interrupts();
    LOGMAGIC("WARN - FORCE UNLOCK HUNG LOCK\r\n");
  }
  //
  // Proceed with new locking path
  //
  if(!_locked) {       // if not locked try to take out a lock
    noInterrupts();
    // repetitive but this time inside of a noInterrupts context
    if(!_locked) {     // we are the locker
      success=true; 
      _locked=true; 
      _lockTs=now;
    } else {
      success=false;  // someone else locked before us
    }
    interrupts();
  } else {             // already locked
    success=false; 
  }
  return success;
}

bool ChituService::unlock() {
  bool success;
  if(_locked) {        // if locked try to release it
    noInterrupts();
    // repetitive but this time inside of a noInterrupts context
    if(_locked) {      // we are the unlocker
      success=true; 
      _locked=false; 
      _uploadInprogress = false;  // side effect will kill any upload in process
      _lockTs=0;
    } else {           // someone else unlocked before us
      success=false;
    }
    interrupts();
  } else {
    success=false;     // already unlocked
  }
  return success;
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
       //
       // If we cannot enter locked state skip this datagram cycle
       //
       if(!lock()) {
         LOGMAGIC("ChituService::handle fail to acquire lock.\r\n");
         return;
       }
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
       //
       // release the lock
       //
       unlock();
     }
  }
}

void ChituService::end() { 
  _started = false; 
  _udp.stop();
  _inDatagram=false;
  unlock();   // Intentionally unpaired unlock.
}

#endif  // COMMUNICATION_PROTOCOL == CHITU_SERIAL
