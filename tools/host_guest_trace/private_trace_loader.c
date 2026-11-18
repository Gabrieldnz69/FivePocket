#define _POSIX_C_SOURCE 200809L
#include <android/dlext.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Supervised private CLI adapter for the embedding ABI. The host runner stages
// and hashes the adjacent library; open an owned regular file without following
// its final symlink, and load that exact FD. No game inputs are bundled here.
int main(int argc,char** argv){
    if(argc<1 || !argv || !argv[0] || argv[0][0]!='/')return 2;
    const char* slash=strrchr(argv[0],'/');
    if(!slash || slash-argv[0]>PATH_MAX-32)return 2;
    char path[PATH_MAX];
    const int length=snprintf(path,sizeof(path),"%.*s/libhost_guest_trace.so",(int)(slash-argv[0]),argv[0]);
    if(length<0 || (size_t)length>=sizeof(path))return 2;
    const int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return 2;
    struct stat info;
    if(fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_uid!=geteuid() ||
        (info.st_mode&0022) || info.st_size<64 || info.st_size>512*1024*1024){close(fd);return 2;}
    android_dlextinfo extension={0};extension.flags=ANDROID_DLEXT_USE_LIBRARY_FD;extension.library_fd=fd;
    void* library=android_dlopen_ext(path,RTLD_NOW|RTLD_LOCAL,&extension);
    close(fd);
    if(!library)return 2;
    typedef int (*entry_type)(int,char**);
    entry_type entry=(entry_type)dlsym(library,"recomp_run_private_trace_v1");
    if(!entry){dlclose(library);return 2;}
    // Keep the library loaded through process exit after real guest execution,
    // including any thread-local destructor lifetime. Do not offer reload/resume.
    return entry(argc,argv);
}
