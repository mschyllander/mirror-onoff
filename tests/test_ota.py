"""Exercise the sketch's upload handlers with mocked HTTP, flash and power APIs."""
from pathlib import Path
import re, subprocess, tempfile, os
root = Path(__file__).resolve().parents[1]
source = (root/'firmware/esp_12V_mirror_timer/esp_12V_mirror_timer.ino').read_text(encoding='utf-8')
functions=[]
for name in ['authenticateAdmin','validAdminRequest','handleFirmwareUpload','finishFirmwareUpload']:
    m=re.search(r'(?:void|bool) '+name+r'\([^)]*\)\s*\{',source)
    at, depth=m.end(),1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    functions.append(source[m.start():at])
stub=r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <iostream>
struct String : std::string {
 using std::string::string;
 bool endsWith(const char* suffix) const {std::string s(suffix);return size()>=s.size()&&compare(size()-s.size(),s.size(),s)==0;}
};
enum {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED,DIGEST_AUTH,U_FLASH};
struct HTTPUpload {int status=UPLOAD_FILE_START;String name="firmware",filename="mirror.bin";uint8_t buf[16]={};size_t currentSize=16;} upload;
struct Server {
 bool authenticated=true,tokenOK=true;int code=0;
 bool authenticate(const char*,const char*) {return authenticated;}
 void requestAuthentication(int,const char*) {code=401;}
 bool hasArg(const char*) {return true;}
 String arg(const char*) {return tokenOK?"nonce":"wrong";}
 HTTPUpload& upload() {return ::upload;}
 void send(int c,const char*,const char*) {code=c;}
} server;
struct Updater {
 bool running=false,beginOK=true,writeOK=true,endOK=true;int begins=0,writes=0,ends=0;
 bool begin(uint32_t,int) {++begins;return running=beginOK;}
 size_t write(uint8_t*,size_t n) {++writes;return writeOK?n:0;}
 bool end(bool success=false) {++ends;running=false;return success&&endOK;}
 bool isRunning(){return running;}
} Update;
struct Esp {bool restarted=false;uint32_t getFreeSketchSpace(){return 1048576;}void restart(){restarted=true;}} ESP;
bool otaInProgress=false,otaSucceeded=false,otaFailed=false,otaAuthorized=false,powerOn=true;
uint32_t otaLastActivity=0;
String adminNonce="nonce";
const char* ADMIN_PASSWORD="test-only-not-a-device-secret";
uint32_t millis(){return 0;}
void manualPowerOff(){powerOn=false;}
void logEvent(const char*){}
void yield(){}
void delay(int){}
'''
test=r'''
void resetFixture(){server=Server();Update=Updater();ESP=Esp();upload=HTTPUpload();powerOn=true;otaInProgress=otaSucceeded=otaFailed=otaAuthorized=false;}
void event(int status){upload.status=status;handleFirmwareUpload();}
void complete(){event(UPLOAD_FILE_WRITE);event(UPLOAD_FILE_END);finishFirmwareUpload();}
int main(){
 resetFixture();server.authenticated=false;event(UPLOAD_FILE_START);complete();
 assert(server.code==401&&Update.begins==0&&powerOn&&!ESP.restarted);
 resetFixture();server.tokenOK=false;event(UPLOAD_FILE_START);complete();
 assert(server.code==403&&Update.begins==0&&powerOn&&!ESP.restarted);
 resetFixture();finishFirmwareUpload();assert(server.code==400&&!ESP.restarted);
 resetFixture();upload.filename="filesystem.img";event(UPLOAD_FILE_START);complete();
 assert(server.code==400&&Update.begins==0&&powerOn&&!ESP.restarted);
 resetFixture();upload.name="filesystem";event(UPLOAD_FILE_START);complete();
 assert(server.code==400&&Update.begins==0);
 resetFixture();event(UPLOAD_FILE_START);assert(!powerOn&&otaInProgress);complete();
 assert(server.code==200&&ESP.restarted&&!powerOn&&!otaInProgress);
 resetFixture();Update.beginOK=false;event(UPLOAD_FILE_START);complete();
 assert(server.code==400&&!ESP.restarted&&!powerOn&&!otaInProgress);
 resetFixture();Update.writeOK=false;event(UPLOAD_FILE_START);complete();
 assert(server.code==400&&!ESP.restarted&&!powerOn&&!Update.running);
 resetFixture();Update.endOK=false;event(UPLOAD_FILE_START);complete();
 assert(server.code==400&&!ESP.restarted&&!powerOn);
 resetFixture();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE);event(UPLOAD_FILE_ABORTED);finishFirmwareUpload();
 assert(server.code==400&&!ESP.restarted&&!powerOn&&!otaInProgress&&!Update.running);
 // A fresh valid upload remains possible after an aborted request.
 event(UPLOAD_FILE_START);complete();assert(server.code==200&&ESP.restarted);
 std::cout<<"PASS: OTA authentication, nonce, missing/wrong file, flash failures, abort, retry, success\n";
}
'''
with tempfile.TemporaryDirectory(prefix='mirror-ota-tests-') as tmp:
    cpp=Path(tmp)/'ota.cpp';exe=Path(tmp)/('ota.exe' if os.name=='nt' else 'ota')
    cpp.write_text(stub+'\n'.join(functions)+test,encoding='utf-8')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
