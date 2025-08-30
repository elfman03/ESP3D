/*
  mksemu.cpp - ESP3D MKS Upload emulation class
    based on command.cpp

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
#include "config.h"
#include "mksemu.h"
#include "wificonf.h"
#include "webinterface.h"
#ifndef FS_NO_GLOBALS
#define FS_NO_GLOBALS
#endif

bool can_accept_mksemu_packets=true;
String MKSEMU::buffer_serial;
String MKSEMU::buffer_tcp;
char   MKSEMU::filename[64];
uint8_t MKSEMU::bufT2S[256];
size_t  MKSEMU::bufT2Ssz=0;
size_t  MKSEMU::T2Sct=0;
int     MKSEMU::theOp=-1;
int     MKSEMU::payloadSz=-1;
int     MKSEMU::payloadOffset=-1;
int     MKSEMU::timeoutCt=0;

extern size_t wait_for_data(uint32_t timeout);
extern bool purge_serial();

#ifdef MKS_UPLOAD_M28EMU
void MKSEMU::tcp_connection_reset () {
  //
  // If first payload in a session we reset the session related variables
  //
  MKSEMU::bufT2Ssz=0;            // current size of buffer to send to serial
  MKSEMU::T2Sct=0;               // ???
  MKSEMU::theOp=-1;              // state engine to determine if this is an upload
  MKSEMU::filename[0]=0;         // filename of in process upload
  MKSEMU::payloadSz=-1;          // the size of the in process upload
  MKSEMU::payloadOffset=-1;      // the offset into the current upload
}

//
// Pull from incoming bytes buffer to flesh out a full line
//
// bytes  -- the inbound byte buffer
// ct     -- size of the inbound byte buffer
// offset -- starting offset into the inbound byte buffer
// expands MKSEMU::bufT2S starting from MKSEMU::bufT2Ssz
// returns new offset into the inbound byte buffer for next time
//
int linefill(uint8_t *bytes, size_t ct, size_t offset) {
  int i;
  for(i=offset;i<ct;i++) {
    //
    // copy that byte
    //
    MKSEMU::bufT2S[MKSEMU::bufT2Ssz]=bytes[i];
    MKSEMU::bufT2Ssz++;
    //
    // check for line max and error out if we hit it
    //
    if(MKSEMU::bufT2Ssz>240) {
      return -1;
    }
    //if(bytes[i]=='\r') { 
    //  MKSEMU::bufT2S[MKSEMU::bufT2Ssz-1]=' ';  // whitespace out <cr>
    //}
    //
    // <nl> found.  yay!
    //
    if(bytes[i]=='\n') {
      MKSEMU::bufT2S[MKSEMU::bufT2Ssz]=0;  // null terminate after <nl>
      return i+1;
    }
  }
  //
  // We hit the end of the buffer without a newline.  maintain residual for the next payload.
  //
  return ct;
}

long fetch_serial_response(char *buf, int targetCt, int maxCt, int timeout) {
  uint32_t endmilli = timeout+millis();
  if(buf) { buf[0]=0; }  // clear buf
  //
  // wait for enough bytes or for a timeout
  //
  int avail=ESPCOM::available(DEFAULT_PRINTER_PIPE);
  for (;avail<targetCt && (millis()<endmilli);) {
    CONFIG::wait(10);
    avail=ESPCOM::available(DEFAULT_PRINTER_PIPE);
  }
  if(!buf) { return avail; }               // if buf is null return how many we could have read
  //
  // read and return byte count read
  //
  int readCt=avail<maxCt?avail:maxCt;
  readCt=ESPCOM::readBytes(DEFAULT_PRINTER_PIPE,(unsigned char*)buf,readCt);
  return readCt;
}

void uploadStart(char *filename) {
  char ctmp[128];
  char sbuf[128];

  web_interface->blockserial=true;
  purge_serial();
  sprintf(ctmp,"M28 %s\r\n",filename);
  ESPCOM::write(DEFAULT_PRINTER_PIPE,ctmp);
  ESPCOM::flush(DEFAULT_PRINTER_PIPE,NULL);
  MKSEMU::timeoutCt=0;
  int rdct=fetch_serial_response(sbuf,100,127,1000);
  sprintf(ctmp,"M28 %s -- rdct=%d -- ",filename,rdct);
  ESPCOM::logMagic(ctmp, false);
  if(rdct>0) { ESPCOM::logMagic(sbuf, rdct, false); }
  ESPCOM::logMagic("\n", false);
}

void uploadElement(unsigned char *line, int len) {
  char ctmp[128];
  char sbuf[128];
  line[0]=';';

  // Chitu M28 checksum protocol 
  // https://github.com/Photonsters/PhotonNetworkController/blob/ca5549a4564e5bc8a19b7db638e8373e4aedc16b/frmMain.cs#L441
  //
  // insert offset
  unsigned int off=MKSEMU::payloadOffset;
  line[len]=off & 0x0ff;
  off=off>>8;
  line[len+1]=off & 0x0ff;
  off=off>>8;
  line[len+2]=off & 0x0ff;
  off=off>>8;
  line[len+3]=off;
  // insert checksum
  unsigned char sum=0;
  for(int i=0;i<(len+4);i++) { sum=sum^line[i]; }
  line[len+4]=sum;
  // insert trailer
  line[len+5]=0x83;
  sprintf(ctmp,"beg: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x len=%d\n",line[0],line[1],line[2],line[3],line[4],line[5],len);
  ESPCOM::logMagic(ctmp, false);
  sprintf(ctmp,"end: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",line[len],line[len+1],line[len+2],line[len+3],line[len+4],line[len+5]);
  ESPCOM::logMagic(ctmp, false);

  ESPCOM::write(DEFAULT_PRINTER_PIPE,(unsigned char*)line,len+6);
  ESPCOM::flush(DEFAULT_PRINTER_PIPE,NULL);

  int rdct=fetch_serial_response(sbuf,6,127,1000);
  if(rdct!=6000) {
    sprintf(ctmp,"SER %d (from %d) - ",rdct,len);
    ESPCOM::logMagic(ctmp, false);
    if(rdct>0) { ESPCOM::logMagic(sbuf, rdct,false); }
    ESPCOM::logMagic("\n", false);
  }

  MKSEMU::payloadOffset+=len;
  MKSEMU::timeoutCt=0;
  //sprintf(ctmp,"payload processed %d/%d ready=%d\n",MKSEMU::payloadOffset,MKSEMU::payloadSz,ready);
  //ESPCOM::logMagic(ctmp, false);
}

void uploadEnd() {
  char ctmp[128];
  char sbuf[128];

  purge_serial();
  sprintf(ctmp,"M29\r\n");
  ESPCOM::write(DEFAULT_PRINTER_PIPE,ctmp);
  ESPCOM::flush(DEFAULT_PRINTER_PIPE,NULL);
  MKSEMU::timeoutCt=0;
  int rdct=fetch_serial_response(sbuf,8,127,2000);
  MKSEMU::payloadOffset=-1;
  sprintf(ctmp,"M29 -- rdct=%d -- ",rdct);
  ESPCOM::logMagic(ctmp, false);
  ESPCOM::logMagic(sbuf, rdct, false);
  ESPCOM::logMagic("\n", false);
  if(rdct>0) { ESPCOM::logMagic(sbuf, rdct, false); }
  web_interface->blockserial=false;
}

// We have a full line
// 1. find the payload size
// 2. find the payload
// 3. handle the payload
// 4. detect end of payload
void handleLine(char *line, int len) {
  char ctmp[128];
  //
  // if payload size is unknown, detect and handle Content-Length tag
  //
  if(MKSEMU::payloadSz==-1) {
    if(strstr(line,"Content-Length: ")==line) {
      sscanf(line,"Content-Length: %d",&MKSEMU::payloadSz);
      sprintf(ctmp,"PAYLOAD SIZE: %d\n",MKSEMU::payloadSz);
      ESPCOM::logMagic(ctmp, false);
    }
    return;
  }
  //
  // if payload size is known we can look for the end of headers and thus start of payload
  //
  if(MKSEMU::payloadOffset==-1) {
    if(strstr(line,"\r\n")==line) {
      MKSEMU::payloadOffset=0;
      ESPCOM::logMagic("PAYLOAD START DETECTED\n", false);
      uploadStart(MKSEMU::filename);
    }
    return;
  }
  //
  // We are in the payload.  Lets count up and send some serial!
  //
  uploadElement((uint8_t *)line,len);

  //
  // handle end of payload condition
  //
  if(MKSEMU::payloadOffset>=MKSEMU::payloadSz) {
    sprintf(ctmp,"DONE!  payload processed %d/%d\n",MKSEMU::payloadOffset,MKSEMU::payloadSz);
    ESPCOM::logMagic(ctmp, false);
    uploadEnd();
    ESPCOM::send2mksTCP("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"err\": \"0\"}\r\n", true);
  }
}

//read buffer as char
void MKSEMU::read_buffer_tcp (uint8_t *bytes, size_t ct)
{
  char ctmp[128];
  int i,space,inputOffset;
  inputOffset=0;
  //
  // Have not yet determined operation type (based on initial line starting with "POST /upload?")
  //
  if(MKSEMU::theOp==-1) {
    bool sufficient=false;
    for(i=0;i<ct;i++) {
      if((MKSEMU::bufT2Ssz+i)>200) { ESPCOM::send2mksTCP(NULL,0,true); return; } // error out if <CR> not found in 200 bytes
      if(!sufficient) { MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i]=bytes[i]; }           // save off bytes
      if(bytes[i]=='\r') { 
	MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i]=' ';  // whitespace out <cr>
      }
      //
      // <nl> found.  we have enough info
      //
      if(bytes[i]=='\n') { 
	MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i+1]=0;  // null terminate
	sufficient=true; 
        MKSEMU::bufT2Ssz+=i;
	inputOffset=i+1;
	i=ct; 
      }
    }
    //
    // i==ct means consumed the whole buffer without finding <cr>.  wait for next payload
    // i=ct+1 means we have a full target
    //
    if(i==ct) {
      MKSEMU::bufT2Ssz+=ct;
    }
    if(sufficient) {
      if(strstr((const char*)MKSEMU::bufT2S,"POST /upload?X-Filename=")==(const char*)MKSEMU::bufT2S) {
	MKSEMU::theOp=1;
      } else {
	MKSEMU::theOp=0;
      }
    }
    // short circuit if not enough bytes yet to tell payload
    if(MKSEMU::theOp==-1) { return; }   
  }
  
  //
  // Operation is NOT an MKS upload.  Reroute user to the real web interface
  //
  if(MKSEMU::theOp==0) {
    // permanently moved header with our IP and port
    ESPCOM::send2mksTCP("HTTP/1.1 301 Moved Permanently\r\nLocation: http://", false);
    ESPCOM::send2mksTCP(WiFi.localIP().toString(), false);
    sprintf(ctmp,":%d",wifi_config.iweb_port);
    ESPCOM::send2mksTCP(ctmp, false);
    //
    // find the URI being requested for the redirection.  e.g., /index.html in GET /index.htm HTTP/1.1
    //
    // skip to first space (i.e., after GET/PUT/HEAD
    for(space=0;MKSEMU::bufT2S[space] && MKSEMU::bufT2S[space]!=' ';) { space++; }
    if(MKSEMU::bufT2S[space]==' ') {
      // find the HTTP/1...
      char *p=strstr((char*)&MKSEMU::bufT2S[space+1]," HTTP/");
      if(p) { 
	// post the URI
	*p=0; 
        ESPCOM::send2mksTCP((const char*)&MKSEMU::bufT2S[space+1], false);
	*p=' '; 
      }
    }
    // finish and hangup
    ESPCOM::send2mksTCP("\r\nConnection: close\r\n\r\n<body>moved</body>\r\n\r\n--", false);
    // DEBUGGING
    //ESPCOM::send2mksTCP((const char*)bytes, ct, false);
    //sprintf(ctmp,"--%d\r\n--",ct);
    //ESPCOM::send2mksTCP(ctmp, false);
    //ESPCOM::send2mksTCP((const char *)MKSEMU::bufT2S, false);
    //sprintf(ctmp,"--%d\r\n",MKSEMU::bufT2Ssz);
    //ESPCOM::send2mksTCP(ctmp, true);
    return;
  }

  //
  // Operation IS an MKS upload.  Lets do some magic
  //

  //
  // First line! Extract filename from "POST /upload?X-Filename=xxxxxxxxx HTTP/1.1" 
  //
  if(!MKSEMU::filename[0]) {
    // Extract filename which starts at character 24 and should end with a space before end of line
    for(space=0;MKSEMU::bufT2S[24+space] && MKSEMU::bufT2S[24+space]!=' ';) { space++; }
    // if the length is zero error out and close connection
    if(!MKSEMU::bufT2S[24+space]) { ESPCOM::send2mksTCP("Filename Syntax Incorrect",true); return; }
    // make a copy of the filename
    for(i=0;i<space;i++) {
      MKSEMU::filename[i]=MKSEMU::bufT2S[24+i];
    }
    MKSEMU::filename[i]=0;

    sprintf(ctmp,"FILENAME: %s\n",MKSEMU::filename);
    ESPCOM::logMagic(ctmp, false);
    MKSEMU::bufT2Ssz=0; // done with this line
  }

  //ESPCOM::logMagic((const char*)bytes, ct, false);

  //
  // At this point we have a filename and we can process lines
  // 1. find the payload size
  // 2. find the payload
  // 3. handle the payload
  // 4. detect end of payload
  //

  can_accept_mksemu_packets=false;      // cannot accept more while processing this one
  for(;inputOffset<ct;) {
    inputOffset=linefill(bytes, ct, inputOffset);
    //
    // Is there a complete packet to handle?  Should have a null at last field.
    //
    if(!MKSEMU::bufT2S[MKSEMU::bufT2Ssz]) {
      sprintf(ctmp,"LINE: %d/%d len=%d: ",inputOffset,ct,MKSEMU::bufT2Ssz);
      ESPCOM::logMagic(ctmp, false);
      ESPCOM::logMagic((const char*)MKSEMU::bufT2S, false);
      handleLine((char*)MKSEMU::bufT2S, MKSEMU::bufT2Ssz);
      MKSEMU::bufT2Ssz=0; // done with this line
    } else {
      //sprintf(ctmp,"Partial: %d/%d len=%d ... ",inputOffset,ct,MKSEMU::bufT2Ssz);
      //ESPCOM::logMagic(ctmp, false);
    }
  }
  can_accept_mksemu_packets=true;  // ready for more pain
}
void MKSEMU::read_buffer_serial (uint8_t *b, size_t len) {
  char ctmp[128];

  if(MKSEMU::payloadOffset==-1) { return; }
  sprintf(ctmp,"SERIAL %d: ",len);
  ESPCOM::logMagic(ctmp, false);
  ESPCOM::logMagic((const char*)b, len, false);
  ESPCOM::logMagic("\n", false);
  if(len==4 && strstr((char*)b,"ok\r\n")) {
    MKSEMU::timeoutCt++;
    if(MKSEMU::timeoutCt>3) {
      ESPCOM::send2mksTCP("Timeout",true); 
      uploadEnd();
      ESPCOM::logMagic("TIMEOUT!!!  Trying to back out...\n", false);
    }
  }
}
#endif
