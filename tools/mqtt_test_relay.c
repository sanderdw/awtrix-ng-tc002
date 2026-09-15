/* Test-only loopback relay for old ADB daemons without `adb reverse`.
 * Device MQTT connects to 18883; host connects through adb forward to 18885.
 * Neither listener binds to Wi-Fi. Exits after 90 seconds or SIGTERM.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop(int signal) { (void)signal; stopped=1; }
static int listener(int port) {
    int fd=socket(AF_INET,SOCK_STREAM,0),one=1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_port=htons(port); addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(fd<0 || bind(fd,(struct sockaddr*)&addr,sizeof(addr)) || listen(fd,4)) return -1;
    return fd;
}
static int copy(int source,int target) {
    char buffer[4096]; ssize_t n=read(source,buffer,sizeof(buffer));
    if(n<=0) return 0;
    ssize_t offset=0;
    while(offset<n) {
        ssize_t sent=send(target,buffer+offset,n-offset,MSG_NOSIGNAL);
        if(sent<=0) return 0;
        offset+=sent;
    }
    return 1;
}
int main(void) {
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGPIPE,SIG_IGN);
    int mqtt=listener(18883),host=listener(18885);
    if(mqtt<0 || host<0) { perror("relay listener"); return 1; }
    FILE* file=fopen("/tmp/awtrix-relay.pid","w");
    if(file) { fprintf(file,"%d\n",getpid()); fclose(file); }
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now); time_t deadline=now.tv_sec+90;
    int peers[2]={-1,-1};
    while(!stopped) {
        clock_gettime(CLOCK_MONOTONIC,&now); if(now.tv_sec>=deadline) break;
        struct pollfd fds[2]={{peers[0]<0?mqtt:peers[0],POLLIN,0},{peers[1]<0?host:peers[1],POLLIN,0}};
        if(poll(fds,2,500)<=0) continue;
        int failed=0;
        for(int i=0;i<2;++i) if(fds[i].revents) {
            if(peers[i]<0) {
                peers[i]=accept(fds[i].fd,NULL,NULL);
                struct timeval timeout={1,0};
                setsockopt(peers[i],SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
            } else if(peers[1-i]>=0 && !copy(peers[i],peers[1-i])) failed=1;
        }
        if(failed) { for(int i=0;i<2;++i) { if(peers[i]>=0) close(peers[i]); peers[i]=-1; } }
    }
    for(int i=0;i<2;++i) if(peers[i]>=0) close(peers[i]);
    close(mqtt); close(host); return 0;
}
