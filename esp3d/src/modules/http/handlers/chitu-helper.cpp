/*
 handle-chitu-files.cpp - ESP3D http handle
    based on handle-mks-files.cpp
    restructure based on https://tttapa.github.io/ESP8266/Chap12%20-%20Uploading%20to%20Server.html

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
#include "../../chitu/chitu_service.h"
#include "../../logmagic/logmagic_server.h"

// Pretend to be astrobox to support url api for uploading.  I wanted to use MKS form instead of astrobox but they do not 
// use http form POST uploads and do not work with esp8266webserver upload mode.  Astrobox uses forms and thus works with 
// esp8266webserver.  This is enough to be accepted by Prusa Slicer as an Astrobox for the purposes of uploading prints.
// 
#define ASTROBOX_VERSION "{\"api\": \"fake\", \"text\": \"AstroBox fake\"}"

void HTTP_Server::chituFakeAstrobox() {
  _webserver->send(200, "application/json", ASTROBOX_VERSION);
  //char ctmp[128];
  //sprintf(ctmp,"Impersonate Astrobox: %s\r\n",ASTROBOX_VERSION);
  //LOGMAGIC(ctmp);
}

// Upload complete.  Send response based on result
// As the Webserver class processes form fragments, it builds up the list of arguments.
// Those arguments are then available to read at the end of the operation which is here.
// Astrobox seems to populate a field labelled "print" to indicate if the result should
// be printed after the upload completes.
// 
void HTTP_Server::chituUploadFN() {
  bool success;
  char ctmp[128];
  HTTPUpload& upload = _webserver->upload();

  const bool print=!strcmp(_webserver->arg("print").c_str(),"true");

  size_t startts=ChituService::uploadStartTime();

  // finalize the upload and initiate print if desires
  //
  success=ChituService::uploadEnd(upload.filename.c_str(),print);

  // respond to the http request with success or failure.
  //
  if(success) {
    uint32_t duration=millis()-startts;
    sprintf(ctmp,"Upload '%s' complete with print=%s.  %d bytes in %dms (%0.2f KB/s)\r\n",
                 upload.filename.c_str(),print?"true":"false",upload.totalSize,duration,
                 (upload.totalSize/1024.0)/(duration/1000.0));
    LOGMAGIC(ctmp);
    _webserver->send(200, "text/plain", ctmp);
  } else {
    sprintf(ctmp,"Upload Failure '%s'.\r\n", upload.filename.c_str());
    LOGMAGIC(ctmp);
    _webserver->send(422, "text/plain", ctmp);
  }
}

// Upload Incremental callback
//
void HTTP_Server::chituUploadUFN() {
  //
  // get upload structure
  //
  HTTPUpload& upload = _webserver->upload();
  if(upload.status == UPLOAD_FILE_START) {
    //
    // file upload is starting
    //
    char ftmp[128];
    size_t fileSize=0;
    sprintf(ftmp,"s%s",upload.filename.c_str());
    if (_webserver->hasArg(ftmp)) {
      fileSize = _webserver->arg(ftmp).toInt();
    } else if (_webserver->hasHeader("Content-Length")) {
      fileSize = _webserver->header("Content-Length").toInt();
    }
    ChituService::uploadBegin(upload.filename.c_str(),fileSize);
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    //
    // Based on webserver api, file payload is 2048 bytes or less for the last payload
    // Write logic that would generalize to longer if needed.
    // Send it to Chitu in 1K chunks.  We experimentally determined that 2K results
    // in errors.  Chitu seems to be designed to accept datagram sized chunks that would
    // be less than about 1300 bytes.
    //
    for(size_t offset=0;offset<upload.currentSize;offset+=1024) {
      ChituService::uploadMiddle((const char *)&upload.buf[offset], offset+upload.totalSize,
		                 (upload.currentSize-offset>1024)?1024:upload.currentSize-offset);
    }
  } else if(upload.status == UPLOAD_FILE_END) {
    //
    // file upload ending.  we would have called the chitu end from here but
    // we have to wait until the final callback in order to see the arguments like
    // whether I should print or not.  So we just NOP at this point.
    //
  } else if(upload.status == UPLOAD_FILE_ABORTED) {
    ChituService::uploadAbort();
  } else {
    LOGMAGIC("ERROR: UPLOAD_FILE UNKNOWN STATE\r\n");
  }
}
#endif  // HTTP_FEATURE && (COMMUNICATION_PROTOCOL == CHITU_SERIAL)
