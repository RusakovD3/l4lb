#include "../common.h"


#define MSG_STR "Message!"
#define RESPONSE_BUF_LEN 1024

int main(void)
{
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;

    struct sockaddr_in server_addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(PORT_SRV),
    };

    int result = inet_pton(AF_INET,
                           ADDR_SRV,
                           &server_addr.sin_addr);

    if (result != 1)
    {
        if (result == 0)
            fprintf(stderr, "Invalid server IP address\n");
        else
            perror("inet_pton");

        ret = EXIT_FAILURE;
        goto exit;
    }

    if(connect(client_fd,
               (struct sockaddr *) &server_addr,
               sizeof(server_addr)) == -1)
    {
        perror("connect");
        ret = EXIT_FAILURE;
        goto exit;
    }

    ssize_t n = send(client_fd, MSG_STR, sizeof(MSG_STR), 0);
    if (n == -1)
    {
        perror("send");
        ret = EXIT_FAILURE;
        goto exit;
    }

    printf("Sent \'" MSG_STR "\" to %s:%d\n", ADDR_SRV, PORT_SRV);

    char buffer[RESPONSE_BUF_LEN];
    n = recv(client_fd, buffer, sizeof(buffer), 0);
    if (n == -1)
    {
        perror("recv");
        ret = EXIT_FAILURE;
        goto exit;
    }

    if (n == 0)
    {
        fprintf(stderr, "Server closed connection without response\n");
        ret = EXIT_FAILURE;
        goto exit;
    }

    printf("Received %zd bytes from server: ", n);

    if (fwrite(buffer, 1, (size_t) n, stdout) != (size_t)n)
    {
        perror("fwrite");
        ret = EXIT_FAILURE;
        goto exit;
    }

    putchar('\n');

exit:
    close(client_fd);
    return ret;
}