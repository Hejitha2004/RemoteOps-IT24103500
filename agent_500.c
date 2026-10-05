#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

#define PORT 9410
#define BUFFER_SIZE 1024
#define AUTH_TOKEN "OPS-3500"
#define SID "0053"

double get_cpu_load(void)
{
    FILE *file;
    unsigned long long user, nice, system, idle, iowait;
    unsigned long long irq, softirq, steal;

    file = fopen("/proc/stat", "r");

    if (file == NULL)
    {
        return -1.0;
    }

    if (fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait,
               &irq, &softirq, &steal) != 8)
    {
        fclose(file);
        return -1.0;
    }

    fclose(file);

    unsigned long long idle_time = idle + iowait;
    unsigned long long total_time =
        user + nice + system + idle + iowait + irq + softirq + steal;

    if (total_time == 0)
    {
        return 0.0;
    }

    return 100.0 * (1.0 - ((double)idle_time / total_time));
}

int main(void)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    int authenticated = 0;

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /* Allow quick reuse of the port */
    int option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &option, sizeof(option));

    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* Bind to port 9410 */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /* Listen for Controllers */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent started.\n");
    printf("Listening on TCP port %d...\n", PORT);

    /* Accept one Controller */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
         if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }perror("accept");
        close(server_fd);
        return 1;
    }

    printf("Controller connected.\n");

    /* Receive authentication command */
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(client_fd,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

    if (bytes_received <= 0)
    {
        printf("Controller disconnected.\n");
        close(client_fd);
        close(server_fd);
        return 0;
    }

    buffer[bytes_received] = '\0';

    /* Remove newline */
    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("Received: %s\n", buffer);

    /* Check AUTH command */
    if (strcmp(buffer, "AUTH " AUTH_TOKEN) == 0)
    {
        authenticated = 1;

        char response[BUFFER_SIZE];

        snprintf(response, sizeof(response),
                 "OK AUTHENTICATED SID:%s\n", SID);

        send(client_fd, response, strlen(response), 0);

        printf("Authentication successful.\n");
    }
    else
    {
        char response[BUFFER_SIZE];

        snprintf(response, sizeof(response),
                 "ERR 001 AUTH_FAILED SID:%s\n", SID);

        send(client_fd, response, strlen(response), 0);

        printf("Authentication failed.\n");
    }

    /* Keep authenticated variable for the next stages */
    /* Keep authenticated connection open for next commands */
if (authenticated)
{
    printf("Controller is authenticated.\n");

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';

        /* Remove newline */
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received command: %s\n", buffer);
if (strcmp(buffer, "SYSINFO") == 0)
{
    struct sysinfo info;

    if (sysinfo(&info) == 0)
    {
        double cpu_load = get_cpu_load();

        unsigned long long total_memory =
            (unsigned long long)info.totalram * info.mem_unit;

        unsigned long long free_memory =
            (unsigned long long)info.freeram * info.mem_unit;

        unsigned long long used_memory =
            total_memory - free_memory;

        double used_memory_mb =
            (double)used_memory / (1024.0 * 1024.0);

        char response[BUFFER_SIZE];

        snprintf(response, sizeof(response),
                 "SYSINFO %.2f %.0f %lu SID:%s\n",
                 cpu_load,
                 used_memory_mb,
                 info.uptime,
                 SID);

        send(client_fd, response, strlen(response), 0);
    }
    else
    {
        char response[BUFFER_SIZE];

        snprintf(response, sizeof(response),
                 "ERR 003 SYSINFO_FAILED SID:%s\n",
                 SID);

        send(client_fd, response, strlen(response), 0);
    }

    continue;
}
/* Authentication is already completed */
char response[BUFFER_SIZE];

snprintf(response, sizeof(response),
         "OK COMMAND_RECEIVED SID:%s\n", SID);

send(client_fd, response, strlen(response), 0);

printf("Command accepted after authentication.\n");
    }
}

close(client_fd);
    close(server_fd);

    printf("Agent stopped.\n");

    return 0;
}
