#include "InheritedProperties.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc == 3) {
        int properties = atoi(argv[1]), hardware = atoi(argv[2]);
        CHECK(tc002PropertyWorkspaceFd() == properties);
        CHECK(fcntl(properties, F_GETFD) >= 0);
        CHECK(fcntl(hardware, F_GETFD) == -1 && errno == EBADF);
        puts("Property workspace survives exec; hardware descriptor closes");
        return 0;
    }
    unsetenv("ANDROID_PROPERTY_WORKSPACE");
    CHECK(tc002PropertyWorkspaceFd() == -1);
    setenv("ANDROID_PROPERTY_WORKSPACE", "999,32768", 1);
    CHECK(tc002PropertyWorkspaceFd() == -1);
    char path[] = "/tmp/tc002-properties-XXXXXX";
    int writable = mkstemp(path);
    CHECK(writable >= 0 && ftruncate(writable, 32768) == 0);
    int properties = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(properties >= 3);
    unlink(path);
    char workspace[64], propArg[16], hwArg[16];
    snprintf(workspace, sizeof(workspace), "%d,32768", writable);
    setenv("ANDROID_PROPERTY_WORKSPACE", workspace, 1);
    CHECK(tc002PropertyWorkspaceFd() == -1); // never retain a writable mapping
    snprintf(workspace, sizeof(workspace), "%d,65536", properties);
    setenv("ANDROID_PROPERTY_WORKSPACE", workspace, 1);
    CHECK(tc002PropertyWorkspaceFd() == -1);
    snprintf(workspace, sizeof(workspace), "%d,32768junk", properties);
    setenv("ANDROID_PROPERTY_WORKSPACE", workspace, 1);
    CHECK(tc002PropertyWorkspaceFd() == -1);
    snprintf(workspace, sizeof(workspace), "%d,32768", properties);
    setenv("ANDROID_PROPERTY_WORKSPACE", workspace, 1);
    CHECK(tc002PropertyWorkspaceFd() == properties);
    snprintf(propArg, sizeof(propArg), "%d", properties);
    snprintf(hwArg, sizeof(hwArg), "%d", writable);
    tc002PrepareExecDescriptors();
    execl("/proc/self/exe", argv[0], propArg, hwArg, (char*)0);
    perror("exec");
    return 1;
}
