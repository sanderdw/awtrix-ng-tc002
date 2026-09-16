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
#include <sys/stat.h>
#include "BootGuard.h"
#include "InheritedProperties.h"
#include "InputDevices.h"

static void* bootstrap;
static void (*vendorInit)(void*);
static void (*vendorDeinit)(void*);
static const char* (*vendorStartupApp)(void*);
static int fallback;

static const char* dataDir = "/data/awtrix-ng";

/* Two copies: /tmp for this boot, and a small synced file on the data partition that survives
 * the power cuts the boot counter is there for. */
static void note(const char* message) {
    char path[300];
    float uptime = 0;
    FILE* up = fopen("/proc/uptime", "r");
    if (up) { if (fscanf(up, "%f", &uptime) != 1) uptime = 0; fclose(up); }
    FILE* log = fopen("/tmp/awtrix-loader.log", "a");
    if (log) { fprintf(log, "[%6.1fs] %s\n", uptime, message); fclose(log); }
    snprintf(path, sizeof(path), "%s/launcher.log", dataDir);
    struct stat info;
    log = fopen(path, stat(path, &info) == 0 && info.st_size > 16384 ? "w" : "a");
    if (log) { fprintf(log, "[%6.1fs] %s\n", uptime, message); fflush(log); fsync(fileno(log)); fclose(log); }
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
    if(trial) dataDir="/tmp/awtrix-launcher-data";
    int attempts=0, held=0;
    if(!trial) {
        /* Count this start before anything slow happens, so a power cut a few seconds into the
         * boot still leaves its mark. */
        int inputs[2];
        const int count=tc002OpenInputDevices(inputs,2,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        held=tc002SelectHeld(inputs,count);
        int i;
        for(i=0;i<count;++i) close(inputs[i]);
        if(!held) {
            attempts=tc002RecordBootAttempt(counter);
            char line[96];
            snprintf(line,sizeof(line),"start recorded: attempt %d of %d before fallback",attempts,TC002_BOOT_ATTEMPT_LIMIT);
            note(line);
        }
    }
    system("/bin/setprop sys.zkapp.state running");
    bootstrap=dlopen(vendor,RTLD_LAZY|RTLD_GLOBAL);
    if(!trial) {
        if(held) {
            tc002ClearBootAttempts(counter);
            runVendorApplication(context,"knob held at power-on: starting the vendor application");
            return;
        }
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
    note("handing over to AWTRIX");
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
