#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <dirent.h>

#define PORT 9410
#define BUFFER_SIZE 1024
#define AUTH_TOKEN "OPS-3500"
#define SID "0053"
#define UDP_MONITOR_PORT 9411
#define MONITOR_INTERVAL 5

volatile int monitor_running = 0;
pthread_t monitor_thread;
int monitor_socket = -1;

void write_log(const char *event)
{
    FILE *log_file = fopen("remoteops.log", "a");

    if (log_file == NULL)
        return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (t != NULL) {
        fprintf(log_file,
                "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                t->tm_year + 1900,
                t->tm_mon + 1,
                t->tm_mday,
                t->tm_hour,
                t->tm_min,
                t->tm_sec,
                event);
    }

    fclose(log_file);
}

double get_cpu_load(void)
{
    FILE *file;
    unsigned long long user, nice, system, idle, iowait;
    unsigned long long irq, softirq, steal;

    file = fopen("/proc/stat", "r");

    if (file == NULL)
        return -1.0;

    if (fscanf(file,
               "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user,
               &nice,
               &system,
               &idle,
               &iowait,
               &irq,
               &softirq,
               &steal) != 8) {

        fclose(file);
        return -1.0;
    }

    fclose(file);

    unsigned long long idle_time = idle + iowait;

    unsigned long long total_time =
        user + nice + system + idle +
        iowait + irq + softirq + steal;

    if (total_time == 0)
        return 0.0;

    return 100.0 *
           (1.0 - ((double)idle_time / total_time));
}

void *monitor_worker(void *arg)
{
    (void)arg;

    struct sockaddr_in monitor_addr;

    monitor_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (monitor_socket < 0) {
        perror("monitor socket");
        monitor_running = 0;
        write_log("MONITOR socket creation failed");
        return NULL;
    }

    memset(&monitor_addr, 0, sizeof(monitor_addr));

    monitor_addr.sin_family = AF_INET;
    monitor_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    monitor_addr.sin_port = htons(UDP_MONITOR_PORT);

    printf("UDP monitor started on port %d.\n",
           UDP_MONITOR_PORT);

    write_log("MONITOR worker started");

    while (monitor_running) {

        double cpu_load = get_cpu_load();

        struct sysinfo info;

        if (sysinfo(&info) == 0) {

            unsigned long long total_memory =
                (unsigned long long)info.totalram *
                info.mem_unit;

            unsigned long long free_memory =
                (unsigned long long)info.freeram *
                info.mem_unit;

            unsigned long long used_memory =
                total_memory - free_memory;

            double used_memory_mb =
                (double)used_memory /
                (1024.0 * 1024.0);

            char message[BUFFER_SIZE];

            snprintf(message,
                     sizeof(message),
                     "MONITOR CPU:%.2f MEM:%.0f UPTIME:%lu SID:%s\n",
                     cpu_load,
                     used_memory_mb,
                     info.uptime,
                     SID);

            sendto(monitor_socket,
                   message,
                   strlen(message),
                   0,
                   (struct sockaddr *)&monitor_addr,
                   sizeof(monitor_addr));

            printf("%s", message);
        }

        sleep(MONITOR_INTERVAL);
    }

    close(monitor_socket);

    monitor_socket = -1;

    printf("UDP monitor stopped.\n");

    write_log("MONITOR worker stopped");

    return NULL;
}

void *handle_controller(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    char buffer[BUFFER_SIZE];

    int authenticated = 0;

    printf("Controller thread started.\n");

    /* First command must be AUTH */

    memset(buffer, 0, sizeof(buffer));

    int bytes_received =
        recv(client_fd,
             buffer,
             sizeof(buffer) - 1,
             0);

    if (bytes_received <= 0) {

        printf("Controller disconnected before authentication.\n");

        close(client_fd);

        return NULL;
    }

    buffer[bytes_received] = '\0';

    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("Received: %s\n", buffer);

    if (strcmp(buffer, "AUTH " AUTH_TOKEN) == 0) {

        authenticated = 1;

        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "OK AUTHENTICATED SID:%s\n",
                 SID);

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication successful.\n");

        write_log("AUTH successful");

    } else {

        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 001 AUTH_FAILED SID:%s\n",
                 SID);

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication failed.\n");

        write_log("AUTH failed");

        close(client_fd);

        return NULL;
    }

    if (!authenticated) {

        close(client_fd);

        return NULL;
    }

    printf("Controller is authenticated.\n");

    /* Handle commands from this Controller */

    while (1) {

        memset(buffer, 0, sizeof(buffer));

        bytes_received =
            recv(client_fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (bytes_received <= 0) {

            printf("Controller disconnected.\n");

            break;
        }

        buffer[bytes_received] = '\0';

        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received command: %s\n", buffer);

        /* LISTPROC */

        if (strcmp(buffer, "LISTPROC") == 0) {

            DIR *proc_dir;

            struct dirent *entry;

            char response[BUFFER_SIZE];

            int offset = 0;

            proc_dir = opendir("/proc");

            if (proc_dir == NULL) {

                snprintf(response,
                         sizeof(response),
                         "ERR 004 LISTPROC_FAILED SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("LISTPROC failed");

                continue;
            }

            offset +=
                snprintf(response + offset,
                         sizeof(response) - offset,
                         "LISTPROC");

            while ((entry = readdir(proc_dir)) != NULL) {

                int is_process = 1;

                for (int i = 0;
                     entry->d_name[i] != '\0';
                     i++) {

                    if (entry->d_name[i] < '0' ||
                        entry->d_name[i] > '9') {

                        is_process = 0;

                        break;
                    }
                }

                if (is_process) {

                    if ((size_t)offset <
                        sizeof(response) - 20) {

                        offset +=
                            snprintf(response + offset,
                                     sizeof(response) - offset,
                                     " %s",
                                     entry->d_name);
                    }
                }
            }

            closedir(proc_dir);

            snprintf(response + offset,
                     sizeof(response) - offset,
                     " SID:%s\n",
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            write_log("LISTPROC completed");

            continue;
        }

        /* SYSINFO */

        if (strcmp(buffer, "SYSINFO") == 0) {

            struct sysinfo info;

            if (sysinfo(&info) == 0) {

                double cpu_load =
                    get_cpu_load();

                unsigned long long total_memory =
                    (unsigned long long)info.totalram *
                    info.mem_unit;

                unsigned long long free_memory =
                    (unsigned long long)info.freeram *
                    info.mem_unit;

                unsigned long long used_memory =
                    total_memory - free_memory;

                double used_memory_mb =
                    (double)used_memory /
                    (1024.0 * 1024.0);

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "SYSINFO %.2f %.0f %lu SID:%s\n",
                         cpu_load,
                         used_memory_mb,
                         info.uptime,
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("SYSINFO completed");

            } else {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 003 SYSINFO_FAILED SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("SYSINFO failed");
            }

            continue;
        }

        /* EXEC whitelist */

        if (strncmp(buffer, "EXEC ", 5) == 0) {

            char exec_command[BUFFER_SIZE];

            char response[BUFFER_SIZE];

            strcpy(exec_command, buffer + 5);

            /* DATE */

            if (strcmp(exec_command, "DATE") == 0) {

                FILE *fp = popen("date", "r");

                if (fp == NULL) {

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 EXEC_FAILED SID:%s\n",
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC DATE failed");

                    continue;
                }

                if (fgets(response,
                          sizeof(response),
                          fp) != NULL) {

                    response[strcspn(response,
                                      "\r\n")] = '\0';

                    char final_response[BUFFER_SIZE];

                    snprintf(final_response,
                             sizeof(final_response),
                             "EXEC DATE %.900s SID:%s\n",
                             response,
                             SID);

                    send(client_fd,
                         final_response,
                         strlen(final_response),
                         0);

                    write_log("EXEC DATE completed");
                }

                pclose(fp);

                continue;
            }

            /* UPTIME */

            if (strcmp(exec_command, "UPTIME") == 0) {

                struct sysinfo info;

                if (sysinfo(&info) == 0) {

                    snprintf(response,
                             sizeof(response),
                             "EXEC UPTIME %lu SID:%s\n",
                             info.uptime,
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC UPTIME completed");

                } else {

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 EXEC_FAILED SID:%s\n",
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC UPTIME failed");
                }

                continue;
            }

            /* DISKFREE */

            if (strcmp(exec_command, "DISKFREE") == 0) {

                struct statvfs disk_info;

                if (statvfs("/", &disk_info) == 0) {

                    unsigned long long free_bytes =
                        (unsigned long long)disk_info.f_bavail *
                        disk_info.f_frsize;

                    unsigned long long free_mb =
                        free_bytes /
                        (1024 * 1024);

                    snprintf(response,
                             sizeof(response),
                             "EXEC DISKFREE %lluMB SID:%s\n",
                             free_mb,
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC DISKFREE completed");

                } else {

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 EXEC_FAILED SID:%s\n",
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC DISKFREE failed");
                }

                continue;
            }

            /* HOSTNAME */

            if (strcmp(exec_command, "HOSTNAME") == 0) {

                char hostname[256];

                if (gethostname(hostname,
                                sizeof(hostname)) == 0) {

                    hostname[sizeof(hostname) - 1] = '\0';

                    snprintf(response,
                             sizeof(response),
                             "EXEC HOSTNAME %s SID:%s\n",
                             hostname,
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC HOSTNAME completed");

                } else {

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 EXEC_FAILED SID:%s\n",
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC HOSTNAME failed");
                }

                continue;
            }

            /* WHOAMI */

            if (strcmp(exec_command, "WHOAMI") == 0) {

                char username[256];

                if (getlogin_r(username,
                               sizeof(username)) == 0) {

                    snprintf(response,
                             sizeof(response),
                             "EXEC WHOAMI %s SID:%s\n",
                             username,
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC WHOAMI completed");

                } else {

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 EXEC_FAILED SID:%s\n",
                             SID);

                    send(client_fd,
                         response,
                         strlen(response),
                         0);

                    write_log("EXEC WHOAMI failed");
                }

                continue;
            }

            /* Reject commands outside whitelist */

            snprintf(response,
                     sizeof(response),
                     "ERR 006 EXEC_NOT_ALLOWED SID:%s\n",
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            write_log("EXEC command not allowed");

            continue;
        }

        /* MONITOR START */

        if (strcmp(buffer, "MONITOR START") == 0) {

            if (monitor_running) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 012 MONITOR_ALREADY_RUNNING SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log(
                    "MONITOR START rejected: already running");

                continue;
            }

            monitor_running = 1;

            if (pthread_create(&monitor_thread,
                               NULL,
                               monitor_worker,
                               NULL) != 0) {

                monitor_running = 0;

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 013 MONITOR_START_FAILED SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("MONITOR START failed");

                continue;
            }

            pthread_detach(monitor_thread);

            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "OK MONITOR STARTED SID:%s\n",
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            write_log("MONITOR START requested");

            continue;
        }

        /* MONITOR STOP */

        if (strcmp(buffer, "MONITOR STOP") == 0) {

            if (!monitor_running) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 014 MONITOR_NOT_RUNNING SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log(
                    "MONITOR STOP rejected: not running");

                continue;
            }

            monitor_running = 0;

            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "OK MONITOR STOPPED SID:%s\n",
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            write_log("MONITOR STOP requested");

            continue;
        }

        /* PUT command */

        if (strncmp(buffer, "PUT ", 4) == 0) {

            char filename[256];

            size_t file_size;

            if (sscanf(buffer,
                       "PUT %255s %zu",
                       filename,
                       &file_size) != 2) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 008 PUT_INVALID SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("PUT invalid request");

                continue;
            }

            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "agentfiles/%s",
                     filename);

            FILE *file =
                fopen(filepath, "wb");

            if (file == NULL) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 009 PUT_FAILED SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log(
                    "PUT failed to open destination");

                continue;
            }

            size_t total_received = 0;

            int transfer_failed = 0;

            while (total_received < file_size) {

                char file_buffer[BUFFER_SIZE];

                size_t remaining =
                    file_size - total_received;

                size_t chunk =
                    remaining < sizeof(file_buffer)
                        ? remaining
                        : sizeof(file_buffer);

                ssize_t received =
                    recv(client_fd,
                         file_buffer,
                         chunk,
                         0);

                if (received <= 0) {

                    transfer_failed = 1;

                    break;
                }

                size_t written =
                    fwrite(file_buffer,
                           1,
                           (size_t)received,
                           file);

                if (written !=
                    (size_t)received) {

                    transfer_failed = 1;

                    break;
                }

                total_received +=
                    (size_t)received;
            }

            fclose(file);

            if (transfer_failed ||
                total_received != file_size) {

                remove(filepath);

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 009 PUT_FAILED SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("PUT transfer failed");

                continue;
            }

            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "OK PUT %zu SID:%s\n",
                     total_received,
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("PUT completed: %s (%zu bytes)\n",
                   filename,
                   total_received);

            write_log("PUT operation completed");

            continue;
        }

        /* GET command */

        if (strncmp(buffer, "GET ", 4) == 0) {

            char filename[256];

            if (sscanf(buffer,
                       "GET %255s",
                       filename) != 1) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 010 GET_INVALID SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("GET invalid request");

                continue;
            }

            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "agentfiles/%s",
                     filename);

            FILE *file =
                fopen(filepath, "rb");

            if (file == NULL) {

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 011 FILE_NOT_FOUND SID:%s\n",
                         SID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                write_log("GET file not found");

                continue;
            }

            if (fseek(file, 0, SEEK_END) != 0) {

                fclose(file);

                write_log(
                    "GET failed during file seek");

                continue;
            }

            long file_size = ftell(file);

            if (file_size < 0) {

                fclose(file);

                write_log(
                    "GET failed during file size check");

                continue;
            }

            if (fseek(file, 0, SEEK_SET) != 0) {

                fclose(file);

                write_log(
                    "GET failed during file rewind");

                continue;
            }

            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "GET %ld\n",
                     file_size);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            size_t total_sent = 0;

            int transfer_failed = 0;

            while (total_sent <
                   (size_t)file_size) {

                char file_buffer[BUFFER_SIZE];

                size_t bytes_read =
                    fread(file_buffer,
                          1,
                          sizeof(file_buffer),
                          file);

                if (bytes_read == 0)
                    break;

                size_t sent_total = 0;

                while (sent_total <
                       bytes_read) {

                    ssize_t sent =
                        send(client_fd,
                             file_buffer +
                                 sent_total,
                             bytes_read -
                                 sent_total,
                             0);

                    if (sent <= 0) {

                        transfer_failed = 1;

                        break;
                    }

                    sent_total +=
                        (size_t)sent;
                }

                if (transfer_failed)
                    break;

                total_sent += sent_total;
            }

            fclose(file);

            printf("GET completed: %s (%zu bytes)\n",
                   filename,
                   total_sent);

            if (transfer_failed ||
                total_sent !=
                    (size_t)file_size) {

                write_log("GET transfer failed");

            } else {

                write_log("GET operation completed");
            }

            continue;
        }

        /* Unknown command */

        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 007 UNKNOWN_COMMAND SID:%s\n",
                     SID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            write_log("Unknown command received");
        }
    }

    close(client_fd);

    printf("Controller thread finished.\n");

    return NULL;
}

int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

    /* Create TCP socket */

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0) {

        perror("socket");

        return 1;
    }

    /* Allow quick reuse of port */

    int option = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &option,
               sizeof(option));

    /* Configure server address */

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    /* Bind */

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {

        perror("bind");

        close(server_fd);

        return 1;
    }

    /* Listen */

    if (listen(server_fd, 10) < 0) {

        perror("listen");

        close(server_fd);

        return 1;
    }

    printf("RemoteOps Agent started.\n");

    printf("Listening on TCP port %d...\n",
           PORT);

    /* Accept Controllers continuously */

    while (1) {

        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int *client_fd =
            malloc(sizeof(int));

        if (client_fd == NULL) {

            perror("malloc");

            continue;
        }

        *client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (*client_fd < 0) {

            perror("accept");

            free(client_fd);

            continue;
        }

        printf("Controller connected.\n");

        write_log("Controller connected");

        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_controller,
                           client_fd) != 0) {

            perror("pthread_create");

            close(*client_fd);

            free(client_fd);

            write_log(
                "Controller thread creation failed");

            continue;
        }

        pthread_detach(thread_id);

        printf("Controller thread created.\n");
    }

    close(server_fd);

    return 0;
}
