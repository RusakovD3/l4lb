#define _GNU_SOURCE

#include "../common.h"
#include <sys/epoll.h>


#define BUF_LEN 1024
#define BACKLOG 16
#define MAX_EVENTS 16

int main(void)
{
    int listen_fd = socket(AF_INET,
                           SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                           0);
    if (listen_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;
    int epoll_fd = -1;
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
        .sin_port = htons(PORT_SRV),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        // .sin_addr.s_addr = htonl(INADDR_ANY) // 0.0.0.0
    };

    if (bind(listen_fd,
             (struct sockaddr *) &addr,
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

    printf("Listening on port %d...\n", PORT_SRV);

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1)
    {
        perror("epoll_create1");
        ret = EXIT_FAILURE;
        goto exit;
    }

    struct epoll_event event = {
        .events = EPOLLIN,
        .data.fd = listen_fd
    };

    if (epoll_ctl(epoll_fd,
                  EPOLL_CTL_ADD,
                  listen_fd,
                  &event) == -1)
    {
        perror("epoll_ctl: listener");
        ret = EXIT_FAILURE;
        goto exit;
    }

    struct epoll_event events[MAX_EVENTS];

    for (;;)
    {
        int count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (count == -1)
        {
            if (errno == EINTR)
                continue;

            perror("epoll_wait");
            ret = EXIT_FAILURE;
            goto exit;
        }

        for (int i = 0; i < count; ++i)
        {
            if (events[i].data.fd == listen_fd)
            {
                for (;;)
                {
                    int client_fd = accept4(
                        listen_fd,
                        NULL,
                        NULL,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if (client_fd == -1)
                    {
                        if (errno == EINTR)
                            continue;

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        perror("accept4");
                        ret = EXIT_FAILURE;
                        goto exit;
                    }

                    printf("Accepted client, fd=%d\n", client_fd);

                    struct epoll_event client_event = {
                        .events = EPOLLIN,
                        .data.fd = client_fd
                    };

                    if (epoll_ctl(epoll_fd,
                                EPOLL_CTL_ADD,
                                client_fd,
                                &client_event) == -1)
                    {
                        perror("epoll_ctl: client");
                        close(client_fd);
                        continue;
                    }
                }
            }
            else
            {
                int fd = events[i].data.fd;
                char buffer[BUF_LEN];

                for (;;)
                {
                    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);

                    if (n > 0)
                    {
                        buffer[n] = '\0';
                        printf("Received from fd=%d: %s\n", fd, buffer);
                        continue;
                    }

                    if (n == -1)
                    {
                        if (errno == EINTR)
                            continue;

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        perror("read");
                    }
                    else
                    {
                        /* n == 0: клиент закончил передачу данных. */
                        printf("Client finished, fd=%d\n", fd);
                    }

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
                        perror("epoll_ctl: remove client");

                    close(fd);
                    break;
                }
            }
        }
    }

exit:
    if (epoll_fd != -1)
        close(epoll_fd);

    close(listen_fd);
    return ret;
}
