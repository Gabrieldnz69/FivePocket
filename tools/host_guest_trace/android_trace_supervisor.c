#define _POSIX_C_SOURCE 200809L
/* One-shot diagnostic containment. This is not an Android application sandbox. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROTOCOL_LIMIT (64U * 1024U)
#define MAX_PROTOCOL_LIMIT (PROTOCOL_LIMIT + 2U * (2U * 1024U * 1024U - 4096U))
#define SWAP_IMAGE_FILE_LIMIT (1280U * 720U * 4U)

static int64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) _exit(125);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int positive_seconds(const char *value) {
    char *end = NULL;
    errno = 0;
    long result = strtol(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' && result > 0 && result <= 300
            ? (int)result : 0;
}

static int limit_checked(int resource, rlim_t soft, rlim_t hard) {
    struct rlimit requested = {soft, hard}, observed;
    return setrlimit(resource, &requested) == 0
            && getrlimit(resource, &observed) == 0
            && observed.rlim_cur == soft && observed.rlim_max == hard;
}

static void cleanup_group(pid_t child, int reaped) {
    /* setsid in the worker makes the child PID its process-group ID. */
    kill(-child, SIGTERM);
    struct timespec grace = {0, 250000000};
    while (nanosleep(&grace, &grace) < 0 && errno == EINTR) {}
    kill(-child, SIGKILL);
    if (!reaped) kill(child, SIGKILL); /* A child that failed before setsid. */
}

int main(int argc, char **argv) {
    if (argc < 5) return 125;
    size_t protocol_limit=PROTOCOL_LIMIT;
    int directory_index=3;
    if(argc>=7 && strcmp(argv[3],"--protocol-bytes")==0) {
        char *end=NULL;errno=0;unsigned long value=strtoul(argv[4],&end,10);
        if(errno || end==argv[4] || *end || value<PROTOCOL_LIMIT || value>MAX_PROTOCOL_LIMIT)return 125;
        protocol_limit=(size_t)value;directory_index=5;
    }
    int swap_image_file=0;
    if(argc>directory_index && strcmp(argv[directory_index],"--swap-image-file")==0){
        swap_image_file=1;++directory_index;
    }
    if(argc<directory_index+2)return 125;
    const int executable_index=directory_index+1;
    /* ADB loss must not cancel the device-side wall deadline. The driver also
       starts this supervisor in its own session with setsid. */
    if (signal(SIGHUP, SIG_IGN) == SIG_ERR || signal(SIGPIPE, SIG_IGN) == SIG_ERR) return 125;
    int wall_seconds = positive_seconds(argv[1]);
    int cpu_seconds = positive_seconds(argv[2]);
    struct stat info;
    if (!wall_seconds || !cpu_seconds || argv[directory_index][0] != '/' || argv[executable_index][0] != '/'
            || lstat(argv[directory_index], &info) != 0 || !S_ISDIR(info.st_mode)
            || (info.st_mode & 0777) != 0700
            || lstat(argv[executable_index], &info) != 0 || !S_ISREG(info.st_mode)
            || (info.st_mode & 0777) != 0700 || chdir(argv[directory_index]) != 0) return 125;
    unsigned char *protocol=malloc(protocol_limit+1);
    if(!protocol)return 125;
    int output[2], ready[2];
    if (pipe(output) != 0 || pipe(ready) != 0) return 125;
    if (fcntl(ready[1], F_SETFD, FD_CLOEXEC) != 0) return 125;
    const int64_t started = milliseconds();
    pid_t child = fork();
    if (child < 0) return 125;
    if (child == 0) {
        close(output[0]);
        close(ready[0]);
        if (setsid() < 0) _exit(125);
        if (signal(SIGHUP, SIG_DFL) == SIG_ERR || signal(SIGPIPE, SIG_DFL) == SIG_ERR) _exit(125);
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0
                || dup2(output[1], STDOUT_FILENO) < 0
                || dup2(null_fd, STDERR_FILENO) < 0) _exit(125);
        close(null_fd);
        close(output[1]);
        const rlim_t file_limit=swap_image_file ? SWAP_IMAGE_FILE_LIMIT : 0;
        if (!limit_checked(RLIMIT_CORE, 0, 0) || !limit_checked(RLIMIT_FSIZE, file_limit, file_limit)
                /* Android's Vulkan loader exceeds 32 transient descriptors
                   before main. Keep a finite bound including content files. */
                || !limit_checked(RLIMIT_NOFILE, 128, 128)
                || !limit_checked(RLIMIT_CPU, (rlim_t)cpu_seconds, (rlim_t)cpu_seconds + 1))
            _exit(125);
        if (write(ready[1], "1", 1) != 1) _exit(125);
        char *environment[] = {"PATH=/system/bin", "LANG=C", "LC_ALL=C", NULL};
        execve(argv[executable_index], argv + executable_index, environment);
        _exit(125);
    }
    close(output[1]);
    close(ready[1]);
    if (fcntl(output[0], F_SETFL, O_NONBLOCK) != 0
            || fcntl(ready[0], F_SETFL, O_NONBLOCK) != 0) {
        cleanup_group(child, 0);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        return 125;
    }
    size_t captured = 0;
    int status = 0, reaped = 0, eof = 0, verified = 0;
    const char *outcome = NULL;
    while (outcome == NULL) {
        char ready_byte;
        if (read(ready[0], &ready_byte, 1) == 1 && ready_byte == '1') verified = 1;
        /* Each iteration reads at most one bounded chunk, so a continuous writer
           cannot starve the independent wall-clock check. */
        if (!eof) {
            size_t remaining = protocol_limit+1 - captured;
            ssize_t count = read(output[0], protocol + captured,
                    remaining < 8192 ? remaining : 8192);
            if (count > 0) captured += (size_t)count;
            else if (count == 0) eof = 1;
            else if (errno != EINTR && errno != EAGAIN) outcome = "launch-failure";
        }
        if (captured > protocol_limit) outcome = "protocol-limit";
        if (!reaped) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) reaped = 1;
            else if (waited < 0 && errno != EINTR) outcome = "launch-failure";
        }
        if (outcome == NULL && reaped && eof) outcome = "exited";
        if (outcome == NULL && milliseconds() - started >= (int64_t)wall_seconds * 1000)
            outcome = "wall-timeout";
        if (outcome == NULL) {
            struct pollfd descriptor = {output[0], POLLIN | POLLHUP, 0};
            poll(&descriptor, 1, 10);
        }
    }
    char ready_byte;
    if (read(ready[0], &ready_byte, 1) == 1 && ready_byte == '1') verified = 1;
    cleanup_group(child, reaped);
    if (!reaped) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    close(output[0]);
    close(ready[0]);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    if (strcmp(outcome, "exited") == 0) {
        if (!verified || exit_code == 125) outcome = "launch-failure";
        else if (signal_number == SIGXCPU) outcome = "cpu-limit";
        else if (signal_number) outcome = "unix-signal";
    }
    printf("ANDROID_TRACE_SUPERVISOR_V1\noutcome=%s\nexit_code=%d\nsignal=%d\n"
           "elapsed_milliseconds=%lld\nlimits_verified=%d\n",
           outcome, exit_code, signal_number, (long long)(milliseconds() - started), verified);
    if(swap_image_file)printf("swap_image_file_limit=%u\n",SWAP_IMAGE_FILE_LIMIT);
    printf("WORKER_PROTOCOL\n");
    if (strcmp(outcome, "exited") == 0 && exit_code == 0)
        fwrite(protocol, 1, captured, stdout);
    free(protocol);
    return 0;
}
