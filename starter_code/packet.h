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
    unsigned char data[2000];
} data_packet_t;