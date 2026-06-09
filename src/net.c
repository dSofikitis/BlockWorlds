#include "net.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  typedef int    sock_len_t;
  #define BAD_SOCK     INVALID_SOCKET
  #define CLOSESOCK    closesocket
  static int sock_wouldblock(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
  static void sock_set_nonblock(sock_t s) { u_long m = 1; ioctlsocket(s, (long)FIONBIO, &m); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <sys/select.h>
  typedef int    sock_t;
  typedef size_t sock_len_t;
  #define BAD_SOCK     (-1)
  #define CLOSESOCK    close
  static int sock_wouldblock(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
  static void sock_set_nonblock(sock_t s) { int f = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, f | O_NONBLOCK); }
#endif

#define RECV_BUF   8192
#define INGRESS_Q  256

typedef struct {
    int     active;
    sock_t  s;
    int32_t id;
    uint8_t buf[RECV_BUF];
    size_t  len;
} peer_t;

struct net_s {
    int     is_host;
    sock_t  listen_s;
    peer_t  peers[NET_MAX_PEERS];
    pthread_mutex_t lock;
    pthread_t       thread;
    volatile int    running;
    int32_t next_id;

    net_msg_t q[INGRESS_Q];
    int32_t   q_from[INGRESS_Q];
    int       q_head, q_tail;
};

typedef struct { uint8_t *d; size_t cap, n; } wbuf_t;
static void wput(wbuf_t *b, const void *src, size_t k) {
    if (b->n + k <= b->cap) memcpy(b->d + b->n, src, k);
    b->n += k;
}
static void w8 (wbuf_t *b, uint8_t v)  { wput(b, &v, 1); }
static void w16(wbuf_t *b, uint16_t v) { uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; wput(b, t, 2); }
static void w32(wbuf_t *b, uint32_t v) { uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; wput(b, t, 4); }
static void wf (wbuf_t *b, float f)    { uint32_t u; memcpy(&u, &f, 4); w32(b, u); }

typedef struct { const uint8_t *d; size_t n, i; } rbuf_t;
static uint8_t  r8 (rbuf_t *b) { return b->i < b->n ? b->d[b->i++] : 0; }
static uint16_t r16(rbuf_t *b) { uint16_t v = r8(b); v |= (uint16_t)((uint16_t)r8(b) << 8); return v; }
static uint32_t r32(rbuf_t *b) { uint32_t v = r8(b); v |= (uint32_t)r8(b) << 8; v |= (uint32_t)r8(b) << 16; v |= (uint32_t)r8(b) << 24; return v; }
static float    rf (rbuf_t *b) { uint32_t u = r32(b); float f; memcpy(&f, &u, 4); return f; }

static void msg_serialize(const net_msg_t *m, wbuf_t *b) {
    w8(b, m->op);
    w32(b, (uint32_t)m->player_id);
    wf(b, m->x); wf(b, m->y); wf(b, m->z); wf(b, m->yaw); wf(b, m->pitch);
    w32(b, (uint32_t)m->bx); w32(b, (uint32_t)m->by); w32(b, (uint32_t)m->bz);
    w16(b, m->block);
    w32(b, m->seed);
    w32(b, (uint32_t)m->gamemode);
    wf(b, m->world_time);
    uint16_t tl = 0; while (tl < NET_CHAT_MAX && m->text[tl]) tl++;
    w16(b, tl); wput(b, m->text, tl);
    uint16_t bl = m->blob_len > NET_BLOB_MAX ? NET_BLOB_MAX : m->blob_len;
    w16(b, bl); wput(b, m->blob, bl);
}
static void msg_deserialize(rbuf_t *b, net_msg_t *m) {
    memset(m, 0, sizeof *m);
    m->op = r8(b);
    m->player_id = (int32_t)r32(b);
    m->x = rf(b); m->y = rf(b); m->z = rf(b); m->yaw = rf(b); m->pitch = rf(b);
    m->bx = (int32_t)r32(b); m->by = (int32_t)r32(b); m->bz = (int32_t)r32(b);
    m->block = r16(b);
    m->seed = r32(b);
    m->gamemode = (int32_t)r32(b);
    m->world_time = rf(b);
    uint16_t tl = r16(b);
    if (tl >= NET_CHAT_MAX) tl = NET_CHAT_MAX - 1;
    for (uint16_t i = 0; i < tl; i++) m->text[i] = (char)r8(b);
    m->text[tl] = '\0';
    uint16_t bl = r16(b);
    if (bl > NET_BLOB_MAX) bl = NET_BLOB_MAX;
    m->blob_len = bl;
    for (uint16_t i = 0; i < bl; i++) m->blob[i] = r8(b);
}

static void send_all(sock_t s, const uint8_t *data, size_t len) {
    size_t off = 0; int spins = 0;
    while (off < len && spins < 100000) {
        int r = (int)send(s, (const char *)data + off, (sock_len_t)(len - off), 0);
        if (r > 0) { off += (size_t)r; spins = 0; }
        else if (r < 0 && sock_wouldblock()) { spins++; }
        else break;
    }
}

static void frame_and_send(sock_t s, const net_msg_t *m) {
    uint8_t body[2 + NET_CHAT_MAX + NET_BLOB_MAX + 64];
    wbuf_t wb = { body, sizeof body, 0 };
    msg_serialize(m, &wb);
    size_t blen = wb.n > sizeof body ? sizeof body : wb.n;
    uint8_t hdr[2] = { (uint8_t)blen, (uint8_t)(blen >> 8) };
    send_all(s, hdr, 2);
    send_all(s, body, blen);
}

static void q_push(net_t *n, const net_msg_t *m, int32_t from) {
    int next = (n->q_tail + 1) % INGRESS_Q;
    if (next == n->q_head) return;
    n->q[n->q_tail] = *m;
    n->q_from[n->q_tail] = from;
    n->q_tail = next;
}

static void peer_extract(net_t *n, peer_t *p) {
    size_t off = 0;
    while (p->len - off >= 2) {
        size_t blen = (size_t)p->buf[off] | ((size_t)p->buf[off + 1] << 8);
        if (p->len - off < 2 + blen) break;
        rbuf_t rb = { p->buf + off + 2, blen, 0 };
        net_msg_t m;
        msg_deserialize(&rb, &m);
        q_push(n, &m, p->id);
        off += 2 + blen;
    }
    if (off > 0) {
        memmove(p->buf, p->buf + off, p->len - off);
        p->len -= off;
    }
}

static void peer_drop(net_t *n, int slot) {
    peer_t *p = &n->peers[slot];
    if (!p->active) return;
    if (n->is_host) {
        net_msg_t dm; memset(&dm, 0, sizeof dm);
        dm.op = NET_DEL_PLAYER; dm.player_id = p->id;
        q_push(n, &dm, p->id);
    }
    CLOSESOCK(p->s);
    p->active = 0; p->len = 0; p->s = BAD_SOCK;
}

static void *io_thread(void *ud) {
    net_t *n = (net_t *)ud;
    while (n->running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        sock_t maxfd = 0;
        pthread_mutex_lock(&n->lock);
        if (n->is_host && n->listen_s != BAD_SOCK) {
            FD_SET(n->listen_s, &rfds);
            if (n->listen_s > maxfd) maxfd = n->listen_s;
        }
        for (int i = 0; i < NET_MAX_PEERS; i++)
            if (n->peers[i].active) {
                FD_SET(n->peers[i].s, &rfds);
                if (n->peers[i].s > maxfd) maxfd = n->peers[i].s;
            }
        pthread_mutex_unlock(&n->lock);

        struct timeval tv = { 0, 50000 };
        int sel = select((int)maxfd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        pthread_mutex_lock(&n->lock);
        if (n->is_host && n->listen_s != BAD_SOCK && FD_ISSET(n->listen_s, &rfds)) {
            sock_t c = accept(n->listen_s, NULL, NULL);
            if (c != BAD_SOCK) {
                int slot = -1;
                for (int i = 0; i < NET_MAX_PEERS; i++) if (!n->peers[i].active) { slot = i; break; }
                if (slot < 0) { CLOSESOCK(c); }
                else {
                    sock_set_nonblock(c);
                    int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
                    n->peers[slot].active = 1;
                    n->peers[slot].s = c;
                    n->peers[slot].len = 0;
                    n->peers[slot].id = ++n->next_id;
                }
            }
        }
        for (int i = 0; i < NET_MAX_PEERS; i++) {
            peer_t *p = &n->peers[i];
            if (!p->active || !FD_ISSET(p->s, &rfds)) continue;
            if (p->len >= RECV_BUF) { peer_drop(n, i); continue; }
            int r = (int)recv(p->s, (char *)p->buf + p->len, (sock_len_t)(RECV_BUF - p->len), 0);
            if (r > 0) { p->len += (size_t)r; peer_extract(n, p); }
            else if (r == 0) { peer_drop(n, i); }
            else if (!sock_wouldblock()) { peer_drop(n, i); }
        }
        pthread_mutex_unlock(&n->lock);
    }
    return NULL;
}

int net_global_init(void) {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}
void net_global_shutdown(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static net_t *net_alloc(int is_host) {
    net_t *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->is_host = is_host;
    n->listen_s = BAD_SOCK;
    for (int i = 0; i < NET_MAX_PEERS; i++) n->peers[i].s = BAD_SOCK;
    pthread_mutex_init(&n->lock, NULL);
    return n;
}

net_t *net_host_start(int port) {
    net_t *n = net_alloc(1);
    if (!n) return NULL;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) { free(n); return NULL; }
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, NET_MAX_PEERS) != 0) {
        CLOSESOCK(s); pthread_mutex_destroy(&n->lock); free(n); return NULL;
    }
    sock_set_nonblock(s);
    n->listen_s = s;
    n->running = 1;
    pthread_create(&n->thread, NULL, io_thread, n);
    return n;
}

net_t *net_client_connect(const char *host, int port) {
    net_t *n = net_alloc(0);
    if (!n) return NULL;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) { free(n); return NULL; }
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        CLOSESOCK(s); pthread_mutex_destroy(&n->lock); free(n); return NULL;
    }
    sock_set_nonblock(s);
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) != 0 && !sock_wouldblock()
#if !defined(_WIN32)
        && errno != EINPROGRESS
#endif
        ) {
        CLOSESOCK(s); pthread_mutex_destroy(&n->lock); free(n); return NULL;
    } else {
        fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
        struct timeval tv = { 3, 0 };
        if (select((int)s + 1, NULL, &wf, NULL, &tv) <= 0) {
            CLOSESOCK(s); pthread_mutex_destroy(&n->lock); free(n); return NULL;
        }
        int err = 0; socklen_t el = (socklen_t)sizeof err;
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el) != 0 || err != 0) {
            CLOSESOCK(s); pthread_mutex_destroy(&n->lock); free(n); return NULL;
        }
    }
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    n->peers[0].active = 1;
    n->peers[0].s = s;
    n->peers[0].id = 0;
    n->running = 1;
    pthread_create(&n->thread, NULL, io_thread, n);
    return n;
}

void net_close(net_t *n) {
    if (!n) return;
    n->running = 0;
    pthread_join(n->thread, NULL);
    pthread_mutex_lock(&n->lock);
    for (int i = 0; i < NET_MAX_PEERS; i++)
        if (n->peers[i].active) { CLOSESOCK(n->peers[i].s); n->peers[i].active = 0; }
    if (n->is_host && n->listen_s != BAD_SOCK) CLOSESOCK(n->listen_s);
    pthread_mutex_unlock(&n->lock);
    pthread_mutex_destroy(&n->lock);
    free(n);
}

int net_is_host(const net_t *n) { return n ? n->is_host : 0; }

void net_poll(net_t *n, net_msg_cb cb, void *ud) {
    if (!n || !cb) return;
    for (;;) {
        net_msg_t m; int32_t from;
        pthread_mutex_lock(&n->lock);
        if (n->q_head == n->q_tail) { pthread_mutex_unlock(&n->lock); break; }
        m = n->q[n->q_head];
        from = n->q_from[n->q_head];
        n->q_head = (n->q_head + 1) % INGRESS_Q;
        pthread_mutex_unlock(&n->lock);
        cb(&m, from, ud);
    }
}

void net_send(net_t *n, int32_t peer, const net_msg_t *m) {
    if (!n) return;
    pthread_mutex_lock(&n->lock);
    if (n->is_host) {
        for (int i = 0; i < NET_MAX_PEERS; i++) {
            if (!n->peers[i].active) continue;
            if (peer >= 0 && n->peers[i].id != peer) continue;
            frame_and_send(n->peers[i].s, m);
        }
    } else if (n->peers[0].active) {
        frame_and_send(n->peers[0].s, m);
    }
    pthread_mutex_unlock(&n->lock);
}
