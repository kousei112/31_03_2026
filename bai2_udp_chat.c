
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>

#define BUF_SIZE     1024
#define PROMPT       "> "


static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { perror("fcntl F_GETFL"); exit(1); }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL"); exit(1);
    }
}

static void print_time(void)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char tbuf[16];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);
    printf("[%s] ", tbuf);
}

int main(int argc, char *argv[])
{

    if (argc != 4) {
        fprintf(stderr,
            "Cach dung: %s <port_s> <ip_d> <port_d>\n"
            "  port_s : cong lang nghe cua ung dung nay\n"
            "  ip_d   : dia chi IP cua ung dung dich\n"
            "  port_d : cong cua ung dung dich\n"
            "\nVi du:\n"
            "  Terminal A: %s 5000 127.0.0.1 5001\n"
            "  Terminal B: %s 5001 127.0.0.1 5000\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    int   port_s = atoi(argv[1]);
    char *ip_d   = argv[2];
    int   port_d = atoi(argv[3]);

    if (port_s <= 0 || port_s > 65535 ||
        port_d <= 0 || port_d > 65535) {
        fprintf(stderr, "Cong khong hop le (phai trong khoang 1-65535)\n");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port        = htons(port_s);

    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind"); close(sockfd); return 1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(port_d);
    if (inet_pton(AF_INET, ip_d, &dest_addr.sin_addr) <= 0) {
        fprintf(stderr, "Dia chi IP khong hop le: %s\n", ip_d);
        close(sockfd); return 1;
    }

    set_nonblocking(STDIN_FILENO);

    printf("=== UDP Chat Non-blocking ===\n");
    printf("Lang nghe  : cong %d\n", port_s);
    printf("Gui den    : %s:%d\n", ip_d, port_d);
    printf("Go chu va nhan Enter de gui. Nhan Ctrl+C de thoat.\n\n");
    printf(PROMPT); fflush(stdout);

    struct pollfd fds[2];
    fds[0].fd     = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd     = sockfd;
    fds[1].events = POLLIN;

    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    int  send_pos = 0; 
    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }
        if (fds[0].revents & POLLIN) {
            char ch;
            while (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == '\n') {
                    send_buf[send_pos] = '\0';

                    if (send_pos > 0) {
                        int sent = sendto(sockfd,
                                          send_buf, send_pos, 0,
                                          (struct sockaddr*)&dest_addr,
                                          sizeof(dest_addr));
                        if (sent < 0) {
                            perror("sendto");
                        } else {
                            print_time();
                            printf("(ban) %s\n", send_buf);
                        }
                    }
                    send_pos = 0;
                    printf(PROMPT); fflush(stdout);
                } else if (ch == 127 || ch == '\b') {
                    if (send_pos > 0) {
                        send_pos--;
                        printf("\b \b"); fflush(stdout);
                    }
                } else if (send_pos < BUF_SIZE - 1) {

                    send_buf[send_pos++] = ch;
                    putchar(ch); fflush(stdout);
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            struct sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);

            int n = recvfrom(sockfd,
                             recv_buf, sizeof(recv_buf) - 1, 0,
                             (struct sockaddr*)&sender_addr, &sender_len);
            if (n < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("recvfrom");
            } else {
                recv_buf[n] = '\0';
                printf("\r");                 
                printf("\033[2K");            

                print_time();
                printf("[%s:%d] %s\n",
                       inet_ntoa(sender_addr.sin_addr),
                       ntohs(sender_addr.sin_port),
                       recv_buf);
                send_buf[send_pos] = '\0';
                printf(PROMPT "%s", send_buf);
                fflush(stdout);
            }
        }


        if (fds[0].revents & (POLLHUP | POLLERR)) {
            printf("\nThoat chuong trinh.\n");
            break;
        }
    }

    close(sockfd);
    return 0;
}

