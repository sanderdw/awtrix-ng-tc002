/* TC002 loader: the vendor init service starts zkgui, which loads this module in place of the
 * vendor application. Normally it reuses the vendor radio initialization and then replaces the
 * whole GUI process with AWTRIX. Two things make it run the vendor application instead:
 *   - the knob is held while the clock powers on, or
 *   - three consecutive AWTRIX starts never reached a healthy state (see BootGuard.h).
 * That fallback needs no ADB and no computer: the stock UI, its updater and ADB come back.
 */
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include "BootGuard.h"
#include "InheritedProperties.h"
#include "InputDevices.h"

static void* bootstrap;
static void (*vendorInit)(void*);
static void (*vendorDeinit)(void*);
static const char* (*vendorStartupApp)(void*);
static int fallback;

static void note(const char* message) {
    FILE* log = fopen("/tmp/awtrix-loader.log", "a");
    if (log) { fputs(message, log); fputc('\n', log); fclose(log); }
}

/* Hand the process over to the vendor application: its plugin entry points run as if this
 * library had never been installed. */
static void runVendorApplication(void* context, const char* reason) {
    note(reason);
    fallback = 1;
    if (!bootstrap) { note("vendor application library is missing; nothing to fall back to"); return; }
    vendorInit = (void (*)(void*))dlsym(bootstrap, "onEasyUIInit");
    vendorDeinit = (void (*)(void*))dlsym(bootstrap, "onEasyUIDeinit");
    vendorStartupApp = (const char* (*)(void*))dlsym(bootstrap, "onStartupApp");
    if (vendorInit) vendorInit(context);
}

void onEasyUIInit(void* context) {
    /* The documented temporary test bundle supplies this environment variable.
     * The init service uses /res and has no test duration. */
    const char* trial=getenv("AWTRIX_TC002_TRIAL_ROOT");
    if(trial && (strncmp(trial,"/tmp/",5)!=0 || strlen(trial)>160)) trial=NULL;
    const char* root=trial ? trial : "/res";
    char binary[256],ui[256],data[256],vendor[256],counter[300];
    snprintf(binary,sizeof(binary),"%s/bin/awtrix-tc002",root);
    snprintf(ui,sizeof(ui),"%s/ui/awtrix.html",root);
    snprintf(data,sizeof(data),"%s",trial ? "/tmp/awtrix-launcher-data" : "/data/awtrix-ng");
    snprintf(vendor,sizeof(vendor),"%s",trial ? "/res/lib/libzkgui.so" : "/res/lib/libulanzi-bootstrap.so");
    snprintf(counter,sizeof(counter),"%s/boot-attempts",data);
    system("/bin/setprop sys.zkapp.state running");
    bootstrap=dlopen(vendor,RTLD_LAZY|RTLD_GLOBAL);
    if(!trial) {
        int inputs[2];
        const int count=tc002OpenInputDevices(inputs,2,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        const int held=tc002SelectHeld(inputs,count);
        int i;
        for(i=0;i<count;++i) close(inputs[i]);
        if(held) {
            tc002ClearBootAttempts(counter);
            runVendorApplication(context,"knob held at power-on: starting the vendor application");
            return;
        }
        const int attempts=tc002RecordBootAttempt(counter);
        if(tc002ShouldFallBack(attempts)) {
            tc002ClearBootAttempts(counter);
            runVendorApplication(context,"AWTRIX did not start healthily three times: starting the vendor application");
            return;
        }
    }
    if(bootstrap) {
        int (*wifiOn)(int)=(int(*)(int))dlsym(bootstrap,"_ZN4base13wifiOnAndWaitEi");
        if(wifiOn) wifiOn(15);
    }
    /* Leave the framework alive until its initial DHCP transaction finishes. A cold-started
     * radio can take well over 20 s to associate, so allow 45 s before handing over. */
    for(int i=0;i<450;++i) {
        int fd=socket(AF_INET,SOCK_DGRAM,0);
        struct ifreq req; memset(&req,0,sizeof(req)); strcpy(req.ifr_name,"wlan0");
        int ready=fd>=0 && ioctl(fd,SIOCGIFADDR,&req)==0 &&
          ((struct sockaddr_in*)&req.ifr_addr)->sin_addr.s_addr!=0;
        if(fd>=0) close(fd);
        if(ready) break;
        usleep(100000);
    }
    int log=open("/tmp/awtrix-ng.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(log>=0) { dup2(log,1); dup2(log,2); if(log>2) close(log); }
    /* Release hardware descriptors but preserve the vendor property mapping. */
    tc002PrepareExecDescriptors();
    if(trial)
      execl(binary,binary,"--hardware","--no-matrix","--data",data,"--webui",ui,
        "--port","18081","--run-for","90",(char*)0);
    else
      execl(binary,binary,"--hardware","--no-matrix","--data",data,"--webui",ui,(char*)0);
    perror("AWTRIX exec failed");
    runVendorApplication(context,"AWTRIX exec failed: starting the vendor application");
}
void onEasyUIDeinit(void* context) { if(fallback && vendorDeinit) vendorDeinit(context); }
const char* onStartupApp(void* context) {
    if(fallback && vendorStartupApp) return vendorStartupApp(context);
    return "mainActivity";
}
