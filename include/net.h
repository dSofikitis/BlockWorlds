#pragma once

#include <stdint.h>


#define NET_MAX_PEERS    8
#define NET_CHAT_MAX     120
#define NET_NAME_MAX     24
#define NET_BLOB_MAX     1024
#define NET_DEFAULT_PORT 25655

typedef enum {
    NET_HELLO = 1,
    NET_WELCOME,
    NET_ADD_PLAYER,
    NET_DEL_PLAYER,
    NET_STATE,
    NET_BLOCK,
    NET_CHAT,
    NET_TIME,
    NET_PLAYERDATA,
    NET_REQ_DATA
} net_op_t;

typedef struct {
    uint8_t  op;
    int32_t  player_id;
    float    x, y, z, yaw, pitch;
    int32_t  bx, by, bz;
    uint16_t block;
    uint32_t seed;
    int32_t  gamemode;
    float    world_time;
    char     text[NET_CHAT_MAX];
    uint16_t blob_len;
    uint8_t  blob[NET_BLOB_MAX];
} net_msg_t;

typedef struct net_s net_t;

int  net_global_init(void);
void net_global_shutdown(void);

net_t *net_host_start(int port);
net_t *net_client_connect(const char *host, int port);
void   net_close(net_t *n);
int    net_is_host(const net_t *n);

typedef void (*net_msg_cb)(const net_msg_t *m, int32_t from_peer, void *ud);
void net_poll(net_t *n, net_msg_cb cb, void *ud);

void net_send(net_t *n, int32_t peer, const net_msg_t *m);
static inline void net_broadcast(net_t *n, const net_msg_t *m) { net_send(n, -1, m); }
