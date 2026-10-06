#include "../common.h"


#define MSG_STR "Message!"


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

    send(client_fd, MSG_STR, sizeof(MSG_STR), 0);

exit:
    close(client_fd);
    return ret;
}