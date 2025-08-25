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
uint8_t MKSEMU::bufT2S[256];
size_t  MKSEMU::bufT2Ssz=0;
size_t  MKSEMU::T2Sct=0;
int     MKSEMU::theOp=-1;

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
}

//read buffer as char
void MKSEMU::read_buffer_tcp (uint8_t *bytes, size_t ct)
{
  int i;
  //
  // Have not yet determined operation type (based on initial line starting with "POST /upload?")
  //
  if(MKSEMU::theOp==-1) {
    bool sufficient=false;
    for(i=0;i<ct;i++) {
      if((MKSEMU::bufT2Ssz+i)>200) { ESPCOM::send2mksTCP(NULL,0,true); return; } // error out if <CR> not found in 200 bytes
      if(!sufficient) { MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i]=bytes[i]; }           // save off bytes
      //
      // <cr> found.  we have enough info
      //
      if(bytes[i]=='\r') { 
	MKSEMU::bufT2S[MKSEMU::bufT2Ssz+i]=0;  // null it out
	sufficient=true; 
        MKSEMU::bufT2Ssz+=i;
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
      if(strstr((const char*)MKSEMU::bufT2S,"POST /upload?")==(const char*)MKSEMU::bufT2S) {
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
    char ctmp[128];
    sprintf(ctmp,":%d",wifi_config.iweb_port);
    ESPCOM::send2mksTCP(ctmp, false);
    //
    // find the URI being requested for the redirection.  e.g., /index.html in GET /index.htm HTTP/1.1
    //
    // skip to first space
    for(i=0;MKSEMU::bufT2S[i] && MKSEMU::bufT2S[i]!=' ';i++) { /* intentional */ }
    if(MKSEMU::bufT2S[i]==' ') {
      // find the HTTP/1...
      char *p=strstr((char*)&MKSEMU::bufT2S[i+1]," HTTP/");
      if(p) { 
	// post the URI
	*p=0; 
        ESPCOM::send2mksTCP((const char*)&MKSEMU::bufT2S[i+1], false);
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
  //ESPCOM::send2mksTCP("HANDLE", false);
  ESPCOM::logMagic((const char*)bytes, ct, false);
  //ESPCOM::send2mksTCP((const char*)MKSEMU::bufT2S, false);
  
    //ESPCOM::send2mksTCP(buf, false);
  //ESPCOM::send2mksTCP((const char*)bufT2S, bufT2Ssz, false);
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
