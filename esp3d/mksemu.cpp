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

String MKSEMU::buffer_serial;
String MKSEMU::buffer_tcp;
char   MKSEMU::filename[64];
uint8_t MKSEMU::bufT2S[256];
size_t  MKSEMU::bufT2Ssz=0;
size_t  MKSEMU::T2Sct=0;
int     MKSEMU::theOp=-1;
int     MKSEMU::payloadSz=-1;
int     MKSEMU::payloadOffset=-1;

//extern bool sendLine2Serial (String &  line, int32_t linenb, int32_t* newlinenb);

/*
//read a buffer in an array
void COMMAND::read_buffer_serial (uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        read_buffer_serial (b[i]);
        //*b++;
    }
}
*/

#ifdef MKS_UPLOAD_M28EMU
void MKSEMU::tcp_connection_reset () {
  //
  // If first payload in a session we have to determine session type and reset parameters
  //
  MKSEMU::bufT2Ssz=0; 
  MKSEMU::T2Sct=0; 
  MKSEMU::theOp=-1;
  MKSEMU::filename[0]=0;
  MKSEMU::payloadSz=-1;
  MKSEMU::payloadOffset=-1;
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
    if(MKSEMU::bufT2Ssz>250) {
      return -1;
    }
    if(bytes[i]=='\r') { 
      MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i]=' ';  // whitespace out <cr>
    }
    //
    // <nl> found.  yay!
    //
    if(bytes[i]=='\n') {
      MKSEMU::bufT2S[MKSEMU::bufT2Ssz]=0;  // null terminate
      return i+1;
    }
  }
  //
  // We hit the end of the buffer without a newline.  maintain residual for the next payload.
  //
  return ct;
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

    sprintf(ctmp,"\nFILENAME: --%s--\n",MKSEMU::filename);
    ESPCOM::logMagic(ctmp, false);
    MKSEMU::bufT2Ssz=0;
  }

  //ESPCOM::logMagic((const char*)bytes, ct, false);

  //
  // At this point we have a filename but still need to extract the payload size.
  //
  for(;inputOffset<ct;) {
    inputOffset=linefill(bytes, ct, inputOffset);
    //
    // Is there a complete packet to handle?  Should have a null at last field.
    //
    if(!MKSEMU::bufT2S[MKSEMU::bufT2Ssz]) {
      sprintf(ctmp,"LINE: %d/%d len=%d: ",inputOffset,ct,MKSEMU::bufT2Ssz);
      ESPCOM::logMagic(ctmp, false);
      ESPCOM::logMagic((const char*)MKSEMU::bufT2S, false);
      MKSEMU::bufT2Ssz=0;
    } else {
      sprintf(ctmp,"Partial: %d/%d len=%d ... ",inputOffset,ct,MKSEMU::bufT2Ssz);
      ESPCOM::logMagic(ctmp, false);
      //ESPCOM::logMagic((const char*)MKSEMU::bufT2S, MKSEMU::bufT2Ssz, false);
      //ESPCOM::logMagic("\n", false);
    }
  }
}
#endif
/*
//read buffer as char
void COMMAND::read_buffer_serial (uint8_t b)
{
    static bool previous_was_char = false;
    static bool iscomment = false;
//to ensure it is continuous string, no char separated by binaries
    if (!previous_was_char) {
        buffer_serial = "";
        iscomment = false;
    }
//is comment ?
    if (char (b) == ';') {
        iscomment = true;
    }
//it is a char so add it to buffer
    if (isPrintable (b) ) {
        previous_was_char = true;
        if (!iscomment) {
            buffer_serial += char (b);
        }
    } else {
        previous_was_char = false; //next call will reset the buffer
    }
//this is not printable but end of command check if need to handle it
    if (b == 13 || b == 10) {
        //reset comment flag
        iscomment = false;
        //Minimum is something like M10 so 3 char
        if (buffer_serial.length() > 3) {
            check_command (buffer_serial, DEFAULT_PRINTER_PIPE);
        }
    }
}
*/
