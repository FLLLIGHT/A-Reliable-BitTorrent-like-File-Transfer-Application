#include "packet.h"

#define MAGIC_NUMBER 15441
#define VERSION 0x01
#define PACKET_TYPE_WHOHAS 0x00
#define PACKET_TYPE_IHAVE 0x01
#define PACKET_TYPE_GET 0x02
#define PACKET_TYPE_DATA 0x03
#define PACKET_TYPE_ACK 0x04
#define PACKET_TYPE_DENIED 0x05
#define LEN_OF_CHUNK_HASH 20
// DATA类型的报文一次所发送的数据段的数据量
#define LEN_OF_SENT_DATA 1024
#define LEN_OF_PACKET_HEADER 16

// 记录了本peer作为“服务端”时，其他向我方请求数据的peer信息
typedef struct upload_to_peer_info
{
    // 其他peer所请求的Chunk Hash在Master Chunks中的id
    int request_id;
    // Master Chunk的文件名
    char master_tar_name[20];
    // 向我方请求资源的peer的addr信息
    struct sockaddr_in sockaddr_upload_to;
    // 最后一次向该peer发送DATA的时间，用于判断超时
    struct timeval last_sent_time;
    // 当前收到的最大的ack
    int max_ack;
    int status;
} upload_to_peer_info_t;

// 记录了本peer作为“客户端”时，正在向对方请求数据的peer信息
typedef struct download_from_peer_info
{
    // 当前请求的Chunk Hash在Request Chunks中的id
    int request_id;
    // 向对方请求数据的peer信息
    struct sockaddr_in sockaddr_download_from;
    // 当前待写入的位置（在当前请求的chunk中的偏移值，即在定长512KB的chunk中的偏移值，而不是在整个文件中的偏移值）
    int curr_pos;
    // 期望等待的DATA的序号
    int expected_seq;
    int status;
} download_from_peer_info_t;

// 记录了某个peer节点所拥有的Chunk，但由于当前正在从该peer下载数据，而必须等待并在之后再发GET请求进行下载
typedef struct waiting_I_HAVE
{
    // 拥有该chunk的addr
    struct sockaddr_in sockaddr_download_from;
    // chunk的hash
    unsigned char hash_name[20];
    // 当前请求的Chunk Hash在Request Chunks中的id
    int request_id;
    int status;
} waiting_i_have_t;

typedef struct chunk_status
{
    int status;
    // 在被请求文件中的id（而不是在master chunks中的id）
    int id;
    // 在被请求文件中的id对应的hash
    unsigned char hash[LEN_OF_CHUNK_HASH];
} chunk_status_t;

void deal_with_ACK(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_DATA(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_GET(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_IHAVE(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_WHOHAS(data_packet_t *curr, struct sockaddr_in *from);
void send_packet(data_packet_t packet, struct sockaddr_in *from);
void print_received_packet_info(data_packet_t *curr);