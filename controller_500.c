#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 1024
#define AUTH_TOKEN "OPS-3500"

int send_all(int sockfd, const void *data, size_t length)
{
    size_t total = 0;
    const char *ptr = data;

    while (total < length)
    {
        ssize_t sent = send(sockfd, ptr + total,
                            length - total, 0);

        if (sent <= 0)
            return -1;

        total += sent;
    }

    return 0;
}

int recv_line(int sockfd, char *buffer, size_t size)
{
    size_t pos = 0;

    while (pos < size - 1)
    {
        char c;
        ssize_t received = recv(sockfd, &c, 1, 0);

        if (received <= 0)
            return -1;

        buffer[pos++] = c;

        if (c == '\n')
            break;
    }

    buffer[pos] = '\0';
    return 0;
}

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");

    /* AUTH */
    const char *auth_command = "AUTH " AUTH_TOKEN "\n";

    send_all(sockfd, auth_command, strlen(auth_command));

    if (recv_line(sockfd, buffer, sizeof(buffer)) == 0)
        printf("Agent response: %s", buffer);

    /*
     * PUT TEST
     *
     * Sends:
     * PUT filename size
     * followed by file bytes.
     */

    const char *filename = "test_upload.txt";
    const char *file_data =
        "RemoteOps PUT test file.\n"
        "This file is being transferred from Controller to Agent.\n";

    size_t file_size = strlen(file_data);

    snprintf(buffer, sizeof(buffer),
             "PUT %s %zu\n",
             filename, file_size);

    printf("Sending: %s", buffer);

    send_all(sockfd, buffer, strlen(buffer));

    /* Send file contents */
    if (send_all(sockfd, file_data, file_size) < 0)
    {
        printf("PUT transfer failed.\n");
        close(sockfd);
        return 1;
    }

    if (recv_line(sockfd, buffer, sizeof(buffer)) == 0)
        printf("PUT response: %s", buffer);

    /*
     * GET TEST
     */

    snprintf(buffer, sizeof(buffer),
             "GET %s\n",
             filename);

    printf("Sending: %s", buffer);

    send_all(sockfd, buffer, strlen(buffer));

    /*
     * Agent first responds:
     * GET <size>
     */

    if (recv_line(sockfd, buffer, sizeof(buffer)) != 0)
    {
        printf("GET response failed.\n");
        close(sockfd);
        return 1;
    }

    printf("GET header: %s", buffer);

    size_t received_size = 0;

    if (sscanf(buffer, "GET %zu", &received_size) != 1)
    {
        printf("GET failed: %s", buffer);
        close(sockfd);
        return 1;
    }

    FILE *downloaded = fopen("downloaded_test_upload.txt", "wb");

    if (downloaded == NULL)
    {
        perror("fopen");
        close(sockfd);
        return 1;
    }

    size_t total_received = 0;

    while (total_received < received_size)
    {
        char file_buffer[BUFFER_SIZE];

        size_t remaining =
            received_size - total_received;

        size_t chunk =
            remaining < sizeof(file_buffer)
                ? remaining
                : sizeof(file_buffer);

        ssize_t received =
            recv(sockfd, file_buffer, chunk, 0);

        if (received <= 0)
        {
            printf("GET transfer failed.\n");
            fclose(downloaded);
            close(sockfd);
            return 1;
        }

        fwrite(file_buffer, 1, received, downloaded);

        total_received += received;
    }

    fclose(downloaded);

    printf("GET completed: %zu bytes received.\n",
           total_received);

    printf("Downloaded file: downloaded_test_upload.txt\n");

    close(sockfd);

    return 0;
}
