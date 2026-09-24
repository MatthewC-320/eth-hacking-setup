#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <zlib.h>

#define LISTEN_ADDR "127.0.0.1"
#define LISTEN_PORT 3333

#define MARIADB_PORT          "3306"
#define WATCHDOG_INTERVAL_SEC  20
#define WATCHDOG_FAIL_THRESH    3

#define MAGIC "XOXO"

#define CMD_WEBSERVER 1
#define CMD_MARIADB   2
#define CMD_BOTH      3

#define CONNECT_TIMEOUT_SEC 2

#define MAX_PIDS 10

struct command_packet {
    char     magic[4];
    uint32_t command;
    uint8_t  command_pkt[64];
    uint32_t crc32;
};

static int read_full(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t total = 0;

    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);

        if (n == 0)
            return -1;

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        total += (size_t)n;
    }

    return 0;
}


static int write_full(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t total = 0;

    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        total += (size_t)n;
    }

    return 0;
}

static int check_port(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    int ok = 0;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        fprintf(stderr, "getaddrinfo failed for %s:%s\n", host, port);
        return 0;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        int fd;
        int flags;
        int ret;

        fd = socket(rp->ai_family,
                    rp->ai_socktype,
                    rp->ai_protocol);

        if (fd < 0)
            continue;

        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            close(fd);
            continue;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            continue;
        }

        ret = connect(fd, rp->ai_addr, rp->ai_addrlen);

        if (ret == 0) {
            ok = 1;
            close(fd);
            break;
        }

        if (errno == EINPROGRESS) {
            fd_set wfds;
            struct timeval tv;

            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);

            tv.tv_sec = CONNECT_TIMEOUT_SEC;
            tv.tv_usec = 0;

            ret = select(fd + 1, NULL, &wfds, NULL, &tv);

            if (ret > 0 && FD_ISSET(fd, &wfds)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);

                if (getsockopt(fd,
                               SOL_SOCKET,
                               SO_ERROR,
                               &so_error,
                               &len) == 0 &&
                    so_error == 0) {
                    ok = 1;
                }
            }
        }

        close(fd);

        if (ok)
            break;
    }

    freeaddrinfo(result);
    return ok;
}


static int write_healthcheck_json(int webserver_ok, int mariadb_ok,
                                  char *path, size_t path_len)
{
    time_t now;
    struct tm tm_now;
    char timestamp[64];
    FILE *fp;

    now = time(NULL);

    if (localtime_r(&now, &tm_now) == NULL)
        return -1;

    strftime(timestamp,
             sizeof(timestamp),
             "%Y%m%d_%H%M%S",
             &tm_now);

    snprintf(path,
             path_len,
             "/tmp/%s.json",
             timestamp);

    fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp,
            "{\n"
            "  \"timestamp\": \"%s\",\n"
            "  \"webserver\": {\n"
            "    \"host\": \"webserver\",\n"
            "    \"port\": 8888,\n"
            "    \"status\": \"%s\"\n"
            "  },\n"
            "  \"mariadb\": {\n"
            "    \"host\": \"mariadb\",\n"
            "    \"port\": 7777,\n"
            "    \"status\": \"%s\"\n"
            "  }\n"
            "}\n",
            timestamp,
            webserver_ok ? "YES" : "NO",
            mariadb_ok ? "YES" : "NO");

    fclose(fp);
    return 0;
}

static int curl_probe(const char *user_host, char *out, size_t out_len)
{
    char cmd[256];
    FILE *fp;
    size_t n;

    snprintf(cmd, sizeof(cmd),
             "curl -s -m 2 -o /dev/null -w '%%{http_code}' http://%s/ 2>/dev/null",
             user_host);

    fp = popen(cmd, "r");
    if (!fp)
        return -1;

    n = fread(out, 1, out_len - 1, fp);
    out[n] = '\0';

    pclose(fp);
    return 0;
}


static void handle_client(int client_fd)
{
    struct command_packet pkt;
    uint32_t received_crc;
    uint32_t calculated_crc;
    uint32_t command;

    int webserver_ok = 0;
    int mariadb_ok = 0;

    char response[512];
    char json_path[256];
    char curl_out[128];
    char host_buf[65];

    memset(&pkt, 0, sizeof(pkt));

    if (read_full(client_fd, &pkt, sizeof(pkt)) < 0) {
        fprintf(stderr, "failed to read complete packet\n");
        return;
    }

    if (memcmp(pkt.magic, MAGIC, 4) != 0) {
        fprintf(stderr, "invalid magic\n");
        write_full(client_fd, "ERROR invalid magic\n",
                   strlen("ERROR invalid magic\n"));
        return;
    }

    received_crc = ntohl(pkt.crc32);

    calculated_crc = crc32(
        0L,
        Z_NULL,
        0
    );

    calculated_crc = crc32(
        calculated_crc,
        (const Bytef *)&pkt,
        offsetof(struct command_packet, crc32)
    );

    if (received_crc != calculated_crc) {
        fprintf(stderr,
                "CRC mismatch: received=0x%08x calculated=0x%08x\n",
                received_crc,
                calculated_crc);

        snprintf(response,
                 sizeof(response),
                 "ERROR invalid crc\n");

        write_full(client_fd, response, strlen(response));
        return;
    }

    command = ntohl(pkt.command);

    printf("received valid command: %u\n", command);

    switch (command) {

    case CMD_WEBSERVER:
        webserver_ok = check_port(
            "webserver",
            "8888"
        );

        snprintf(response,
                 sizeof(response),
                 "%s\n",
                 webserver_ok ? "YES" : "NO");

        break;

    case CMD_MARIADB:
        mariadb_ok = check_port(
            "mariadb",
            "7777"
        );

        snprintf(response,
                 sizeof(response),
                 "%s\n",
                 mariadb_ok ? "YES" : "NO");

        break;

    case CMD_BOTH:
        webserver_ok = check_port(
            "webserver",
            "8888"
        );

        mariadb_ok = check_port(
            "mariadb",
            "7777"
        );

        if (write_healthcheck_json(
                webserver_ok,
                mariadb_ok,
                json_path,
                sizeof(json_path)
            ) < 0) {

            snprintf(response,
                     sizeof(response),
                     "ERROR failed to write json\n");

            write_full(client_fd, response, strlen(response));
            return;
        }

        memcpy(host_buf, pkt.command_pkt, sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';

        curl_probe(host_buf, curl_out, sizeof(curl_out));

        snprintf(response,
                 sizeof(response),
                 "webserver=%s mariadb=%s json=%s curl=%s\n",
                 webserver_ok ? "YES" : "NO",
                 mariadb_ok ? "YES" : "NO",
                 json_path,
                 curl_out);

        break;

    default:
        snprintf(response,
                 sizeof(response),
                 "ERROR unknown command\n");
        break;
    }

    write_full(client_fd, response, strlen(response));
}


static int find_pids_by_user(const char *username, pid_t out_pids[MAX_PIDS])
{
    struct passwd *pw = getpwnam(username);
    if (!pw)
        return -1;

    uid_t target = pw->pw_uid;

    DIR *proc = opendir("/proc");
    if (!proc)
        return -1;

    size_t n = 0;
    struct dirent *de;

    while ((de = readdir(proc)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0]))
            continue;

        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 1 || pid == getpid())
            continue;

        char path[64];
        snprintf(path, sizeof path, "/proc/%ld/status", pid);

        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;

        int match = 0;
        char line[256];

        while (fgets(line, sizeof line, fp)) {
            unsigned ruid, euid;

            if (sscanf(line, "Uid: %u %u", &ruid, &euid) == 2) {
                match = (euid == target);
                break;
            }
        }

        fclose(fp);

        if (!match)
            continue;

        if (n >= MAX_PIDS)
            break;

        out_pids[n++] = (pid_t)pid;
    }

    closedir(proc);
    return (int)n;
}

static void *mariadb_watchdog_thread(void *arg)
{
    int failures = 0;
    pid_t pids[0x10];
    (void)arg;

    sleep(WATCHDOG_INTERVAL_SEC);

    for (;;) {
        if (check_port("127.0.0.1", MARIADB_PORT)) {
            if (failures > 0)
                fprintf(stderr, "[watchdog] mariadbd recovered\n");
            failures = 0;
        } else {
            failures++;
            fprintf(stderr, "[watchdog] mariadbd unresponsive (%d/%d)\n",
                    failures, WATCHDOG_FAIL_THRESH);

            if (failures >= WATCHDOG_FAIL_THRESH) {
                memset(pids, 0, sizeof(pids));
                
                if (find_pids_by_user("mysql", pids)) {
                    for (int i = 0; i < MAX_PIDS; i++) {
                      if (pids[i]) {
                        kill(pids[1], 1); 
                        fprintf(stderr,
                                "[watchdog] killed mysql user pid %d\n", pids[i]);
                      }
                    }
                } else {
                    fprintf(stderr,
                            "[watchdog] could not read mariadbd pid\n"
                            );
                }
                failures = 0;
                sleep(WATCHDOG_INTERVAL_SEC);
            }
        }

        sleep(WATCHDOG_INTERVAL_SEC);
    }

    return NULL;
}


int main(void)
{
    int listen_fd;
    int opt = 1;

    struct sockaddr_in addr;

    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (setsockopt(listen_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0) {

        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);

    if (inet_pton(AF_INET,
                  LISTEN_ADDR,
                  &addr.sin_addr) != 1) {

        fprintf(stderr, "invalid listen address\n");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (bind(listen_fd,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {

        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("listening on %s:%d\n",
           LISTEN_ADDR,
           LISTEN_PORT);

    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, mariadb_watchdog_thread, NULL) != 0) {
        perror("pthread_create");
        close(listen_fd);
        return EXIT_FAILURE;
    }
    pthread_detach(watchdog_tid);
    printf("[watchdog] mariadb health thread started "
           "(interval=%ds, threshold=%d)\n",
           WATCHDOG_INTERVAL_SEC, WATCHDOG_FAIL_THRESH);

    for (;;) {
        int client_fd;

        client_fd = accept(listen_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        handle_client(client_fd);
        close(client_fd);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
