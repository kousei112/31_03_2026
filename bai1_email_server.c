/*
 * Bai 1: Non-blocking TCP Server - Tra ve email sinh vien DHBK Ha Noi
 * 
 * Su dung ky thuat non-blocking socket + poll() de xu ly nhieu client
 * dong thoi trong mot tien trinh duy nhat.
 *
 * Cach chay:
 *   Bien dich: gcc -o email_server bai1_email_server.c
 *   Chay server: ./email_server
 *   Chay client (terminal khac): telnet 127.0.0.1 8080
 *                             hoac: nc 127.0.0.1 8080
 *
 * Minh hoa: Mo nhieu terminal, chay nhieu client cung luc.
 * Server se xu ly tung buoc trang thai cua tung client (state machine).
 */

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

/* ===== Cau hinh ===== */
#define SERVER_PORT   8080
#define MAX_CLIENTS   64
#define BUF_SIZE      256

/*
 * Trang thai cua moi client (state machine)
 * De theo doi client dang o buoc nao trong quy trinh hoi dap
 */
typedef enum {
    STATE_WAIT_NAME = 0,   /* Cho client nhap Ho ten    */
    STATE_WAIT_MSSV,       /* Cho client nhap MSSV      */
    STATE_DONE             /* Da xu ly xong, cho dong   */
} ClientState;

/* Thong tin luu tru cho tung client */
typedef struct {
    int          fd;                    /* Socket file descriptor   */
    ClientState  state;                 /* Trang thai hien tai      */
    char         name[BUF_SIZE];        /* Ho ten sinh vien         */
    char         mssv[BUF_SIZE];        /* Ma so sinh vien          */
    char         recv_buf[BUF_SIZE];    /* Buffer nhan du lieu       */
    int          recv_len;              /* So byte da nhan           */
} ClientInfo;

/* ===== Chuyen socket sang non-blocking ===== */
static void set_nonblocking(int fd)
{
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

/*
 * Tao dia chi email tu Ho ten va MSSV
 * Quy tac DHBK: <vietkhong_dau>.<mssv>@sis.hust.edu.vn
 * Vi du: Nguyen Van An - 20210001 => nguyenvanan.20210001@sis.hust.edu.vn
 */
static void to_lowercase_nospace(const char *in, char *out, int maxlen)
{
    int j = 0;
    for (int i = 0; in[i] && j < maxlen - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ' || c == '\t') continue;  /* bo khoang trang */
        out[j++] = tolower(c);
    }
    out[j] = '\0';
}

static void build_email(const char *name, const char *mssv, char *email_out, int maxlen)
{
    char name_part[BUF_SIZE];
    to_lowercase_nospace(name, name_part, sizeof(name_part));
    snprintf(email_out, maxlen, "%s.%s@sis.hust.edu.vn", name_part, mssv);
}

/* ===== Gui chuoi den client (xu ly EINTR / partial send) ===== */
static void send_str(int fd, const char *msg)
{
    int total = strlen(msg);
    int sent  = 0;
    while (sent < total) {
        int r = send(fd, msg + sent, total - sent, 0);
        if (r <= 0) break;
        sent += r;
    }
}

/*
 * Xu ly du lieu nhan duoc tu mot client.
 * Ham nay duoc goi moi khi poll() bao co du lieu tren socket client.
 * Tra ve: 0 - tiep tuc, -1 - dong ket noi client nay
 */
static int handle_client_data(ClientInfo *c)
{
    /* Nhan du lieu vao phan con lai cua recv_buf */
    int n = recv(c->fd,
                 c->recv_buf + c->recv_len,
                 sizeof(c->recv_buf) - c->recv_len - 1,
                 0);

    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;   /* Chua co du lieu, binh thuong */
        perror("recv");
        return -1;
    }
    if (n == 0) {
        printf("[Server] Client fd=%d ngat ket noi.\n", c->fd);
        return -1;
    }

    c->recv_len += n;
    c->recv_buf[c->recv_len] = '\0';

    /* Kiem tra xem da nhan du mot dong (ket thuc bang '\n') chua */
    char *nl = strchr(c->recv_buf, '\n');
    if (!nl) return 0;   /* Chua du mot dong, doi them */

    /* Cat lay mot dong du lieu */
    *nl = '\0';
    trim_newline(c->recv_buf);

    char line[BUF_SIZE];
    strncpy(line, c->recv_buf, sizeof(line) - 1);
    line[sizeof(line)-1] = '\0';

    /* Dich chuyen phan con lai trong buffer (neu co nhieu dong) */
    int used = (nl - c->recv_buf) + 1;
    memmove(c->recv_buf, c->recv_buf + used, c->recv_len - used);
    c->recv_len -= used;

    /* Xu ly theo trang thai hien tai */
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

        /* Tao email va gui ket qua */
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
        return -1;   /* Dong ket noi sau khi hoan tat */

    default:
        return -1;
    }

    return 0;
}

/* ===== Ham chinh ===== */
int main(void)
{
    /* --- Tao socket lang nghe --- */
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); exit(1); }

    /* Cho phep tai su dung cong ngay sau khi tat server */
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

    /* Chuyen sang non-blocking */
    set_nonblocking(listener);

    printf("=== Server Email DHBK Khoi dong tren cong %d ===\n", SERVER_PORT);
    printf("Su dung: telnet 127.0.0.1 %d\n\n", SERVER_PORT);

    /* --- Khoi tao mang poll fd --- */
    struct pollfd fds[MAX_CLIENTS + 1];
    ClientInfo    clients[MAX_CLIENTS];
    int           nfds = 0;

    /* Vi tri 0 danh cho listener */
    fds[0].fd     = listener;
    fds[0].events = POLLIN;
    nfds = 1;

    /* Khoi tao tat ca client slot */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&clients[i], 0, sizeof(ClientInfo));
        clients[i].fd = -1;
    }

    /* =============================================
     * Vong lap chinh: Su dung poll() de theo doi
     * ca socket listener lan tat ca client socket
     * ============================================= */
    while (1) {
        int ret = poll(fds, nfds, -1);   /* Cho vo han */
        if (ret < 0) {
            perror("poll"); break;
        }

        /* --- Kiem tra listener: co ket noi moi? --- */
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int new_fd = accept(listener,
                                (struct sockaddr*)&cli_addr, &cli_len);
            if (new_fd < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("accept");
            } else if (nfds - 1 >= MAX_CLIENTS) {
                /* Qua nhieu client */
                send_str(new_fd, "Server qua tai. Vui long thu lai sau.\n");
                close(new_fd);
            } else {
                /* Chuyen socket moi sang non-blocking */
                set_nonblocking(new_fd);

                /* Tim slot trong */
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) { slot = i; break; }
                }

                /* Luu thong tin client */
                memset(&clients[slot], 0, sizeof(ClientInfo));
                clients[slot].fd    = new_fd;
                clients[slot].state = STATE_WAIT_NAME;

                /* Them vao mang poll */
                fds[nfds].fd     = new_fd;
                fds[nfds].events = POLLIN;
                nfds++;

                printf("[Server] Client moi ket noi: fd=%d, ip=%s\n",
                       new_fd, inet_ntoa(cli_addr.sin_addr));

                /* Chao hoi client */
                send_str(new_fd,
                    "=== Server Email Sinh Vien DHBK Ha Noi ===\n"
                    "Nhap Ho ten: ");
            }
        }

        /* --- Kiem tra tung client socket --- */
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLERR)))
                continue;

            /* Tim ClientInfo tuong ung */
            int slot = -1;
            for (int k = 0; k < MAX_CLIENTS; k++) {
                if (clients[k].fd == fds[i].fd) { slot = k; break; }
            }
            if (slot == -1) continue;

            int r = handle_client_data(&clients[slot]);
            if (r < 0) {
                /* Dong ket noi client nay */
                close(fds[i].fd);
                clients[slot].fd = -1;

                /* Xoa khoi mang poll bang cach dich mang */
                fds[i] = fds[nfds - 1];
                nfds--;
                i--;   /* Kiem tra lai vi tri nay */
            }
        }
    }

    close(listener);
    return 0;
}