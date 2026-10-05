#include "common.h"


#define BUF_LEN 1024
#define PORT    8080
#define BACKLOG 16


int start_server(void)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;
    int reuse = 1;

    if (setsockopt(listen_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse,
                   sizeof(reuse)) == -1)
    {
        perror("setsockopt");
        ret = EXIT_FAILURE;
        goto exit;
    }

    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(listen_fd,
             (struct sockaddr *)&addr,
             sizeof(addr)) == -1)
    {
        perror("bind");
        ret = EXIT_FAILURE;
        goto exit;
    }

    if (listen(listen_fd, BACKLOG) == -1)
    {
        perror("listen");
        ret = EXIT_FAILURE;
        goto exit;
    }

    printf("Listening on port %d...\n", PORT);

    for (;;)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            (struct sockaddr *)&client_addr,
            &client_addr_len
        );

        if (client_fd == -1)
        {
            if (errno == EINTR)
                continue;

            perror("accept");
            ret = EXIT_FAILURE;
            break;
        }

        char ip[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET,
                      &client_addr.sin_addr,
                      ip,
                      sizeof(ip)) != NULL)
        {
            printf("Client connected: %s:%u\n",
                   ip,
                   ntohs(client_addr.sin_port));
        }

        char buffer[BUF_LEN] = { 0 };

        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);

        if (n > 0)
        {
            buffer[n] = '\0';
            printf("Received: %s\n", buffer);
        }
        else if (n == -1)
        {
            perror("read");
        }

        close(client_fd);
    }

exit:
    close(listen_fd);
    return ret;
}