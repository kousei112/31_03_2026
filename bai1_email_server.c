
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>

#define SERVER_PORT   8080
#define MAX_CLIENTS   64
#define BUF_SIZE      256

typedef enum {
    STATE_WAIT_NAME = 0,   
    STATE_WAIT_MSSV,       
    STATE_DONE             
} ClientState;

typedef struct {
    int          fd;               
    ClientState  state;                 
    char         name[BUF_SIZE];        
    char         mssv[BUF_SIZE];        
    char         recv_buf[BUF_SIZE];   
    int          recv_len;              
} ClientInfo;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ===== Xoa ky tu xuong dong cuoi chuoi ===== */
static void trim_newline(char *s)
{
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r'))
        s[--len] = '\0';
}

static char to_lower_ascii(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int split_words(const char *input, char words[][BUF_SIZE], int max_words) {
    int count = 0;
    int i     = 0;
    int len   = strlen(input);

    while (i < len && count < max_words) {
        while (i < len && (input[i] == ' ' || input[i] == '\t')) i++;
        if (i >= len) break;
        int j = 0;
        while (i < len && input[i] != ' ' && input[i] != '\t' && j < BUF_SIZE - 1)
            words[count][j++] = input[i++];
        words[count][j] = '\0';
        count++;
    }
    return count;
}

static void build_email(const char *name, const char *mssv, char *email_out, int maxlen) {
    char words[16][BUF_SIZE];
    int  nwords = split_words(name, words, 16);

    if (nwords == 0) {
        snprintf(email_out, maxlen, "unknown.%s@sis.hust.edu.vn", mssv);
        return;
    }
    char first_name[BUF_SIZE];
    int k = 0;
    for (int i = 0; words[nwords-1][i] && k < BUF_SIZE-1; i++)
        first_name[k++] = to_lower_ascii((unsigned char)words[nwords-1][i]);
    first_name[k] = '\0';
    char initials[32];
    int  m = 0;
    for (int w = 0; w < nwords - 1 && m < 31; w++)
        if (words[w][0] != '\0')
            initials[m++] = to_lower_ascii((unsigned char)words[w][0]);
    initials[m] = '\0';
    const char *mssv_short = (strlen(mssv) > 2) ? mssv + 2 : mssv;
    snprintf(email_out, maxlen, "%s.%s%s@sis.hust.edu.vn",
             first_name, initials, mssv_short);
}

static void send_str(int fd, const char *msg) {
    int total = strlen(msg);
    int sent  = 0;
    while (sent < total) {
        int r = send(fd, msg + sent, total - sent, 0);
        if (r <= 0) break;
        sent += r;
    }
}

static int handle_client_data(ClientInfo *c) {
    int n = recv(c->fd,
                 c->recv_buf + c->recv_len,
                 sizeof(c->recv_buf) - c->recv_len - 1,
                 0);

    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;  
        perror("recv");
        return -1;
    }
    if (n == 0) {
        printf("[Server] Client fd=%d ngat ket noi.\n", c->fd);
        return -1;
    }

    c->recv_len += n;
    c->recv_buf[c->recv_len] = '\0';
    char *nl = strchr(c->recv_buf, '\n');
    if (!nl) return 0;   

    *nl = '\0';
    trim_newline(c->recv_buf);
    char line[BUF_SIZE];
    strncpy(line, c->recv_buf, sizeof(line) - 1);
    line[sizeof(line)-1] = '\0';
    int used = (nl - c->recv_buf) + 1;
    memmove(c->recv_buf, c->recv_buf + used, c->recv_len - used);
    c->recv_len -= used;
    switch (c->state) {

    case STATE_WAIT_NAME:
        if (strlen(line) == 0) {
            send_str(c->fd, "Ho ten khong duoc de trong. Nhap lai Ho ten: ");
            return 0;
        }
        strncpy(c->name, line, sizeof(c->name) - 1);
        printf("[Server] Client fd=%d - Ho ten: %s\n", c->fd, c->name);
        c->state = STATE_WAIT_MSSV;
        send_str(c->fd, "Nhap MSSV: ");
        break;

    case STATE_WAIT_MSSV:
        if (strlen(line) == 0) {
            send_str(c->fd, "MSSV khong duoc de trong. Nhap lai MSSV: ");
            return 0;
        }
        strncpy(c->mssv, line, sizeof(c->mssv) - 1);
        printf("[Server] Client fd=%d - MSSV: %s\n", c->fd, c->mssv);
        char email[BUF_SIZE * 2];
        build_email(c->name, c->mssv, email, sizeof(email));

        char reply[BUF_SIZE * 6];
        snprintf(reply, sizeof(reply),
                 "\n========================================\n"
                 " Ho ten : %s\n"
                 " MSSV   : %s\n"
                 " Email  : %s\n"
                 "========================================\n"
                 "Ket noi se dong. Tam biet!\n",
                 c->name, c->mssv, email);
        send_str(c->fd, reply);
        printf("[Server] Da gui email %s cho client fd=%d\n", email, c->fd);
        c->state = STATE_DONE;
        return -1;   

    default:
        return -1;
    }

    return 0;
}


int main() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listener, 10) < 0) {
        perror("listen"); exit(1);
    }

    set_nonblocking(listener);

    printf("=== Server Email DHBK Khoi dong tren cong %d ===\n", SERVER_PORT);
    printf("Su dung: telnet 127.0.0.1 %d\n\n", SERVER_PORT);

    struct pollfd fds[MAX_CLIENTS + 1];
    ClientInfo    clients[MAX_CLIENTS];
    int           nfds = 0;

    fds[0].fd     = listener;
    fds[0].events = POLLIN;
    nfds = 1;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&clients[i], 0, sizeof(ClientInfo));
        clients[i].fd = -1;
    }
    while (1) {
        int ret = poll(fds, nfds, -1);  
        if (ret < 0) {
            perror("poll"); break;
        }
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int new_fd = accept(listener,
                                (struct sockaddr*)&cli_addr, &cli_len);
            if (new_fd < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("accept");
            } else if (nfds - 1 >= MAX_CLIENTS) {
                send_str(new_fd, "Server qua tai. Vui long thu lai sau.\n");
                close(new_fd);
            } else {
                set_nonblocking(new_fd);

                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) { slot = i; break; }
                }

                memset(&clients[slot], 0, sizeof(ClientInfo));
                clients[slot].fd    = new_fd;
                clients[slot].state = STATE_WAIT_NAME;

                fds[nfds].fd     = new_fd;
                fds[nfds].events = POLLIN;
                nfds++;

                printf("[Server] Client moi ket noi: fd=%d, ip=%s\n",
                       new_fd, inet_ntoa(cli_addr.sin_addr));

                send_str(new_fd,
                    "=== Server Email Sinh Vien DHBK Ha Noi ===\n"
                    "Nhap Ho ten: ");
            }
        }

        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLERR)))
                continue;

            int slot = -1;
            for (int k = 0; k < MAX_CLIENTS; k++) {
                if (clients[k].fd == fds[i].fd) { slot = k; break; }
            }
            if (slot == -1) continue;

            int r = handle_client_data(&clients[slot]);
            if (r < 0) {
                close(fds[i].fd);
                clients[slot].fd = -1;

                fds[i] = fds[nfds - 1];
                nfds--;
                i--; 
            }
        }
    }

    close(listener);
    return 0;
}