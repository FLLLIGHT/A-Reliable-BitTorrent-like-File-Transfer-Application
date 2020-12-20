#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "spiffy.h"
#include "bt_parse.h"
#include "input_buffer.h"

#include "chunk.h"
#include <sys/time.h>
#include "handler.h"
#include "utils.h"

extern bt_config_t config;
extern upload_to_peer_info_t *upload_to_peer_infos;
extern download_from_peer_info_t *download_from_peer_infos;
extern waiting_i_have_t *waiting_list_of_I_HAVE;
extern char requestfile_name[BT_FILENAME_LEN];
extern char outputfile_name[BT_FILENAME_LEN];
extern chunk_status_t *chunk_status;
extern int chunk_num;
char master_tar[10];

// debug用，输出接收的packet信息
void print_received_packet_info(data_packet_t *curr)
{
    printf("MAGIC: %d\n", ntohs((curr->header).magicnum));
    printf("VERSION: %x\n", curr->header.version);
    printf("PACKET TYPE: %x\n", curr->header.packet_type);
    printf("HEADER LENGTH: %d\n", ntohs((curr->header).header_len));
    printf("TOTAL PACKET LENGTH: %d\n", ntohs((curr->header).packet_len));
}

// 向指定地址发数据包
void send_packet(data_packet_t packet, struct sockaddr_in *from)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in toaddr, myaddr;
    bzero(&myaddr, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    inet_aton("127.0.0.1", &myaddr.sin_addr);
    bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
    inet_aton("127.0.0.1", &toaddr.sin_addr);
    toaddr.sin_port = from->sin_port;
    toaddr.sin_family = AF_INET;
    printf("SEND TO: %d\n", ntohs(from->sin_port));
    printf("SENT ACK: %d\n", ntohl(packet.header.ack_num));
    printf("SENT SEQ: %d\n", ntohl(packet.header.seq_num));

    printf("DATA NUMBER SENT: %ld\n", spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr)));
    close(fd);
}

void deal_with_ACK(data_packet_t *curr, struct sockaddr_in *from)
{
    printf("\n\n===================RECEIVED ACK===============\n\n");
    u_int ack = ntohl((curr->header).ack_num);
    printf("ACK NUM: %d\n", ack);
    printf("INFO: %s\n", upload_to_peer_infos[0].master_tar_name);
    printf("INFO: %d\n", upload_to_peer_infos[0].request_id);
    printf("INFO: %d\n", ntohs(upload_to_peer_infos[0].sockaddr_upload_to.sin_port));
    if (ack >= BT_CHUNK_SIZE / LEN_OF_SENT_DATA)
        return;
    printf("\n===================ACK NEXT DATA================\n\n");
    FILE *fp_master_tar;
    printf("FILE NAME: %s\n", upload_to_peer_infos[0].master_tar_name);
    fp_master_tar = fopen(upload_to_peer_infos[0].master_tar_name, "r+");

    fseek(fp_master_tar, upload_to_peer_infos[0].request_id * BT_CHUNK_SIZE + ack * LEN_OF_SENT_DATA, SEEK_SET);
    printf("%d\n", (upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024) / 1024);
    gettimeofday(&(upload_to_peer_infos[0].last_sent_time), NULL);
    // 更新最后发送数据的时间
    printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 + upload_to_peer_infos[0].last_sent_time.tv_usec);
    upload_to_peer_infos[0].max_ack = ack;
    data_packet_t packet;
    packet.header = make_packet_header(PACKET_TYPE_DATA, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER + LEN_OF_SENT_DATA, ack + 1, -1);
    fread(packet.data, 1, LEN_OF_SENT_DATA, fp_master_tar);
    send_packet(packet, from);
    fclose(fp_master_tar);
}

void deal_with_DATA(data_packet_t *curr, struct sockaddr_in *from)
{
    printf("\n\n===================RECEIVED DATA===============\n\n");
    printf("\n\n===================SEND ACK===============\n\n");
    data_packet_t packet;
    u_int seq = ntohl(((curr->header).seq_num));
    printf("SEQ NUM: %d\n", seq);

    // 找到发送数据的peer所对应的download_from_peer_info
    int curr_download_index = -1;
    for (int i = 0; i < config.max_conn; i++)
    {
        printf("STATUS: %d\n", download_from_peer_infos[i].status);
        if (download_from_peer_infos[i].status == 0)
            continue;
        printf("%d\n", download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr);
        if (download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr == from->sin_addr.s_addr && download_from_peer_infos[i].sockaddr_download_from.sin_port == from->sin_port)
        {
            printf("DOWNLOAD ID: %d\n", download_from_peer_infos[i].request_id);
            printf("EXPECTED SEQ: %d\n", download_from_peer_infos[i].expected_seq);
            curr_download_index = i;
            break;
        }
    }
    if (curr_download_index == -1)
    {
        packet.header = make_packet_header(PACKET_TYPE_ACK, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER, -1, seq);
        send_packet(packet, from);
    }
    // 如果data的序列号与当前期望的数据号不符，则返回（期望值-1）的ACK（窗口大小为1）
    if (seq != download_from_peer_infos[curr_download_index].expected_seq)
    {
        printf("%d\n", download_from_peer_infos[curr_download_index].expected_seq);
        int sent_ack = download_from_peer_infos[curr_download_index].expected_seq - 1 > 0 ? download_from_peer_infos[curr_download_index].expected_seq - 1 : 1;
        packet.header = make_packet_header(PACKET_TYPE_ACK, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER, -1, sent_ack);
        send_packet(packet, from);
        return;
    }

    // 打开或创建目标文件
    FILE *outputfile = fopen(outputfile_name, "rb+");
    if (outputfile == NULL)
    {
        outputfile = fopen(outputfile_name, "wb+");
    }

    // 寻址（根据request id和curr_pos，request id代表了chunk在文件中的位置，curr_pos代表了当前数据在chunk中的位置）并写入目标文件，发送ACK
    printf("CURR POS OF ALL: %d\n", (download_from_peer_infos[curr_download_index].request_id * 512 * 1024 + download_from_peer_infos[curr_download_index].curr_pos) / 1024);
    fseek(outputfile, download_from_peer_infos[curr_download_index].request_id * BT_CHUNK_SIZE + download_from_peer_infos[curr_download_index].curr_pos, SEEK_SET);
    // 获取报文的数据段的长度
    int real_length_of_dataload = ntohs((curr->header).packet_len) - ntohs((curr->header).header_len);
    printf("REAL LENGTH OF DATALOAD: %d\n", real_length_of_dataload);

    download_from_peer_infos[curr_download_index].curr_pos += real_length_of_dataload;
    download_from_peer_infos[curr_download_index].expected_seq += 1;
    fwrite(curr->data, 1, real_length_of_dataload, outputfile);
    fclose(outputfile);

    packet.header = make_packet_header(PACKET_TYPE_ACK, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER, -1, seq);
    send_packet(packet, from);

    // 若chunk size写完，则寻找等待发送的get，并发送GET请求
    if (download_from_peer_infos[curr_download_index].curr_pos >= 524288)
    {
        FILE *outputfile = fopen(outputfile_name, "rb+");
        fseek(outputfile, download_from_peer_infos[curr_download_index].request_id * BT_CHUNK_SIZE, SEEK_SET);
        unsigned char data_writen[BT_CHUNK_SIZE];
        fread(data_writen, BT_CHUNK_SIZE, 1, outputfile);
        fclose(outputfile);
        if (check_hash(data_writen, chunk_status[download_from_peer_infos[curr_download_index].request_id].hash) == 0)
        {
            printf("GOT!");
            chunk_status[download_from_peer_infos[curr_download_index].request_id].status = 2;
        }
        else
        {
            printf("CORRUPTED!");
            chunk_status[download_from_peer_infos[curr_download_index].request_id].status = 0;
        }
        // 在download列表中清空该项
        download_from_peer_infos[curr_download_index].status = 0;
        memset(download_from_peer_infos + curr_download_index, 0, sizeof(download_from_peer_info_t));
        sleep(1);
        try_send_GET_in_waiting_list();
    }
}

void deal_with_GET(data_packet_t *curr, struct sockaddr_in *from)
{
    // 找到要GET的Chunk Hash在Master Chunks中所对应的ID，并将信息记入upload_to_peer_info，发送第一个DATA包
    printf("\n\n===================RECEIVED GET================\n\n");
    char *request_hash = malloc(sizeof(char) * 2 * LEN_OF_CHUNK_HASH);
    binary2hex(curr->data, LEN_OF_CHUNK_HASH, request_hash);
    printf("requesting hash%s\n", request_hash);
    //打开目标文件
    int request_id = search_id_for_hash(request_hash, config.chunk_file);
    free(request_hash);
    FILE *fp_master_tar;
    search_master_tar(config.chunk_file);
    printf("FILE NAME: %s\n", master_tar);
    fp_master_tar = fopen(master_tar, "r+");
    //根据request id找到要请求的chunk在文件中的位置，chunk size为固定值512KB
    fseek(fp_master_tar, request_id * BT_CHUNK_SIZE, SEEK_SET);
    upload_to_peer_info_t *upload_to_peer_info = malloc(sizeof(upload_to_peer_info_t));
    memset(upload_to_peer_info, 0, sizeof(upload_to_peer_info_t));
    memcpy(upload_to_peer_info->master_tar_name, master_tar, strlen(master_tar) * sizeof(char));
    printf("\n%s\n", upload_to_peer_info->master_tar_name);
    upload_to_peer_info->request_id = request_id;
    upload_to_peer_info->max_ack = 0;
    gettimeofday(&(upload_to_peer_info->last_sent_time), NULL);
    printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_info->last_sent_time.tv_sec * 1000000 + upload_to_peer_info->last_sent_time.tv_usec);

    memcpy(&upload_to_peer_info->sockaddr_upload_to, from, sizeof(struct sockaddr_in));
    memcpy(upload_to_peer_infos, upload_to_peer_info, sizeof(upload_to_peer_info_t));
    free(upload_to_peer_info);

    printf("\n===================DATA================\n\n");
    data_packet_t packet;
    packet.header = make_packet_header(PACKET_TYPE_DATA, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER + LEN_OF_SENT_DATA, 1, -1);
    fread(packet.data, 1, 1024, fp_master_tar);
    send_packet(packet, from);
    fclose(fp_master_tar);
}

//发GET，对于第一个Hash，直接发送GET请求，之后的记入waiting_i_have
void deal_with_IHAVE(data_packet_t *curr, struct sockaddr_in *from)
{
    //通过解析IHAVE包的数据段的第一行的第一个byte，获取IHAVE包中数据段存放的chunk hash的数量
    char *number_of_chunk_hash_char = malloc(sizeof(char) * 2);
    binary2hex(curr->data, 1, number_of_chunk_hash_char);
    int number_of_chunk_hash = atoi(number_of_chunk_hash_char);
    free(number_of_chunk_hash_char);
    printf("NUMBER OF CHUNK HASH: %d\n", number_of_chunk_hash);

    //正式开始读ihave的hash list，跳过第一行
    unsigned char *data = curr->data + 4;
    // //将数据从hex string（unsigned char数组）转为字符串（char数组）
    // char *alldata = malloc(sizeof(char) * 2 * number_of_chunk_hash * LEN_OF_CHUNK_HASH);
    // binary2hex(data, LEN_OF_CHUNK_HASH * number_of_chunk_hash, alldata);
    // printf("%s\n", alldata);
    // free(alldata);
    int sent = 0;
    //遍历每一个IHAVE的hash
    for (int i = 0; i < number_of_chunk_hash; i++)
    {
        printf("\n============GET=================\n\n");
        char thisdata[40];
        binary2hex(data, LEN_OF_CHUNK_HASH, thisdata);
        int request_id = search_id_for_hash(thisdata, requestfile_name);

        // 判断该hash对应的chunk是否正在被传或已经下载完毕，如果正在被传或已经下载完毕，则跳过该hash
        if (chunk_status[request_id].status != 0)
        {
            data += LEN_OF_CHUNK_HASH;
            continue;
        }
        // 对于每个hash，只对第一个hash发请求，其他的则存放到waiting list中
        if (sent == 0)
        {
            download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
            download_from_peer_info->request_id = request_id;
            download_from_peer_info->curr_pos = 0;
            download_from_peer_info->expected_seq = 1;
            download_from_peer_info->status = 1;

            memcpy(&download_from_peer_info->sockaddr_download_from, from, sizeof(struct sockaddr_in));

            int download_info_id = find_free_download_info();
            printf("FREE: %d\n", download_info_id);
            memcpy(download_from_peer_infos + download_info_id, download_from_peer_info, sizeof(download_from_peer_info_t));
            free(download_from_peer_info);

            data_packet_t packet;
            packet.header = make_packet_header(PACKET_TYPE_GET, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER + LEN_OF_CHUNK_HASH, -1, -1);
            memcpy(packet.data, data, LEN_OF_CHUNK_HASH);

            chunk_status[request_id].status = 1;
            memcpy(chunk_status[request_id].hash, data, LEN_OF_CHUNK_HASH);

            data += LEN_OF_CHUNK_HASH;
            sent++;
            send_packet(packet, from);
        }
        else
        {
            waiting_i_have_t *waiting_i_have = malloc(sizeof(waiting_i_have_t));
            waiting_i_have->request_id = request_id;
            waiting_i_have->status = 1;
            memcpy(&waiting_i_have->sockaddr_download_from, from, sizeof(struct sockaddr_in));
            memcpy(&waiting_i_have->hash_name, data, LEN_OF_CHUNK_HASH);
            int waiting_list_index = find_free_waiting_entity();
            memcpy(waiting_list_of_I_HAVE + waiting_list_index, waiting_i_have, sizeof(waiting_i_have_t));
            data += LEN_OF_CHUNK_HASH;
            free(waiting_i_have);
        }
    }
}

void deal_with_WHOHAS(data_packet_t *curr, struct sockaddr_in *from)
{
    //通过解析WHOHAS包的数据段的第一行的第一个byte，获取WHOHAS包中数据段存放的chunk hash的数量
    char *number_of_chunk_hash_char = malloc(sizeof(char) * 2);
    binary2hex(curr->data, 1, number_of_chunk_hash_char);
    int number_of_chunk_hash = atoi(number_of_chunk_hash_char);
    free(number_of_chunk_hash_char);
    printf("NUMBER OF CHUNK HASH: %d\n", number_of_chunk_hash);

    //正式开始读whohas的hash list
    unsigned char *data = curr->data + 4;
    char *alldata = malloc(sizeof(char) * 2 * number_of_chunk_hash * LEN_OF_CHUNK_HASH);
    binary2hex(data, LEN_OF_CHUNK_HASH * number_of_chunk_hash, alldata);
    printf("ALL DATA: %s\n", alldata);

    printf("\n============SEND IHAVE=================\n\n");

    data_packet_t packet;
    FILE *fp;
    fp = fopen(config.has_chunk_file, "r+");
    char bbf[60];
    unsigned char *bufp = packet.data + 4;
    int count = 0;
    while (fgets(bbf, 60, fp))
    {
        char *hash_name;
        hash_name = strchr(bbf, ' ') + 1;
        hash_name[strlen(hash_name) - 1] = 0;
        printf("%s\n", hash_name);
        if (strstr(alldata, hash_name) != NULL)
        {
            count++;
            unsigned char *hash_binary = malloc(sizeof(unsigned char) * LEN_OF_CHUNK_HASH);
            hex2binary(hash_name, strlen(hash_name), hash_binary);
            memcpy(bufp, hash_binary, LEN_OF_CHUNK_HASH);
            bufp += LEN_OF_CHUNK_HASH;
            free(hash_binary);
            printf("%ld\n", strlen(hash_name));
            printf("%s\n", packet.data);
        }
    }
    *bufp = 0;
    free(alldata);
    // char number_of_chunks[2];
    // sprintf(number_of_chunks, "%x", count);
    // unsigned char *number_of_chunks_uc = malloc(sizeof(unsigned char));
    // hex2binary(number_of_chunks, 1, number_of_chunks_uc);
    // memcpy(&packet.data[0], number_of_chunks_uc, sizeof(unsigned char));
    // printf("%x\n\n", packet.data[0]);
    // printf("%x\n\n", number_of_chunks_uc);

    // free(number_of_chunks_uc);
    packet.data[0] = count;
    packet.data[1] = 0x00;
    packet.data[2] = 0x00;
    packet.data[3] = 0x00;
    int packet_len = LEN_OF_PACKET_HEADER + 4 + count * LEN_OF_CHUNK_HASH;
    packet.header = make_packet_header(PACKET_TYPE_IHAVE, LEN_OF_PACKET_HEADER, packet_len, -1, -1);

    if (count != 0)
    {
        send_packet(packet, from);
    }
}
