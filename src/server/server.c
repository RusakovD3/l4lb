#define _GNU_SOURCE

#include "../common.h"
#include <sys/epoll.h>

#define BACKEND_PORT 9000
#define BACKEND_ADDR "127.0.0.1"

#define BUF_LEN 1024
#define BACKLOG 16
#define MAX_EVENTS 16

enum endpoint_type
{
    ENDPOINT_LISTENER,
    ENDPOINT_CLIENT,
    ENDPOINT_BACKEND
};

enum connection_state
{
    CONN_CONNECTING_BACKEND,
    CONN_READY,
    CONN_CLOSED
};

struct connection;

struct endpoint
{
    int fd;
    enum endpoint_type type;
    struct connection *conn;
};

struct connection
{
    struct endpoint client;
    struct endpoint backend;

    enum connection_state state;

    struct connection *next;
};


static void close_endpoint(int epoll_fd, struct endpoint *endpoint)
{
    if (endpoint->fd == -1)
        return;

    if (epoll_fd != -1 &&
        epoll_ctl(epoll_fd,
                  EPOLL_CTL_DEL,
                  endpoint->fd,
                  NULL) == -1 &&
        errno != ENOENT &&
        errno != EBADF)
    {
        perror("epoll_ctl: remove fd");
    }

    if (close(endpoint->fd) == -1)
        perror("close");

    endpoint->fd = -1;
}

static void close_connection(int epoll_fd, struct connection *conn)
{
    if (conn == NULL || conn->state == CONN_CLOSED)
        return;

    conn->state = CONN_CLOSED;

    close_endpoint(epoll_fd, &conn->client);
    close_endpoint(epoll_fd, &conn->backend);
}

static void free_closed_connections(struct connection **connections)
{
    struct connection **it = connections;

    while (*it != NULL)
    {
        struct connection *conn = *it;

        if (conn->state == CONN_CLOSED)
        {
            *it = conn->next;
            free(conn);
            continue;
        }

        it = &conn->next;
    }
}

static void destroy_connections(int epoll_fd,
                                struct connection *connections)
{
    while (connections != NULL)
    {
        struct connection *next = connections->next;

        close_connection(epoll_fd, connections);
        free(connections);

        connections = next;
    }
}

static struct connection *create_connection(int epoll_fd,
                                            int client_fd)
{
    struct connection *conn = calloc(1, sizeof(*conn));
    if (conn == NULL)
    {
        perror("calloc");
        return NULL;
    }

    conn->client.fd = client_fd;
    conn->client.type = ENDPOINT_CLIENT;
    conn->client.conn = conn;

    conn->backend.fd = -1;
    conn->backend.type = ENDPOINT_BACKEND;
    conn->backend.conn = conn;

    conn->state = CONN_CONNECTING_BACKEND;

    struct epoll_event event =
    {
        .events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP,
        .data.ptr = &conn->client
    };

    if (epoll_ctl(epoll_fd,
                  EPOLL_CTL_ADD,
                  client_fd,
                  &event) == -1)
    {
        perror("epoll_ctl: client");
        free(conn);
        return NULL;
    }

    return conn;
}

static int connect_backend(int epoll_fd,
                           struct connection *conn)
{
    int backend_fd = socket(AF_INET,
                            SOCK_STREAM |
                                SOCK_NONBLOCK |
                                SOCK_CLOEXEC,
                            0);
    if (backend_fd == -1)
    {
        perror("socket: backend");
        return -1;
    }

    conn->backend.fd = backend_fd;

    struct sockaddr_in backend_addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(BACKEND_PORT)
    };

    int result = inet_pton(AF_INET,
                           BACKEND_ADDR,
                           &backend_addr.sin_addr);

    if (result != 1)
    {
        if (result == 0)
            fprintf(stderr, "Invalid backend IP address\n");
        else
            perror("inet_pton: backend");

        return -1;
    }

    int connect_result = connect(
        backend_fd,
        (struct sockaddr *) &backend_addr,
        sizeof(backend_addr)
    );

    uint32_t backend_events =
        EPOLLRDHUP |
        EPOLLERR |
        EPOLLHUP;

    if (connect_result == 0)
    {
        conn->state = CONN_READY;

        printf("Backend connected immediately, fd=%d\n",
               backend_fd);
    }
    else
    {
        if (errno != EINPROGRESS)
        {
            perror("connect: backend");
            return -1;
        }

        conn->state = CONN_CONNECTING_BACKEND;

        backend_events |= EPOLLOUT;

        printf("Backend connection in progress, fd=%d\n",
               backend_fd);
    }

    struct epoll_event event =
    {
        .events = backend_events,
        .data.ptr = &conn->backend
    };

    if (epoll_ctl(epoll_fd,
                  EPOLL_CTL_ADD,
                  backend_fd,
                  &event) == -1)
    {
        perror("epoll_ctl: backend");
        return -1;
    }

    return 0;
}

static int finish_backend_connect(int epoll_fd,
                                  struct connection *conn)
{
    int error = 0;
    socklen_t error_len = sizeof(error);

    if (getsockopt(conn->backend.fd,
                   SOL_SOCKET,
                   SO_ERROR,
                   &error,
                   &error_len) == -1)
    {
        perror("getsockopt: SO_ERROR");
        return -1;
    }

    if (error != 0)
    {
        errno = error;
        perror("connect: backend");
        return -1;
    }

    struct epoll_event event =
    {
        .events = EPOLLRDHUP |
                  EPOLLERR |
                  EPOLLHUP,

        .data.ptr = &conn->backend
    };

    if (epoll_ctl(epoll_fd,
                  EPOLL_CTL_MOD,
                  conn->backend.fd,
                  &event) == -1)
    {
        perror("epoll_ctl: backend connected");
        return -1;
    }

    conn->state = CONN_READY;

    printf("Backend connected, fd=%d\n",
           conn->backend.fd);

    return 0;
}

static void accept_clients(int epoll_fd,
                           int listen_fd,
                           struct connection **connections)
{
    for (;;)
    {
        int client_fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd == -1)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            perror("accept4");
            break;
        }

        printf("Accepted client, fd=%d\n", client_fd);

        struct connection *conn = create_connection(epoll_fd, client_fd);

        if (conn == NULL)
        {
            if (close(client_fd) == -1)
                perror("close: client");

            continue;
        }

        conn->next = *connections;
        *connections = conn;

        if (connect_backend(epoll_fd, conn) == -1)
            close_connection(epoll_fd, conn);
    }
}

static int read_client(struct endpoint *endpoint)
{
    char buffer[BUF_LEN];

    for (;;)
    {
        ssize_t n = read(endpoint->fd,
                         buffer,
                         sizeof(buffer));

        if (n > 0)
        {
            printf("Received from client: %.*s\n", (int)n, buffer);
            continue;
        }

        if (n == 0)
        {
            printf("Client closed connection, fd=%d\n", endpoint->fd);
            return -1;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;

        perror("read: client");
        return -1;
    }
}

static void handle_connection_event(int epoll_fd,
                                    struct endpoint *endpoint,
                                    uint32_t events)
{
    struct connection *conn = endpoint->conn;
    if (conn == NULL || conn->state == CONN_CLOSED)
        return;

    if (endpoint->type == ENDPOINT_BACKEND && conn->state == CONN_CONNECTING_BACKEND)
    {
        if (events & (EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        {
            if (finish_backend_connect(epoll_fd, conn) == -1)
            {
                close_connection(epoll_fd, conn);
                return;
            }
        }
    }

    if (endpoint->type == ENDPOINT_CLIENT && events & EPOLLIN)
    {
        if (read_client(endpoint) == -1)
        {
            close_connection(epoll_fd, conn);
            return;
        }
    }

    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
    {
        if (endpoint->type == ENDPOINT_CLIENT)
            printf("Client disconnected, fd=%d\n", endpoint->fd);
        else
            printf("Backend disconnected, fd=%d\n", endpoint->fd);

        close_connection(epoll_fd, conn);
    }
}

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

    struct connection *connections = NULL;

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

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1)
    {
        perror("epoll_create1");
        ret = EXIT_FAILURE;
        goto exit;
    }

    struct endpoint listener =
    {
        .fd = listen_fd,
        .type = ENDPOINT_LISTENER,
        .conn = NULL
    };

    struct epoll_event event =
    {
        .events = EPOLLIN,
        .data.ptr = &listener
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

    printf("Listening on %s:%d...\n",
           ADDR_SRV,
           PORT_SRV);

    printf("Backend: %s:%d\n",
           BACKEND_ADDR,
           BACKEND_PORT);

    struct epoll_event events[MAX_EVENTS];

    for (;;)
    {
        int count = epoll_wait(epoll_fd,
                               events,
                               MAX_EVENTS,
                               -1);

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
            struct endpoint *endpoint = events[i].data.ptr;

            if (endpoint->type == ENDPOINT_LISTENER)
            {
                if (events[i].events & (EPOLLERR | EPOLLHUP))
                {
                    fprintf(stderr, "Listener failure\n");
                    ret = EXIT_FAILURE;
                    goto exit;
                }

                if (events[i].events & EPOLLIN)
                {
                    accept_clients(epoll_fd, listen_fd, &connections);
                }

                continue;
            }

            handle_connection_event(epoll_fd, endpoint, events[i].events);
        }

        free_closed_connections(&connections);
    }

exit:
    destroy_connections(epoll_fd, connections);

    if (epoll_fd != -1)
        close(epoll_fd);

    close(listen_fd);

    return ret;
}
