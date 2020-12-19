// 报文的头部结构
typedef struct header_s
{
    // magic number，为定值15441
    short magicnum;
    // version, 为定值0x01
    char version;
    // 根据报文的种类不同，有不同的packet type，取值从0x00到0x05
    char packet_type;
    // 报文头部的长度
    short header_len;
    // 报文的总长度
    short packet_len;
    // 发送的DATA序号，仅在DATA类型的报文中存在
    u_int seq_num;
    // 发从的ACK号，进在ACK类型的报文中存在
    u_int ack_num;
} header_t;

// 报文的整体结构
typedef struct data_packet
{
    header_t header;
    // 用于存放Chunk Hash或文件DATA
    unsigned char data[1500];
} data_packet_t;

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
} download_from_peer_info_t;

// 记录了某个peer节点所拥有的Chunk，但由于当前正在从该peer下载数据，而必须等在并在之后在发GET请求进行下载
typedef struct waiting_I_HAVE
{
    // 拥有该chunk的addr
    struct sockaddr_in sockaddr_download_from;
    // chunk的hash
    unsigned char hash_name[20];
    // 该hash在master chunks中的id
    int request_id;
} waiting_i_have_t;

void deal_with_ACK(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_DATA(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_GET(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_IHAVE(data_packet_t *curr, struct sockaddr_in *from);
void deal_with_WHOHAS(data_packet_t *curr, struct sockaddr_in *from);
void send_packet(data_packet_t packet, struct sockaddr_in *from);
void print_received_packet_info(data_packet_t *curr);
