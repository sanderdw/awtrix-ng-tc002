/* TC002 loader: the vendor init service starts zkgui, which loads this module.
 * Reuse the installed vendor radio initialization, then replace the entire GUI
 * process with AWTRIX. No vendor GUI or MQTT bridge runs alongside AWTRIX.
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
#include "InheritedProperties.h"

static void* bootstrap;
void onEasyUIInit(void* context) {
    (void)context;
    /* The documented temporary test bundle supplies this environment variable.
     * The init service uses /res and has no test duration. */
    const char* trial=getenv("AWTRIX_TC002_TRIAL_ROOT");
    if(trial && (strncmp(trial,"/tmp/",5)!=0 || strlen(trial)>160)) trial=NULL;
    const char* root=trial ? trial : "/res";
    char binary[256],ui[256],data[256],vendor[256];
    snprintf(binary,sizeof(binary),"%s/bin/awtrix-tc002",root);
    snprintf(ui,sizeof(ui),"%s/ui/awtrix.html",root);
    snprintf(data,sizeof(data),"%s",trial ? "/tmp/awtrix-launcher-data" : "/data/awtrix-ng");
    snprintf(vendor,sizeof(vendor),"%s",trial ? "/res/lib/libzkgui.so" : "/res/lib/libulanzi-bootstrap.so");
    system("/bin/setprop sys.zkapp.state running");
    bootstrap=dlopen(vendor,RTLD_LAZY|RTLD_GLOBAL);
    if(bootstrap) {
        int (*wifiOn)(int)=(int(*)(int))dlsym(bootstrap,"_ZN4base13wifiOnAndWaitEi");
        if(wifiOn) wifiOn(15);
    }
    /* Leave the framework alive until its initial DHCP transaction finishes. */
    for(int i=0;i<200;++i) {
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
    perror("AWTRIX exec failed; falling back to vendor application");
}
void onEasyUIDeinit(void* context) { (void)context; }
const char* onStartupApp(void* context) { (void)context; return "mainActivity"; }
