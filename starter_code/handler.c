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

extern bt_config_t config;
extern upload_to_peer_info_t *upload_to_peer_infos;
extern download_from_peer_info_t *download_from_peer_infos;
extern waiting_i_have_t *waiting_list_of_I_HAVE;
char outputfile_name[20];
extern char requestfile_name[20];
int waiting_count = 0;
int download_num = 0;

void print_received_packet_info(data_packet_t *curr)
{
    printf("MAGIC: %d\n", ntohs((curr->header).magicnum));
    printf("VERSION: %x\n", curr->header.version);
    printf("PACKET TYPE: %x\n", curr->header.packet_type);
    printf("HEADER LENGTH: %d\n", ntohs((curr->header).header_len));
    printf("TOTAL PACKET LENGTH: %d\n", ntohs((curr->header).packet_len));
}

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

    printf("DATA NUMBER SENT: %ld\n", spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr)));
    printf("WAITING COUNT: %d\n", waiting_count);
    printf("CURR POS: %d\n", download_from_peer_infos[0].curr_pos);
    printf("SEND TO: %d\n", ntohs(from->sin_port));
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
    if (ack >= 512)
        return;
    printf("\n===================ACK NEXT DATA================\n\n");
    FILE *fp_master_tar;
    printf("FILE NAME: %s\n", upload_to_peer_infos[0].master_tar_name);
    fp_master_tar = fopen(upload_to_peer_infos[0].master_tar_name, "r+");

    fseek(fp_master_tar, upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024, SEEK_SET);
    printf("%d\n", (upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024) / 1024);
    gettimeofday(&(upload_to_peer_infos[0].last_sent_time), NULL);
    // 更新最后发送数据的时间
    printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 + upload_to_peer_infos[0].last_sent_time.tv_usec);
    upload_to_peer_infos[0].max_ack = ack;
    data_packet_t packet;
    packet.header.magicnum = htons(15441);
    packet.header.version = 0x01;
    packet.header.packet_type = 0x03;
    packet.header.header_len = htons(16);
    packet.header.packet_len = htons(16 + 1024);
    packet.header.seq_num = htonl(ack + 1);

    fread(packet.data, 1, 1024, fp_master_tar);
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
    for (int i = 0; i < download_num; i++)
    {
        printf("%d\n", download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr);
        if (download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr == from->sin_addr.s_addr && download_from_peer_infos[i].sockaddr_download_from.sin_port == from->sin_port)
        {
            printf("DOWNLOAD ID: %d\n", download_from_peer_infos[i].request_id);
            printf("EXPECTED SEQ: %d\n", download_from_peer_infos[i].expected_seq);
            curr_download_index = i;
        }
    }

    // 如果data的序列号与当前期望的数据号不符，则返回期望值-1的ACK（窗口大小为1）
    if (seq != download_from_peer_infos[curr_download_index].expected_seq)
    {
        packet.header.magicnum = htons(15441);
        packet.header.version = 0x01;
        packet.header.packet_type = 0x04;
        packet.header.header_len = htons(16);
        packet.header.packet_len = htons(16);
        packet.header.ack_num = htonl(download_from_peer_infos[curr_download_index].expected_seq - 1);
        send_packet(packet, from);
        return;
    }

    // 打开或创建目标文件
    FILE *outputfile = fopen(/*outputfile_name*/ config.output_file, "rb+");
    if (outputfile == NULL)
    {
        outputfile = fopen(/*outputfile_name*/ config.output_file, "wb+");
    }

    // 寻址并写入目标文件，发送ACK
    printf("CURR POS OF ALL: %d\n", (download_from_peer_infos[curr_download_index].request_id * 512 * 1024 + download_from_peer_infos[curr_download_index].curr_pos) / 1024);
    fseek(outputfile, download_from_peer_infos[curr_download_index].request_id * 512 * 1024 + download_from_peer_infos[curr_download_index].curr_pos, SEEK_SET);
    int real_length_of_dataload = ntohs((curr->header).packet_len) - ntohs((curr->header).header_len);
    printf("REAL LENGTH OF DATALOAD: %d\n", real_length_of_dataload);

    download_from_peer_infos[curr_download_index].curr_pos += real_length_of_dataload;
    download_from_peer_infos[curr_download_index].expected_seq += 1;
    fwrite(curr->data, 1, real_length_of_dataload, outputfile);
    fclose(outputfile);
    packet.header.magicnum = htons(15441);
    packet.header.version = 0x01;
    packet.header.packet_type = 0x04;
    packet.header.header_len = htons(16);
    packet.header.packet_len = htons(16);
    packet.header.ack_num = htonl(seq);
    send_packet(packet, from);

    // 若chunk size写完，则寻找等待发送的get，并发送GET请求
    if (download_from_peer_infos[curr_download_index].curr_pos >= 524288 && waiting_count > 0)
    {
        waiting_count--;
        printf("\n============GET=================\n\n");
        data_packet_t packet;
        packet.header.magicnum = htons(15441);
        packet.header.version = 0x01;
        packet.header.packet_type = 0x02;
        packet.header.header_len = htons(16);
        packet.header.packet_len = htons(16 + 20);
        memcpy(packet.data, waiting_list_of_I_HAVE[0].hash_name, 20);

        FILE *fp;
        fp = fopen(requestfile_name, "r+");
        char bbf[60];
        char request_id_char[10];
        while (fgets(bbf, 60, fp))
        {
            char *hash_name;
            hash_name = strchr(bbf, ' ') + 1;
            hash_name[strlen(hash_name) - 1] = 0;
            char *alldata = malloc(sizeof(char) * 2 * 20);
            binary2hex(waiting_list_of_I_HAVE[0].hash_name, 20 * 1, alldata);
            printf("THIS HASH: %s\n", alldata);
            if (strcmp(alldata, hash_name) == 0)
            {
                memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
                free(alldata);
                break;
            }
            free(alldata);
        }
        fclose(fp);
        int request_id = atoi(request_id_char);

        download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
        download_from_peer_info->curr_pos = 0;
        download_from_peer_info->expected_seq = 1;
        download_from_peer_info->request_id = request_id;
        printf("\n%d\n", download_from_peer_info->request_id);
        memcpy(&download_from_peer_info->sockaddr_download_from, &(waiting_list_of_I_HAVE[0].sockaddr_download_from), sizeof(struct sockaddr_in));
        memcpy(download_from_peer_infos + download_num, download_from_peer_info, sizeof(download_from_peer_info_t));
        free(download_from_peer_info);
        download_num++;

        send_packet(packet, from);
    }
}

void deal_with_GET(data_packet_t *curr, struct sockaddr_in *from)
{
    // 找到要GET的Chunk Hash在Master Chunks中所对应的ID，并将信息记入upload_to_peer_info，发送第一个DATA包
    printf("\n\n===================RECEIVED GET================\n\n");
    char *request_hash = malloc(sizeof(char) * 2 * 20);
    binary2hex(curr->data, 20, request_hash);
    printf("requesting hash%s\n", request_hash);
    FILE *fp;
    //开master chunk file
    fp = fopen(config.chunk_file, "r+");
    char bbf[60];
    char request_id_char[10];
    int count = 0;

    char *master_tar_name = "C.tar";
    while (fgets(bbf, 60, fp))
    {
        if (count == 0)
        {
            // master_tar_name = strchr(bbf, ' ') + 1;
            count++;
            continue;
        }
        else if (count == 1)
        {
            count++;
            continue;
        }
        char *hash_name;
        hash_name = strchr(bbf, ' ') + 1;
        hash_name[strlen(hash_name) - 1] = 0;
        if (strcmp(hash_name, request_hash) == 0)
        {
            memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
        }
    }
    fclose(fp);
    int request_id = atoi(request_id_char);
    free(request_hash);
    FILE *fp_master_tar;
    printf("FILE NAME: %s\n", master_tar_name);
    fp_master_tar = fopen(master_tar_name, "r+");
    fseek(fp_master_tar, request_id * 512 * 1024, SEEK_SET);

    upload_to_peer_info_t *upload_to_peer_info = malloc(sizeof(upload_to_peer_info_t));
    memset(upload_to_peer_info, 0, sizeof(upload_to_peer_info_t));
    memcpy(upload_to_peer_info->master_tar_name, master_tar_name, strlen(master_tar_name) * sizeof(char));
    printf("\n%s\n", upload_to_peer_info->master_tar_name);
    upload_to_peer_info->request_id = request_id;
    upload_to_peer_info->max_ack = 0;
    printf("\n%d\n", upload_to_peer_info->request_id);
    gettimeofday(&(upload_to_peer_info->last_sent_time), NULL);
    printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_info->last_sent_time.tv_sec * 1000000 + upload_to_peer_info->last_sent_time.tv_usec);

    memcpy(&upload_to_peer_info->sockaddr_upload_to, from, sizeof(struct sockaddr_in));
    memcpy(upload_to_peer_infos, upload_to_peer_info, sizeof(upload_to_peer_info_t));
    free(upload_to_peer_info);

    printf("\n===================DATA================\n\n");
    data_packet_t packet;
    packet.header.magicnum = htons(15441);
    packet.header.version = 0x01;
    packet.header.packet_type = 0x03;
    packet.header.header_len = htons(16);
    packet.header.packet_len = htons(16 + 1024);
    packet.header.seq_num = htonl(1);
    fread(packet.data, 1, 1024 /*+ (request_id - 2) * 512*/, fp_master_tar);
    send_packet(packet, from);
    fclose(fp_master_tar);
}

void deal_with_IHAVE(data_packet_t *curr, struct sockaddr_in *from)
{
    //发GET，对于第一个Hash，直接发送GET请求，之后的记入waiting_i_have
    //实际上是number of chunks
    char *len = malloc(sizeof(char) * 2);
    binary2hex(curr->data, 1, len);
    int length = atoi(len);
    free(len);
    printf("DATA LEN: %d\n", length);

    //正式开始读ihave的hash list，跳过第一行
    unsigned char *data = curr->data + 4;
    char *alldata = malloc(sizeof(char) * 2 * length * 20);
    binary2hex(data, 20 * length, alldata);
    int sent = 0;
    for (int i = 0; i < length; i++)
    {
        printf("\n============GET=================\n\n");
        FILE *fp;
        //开master chunk file
        fp = fopen(requestfile_name, "r+");
        char bbf[60];
        char request_id_char[10];
        char *thisdata = malloc(sizeof(char) * 2 * 20);
        binary2hex(data, 20, thisdata);

        while (fgets(bbf, 60, fp))
        {
            char *hash_name;
            hash_name = strchr(bbf, ' ') + 1;
            hash_name[strlen(hash_name) - 1] = 0;
            printf("%s\n", thisdata);

            if (strcmp(thisdata, hash_name) == 0)
            {
                printf("%s\n", thisdata);
                memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
                break;
            }
        }
        free(thisdata);
        fclose(fp);
        int request_id = atoi(request_id_char);
        int under_trans = 0;
        for (int i = 0; i < download_num; i++)
        {
            if (request_id == download_from_peer_infos[i].request_id)
            {
                printf("%d\n", request_id);
                under_trans = 1;
            }
        }
        if (under_trans == 1)
        {
            data += 20;
            continue;
        }
        if (sent == 0)
        {
            download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
            download_from_peer_info->request_id = request_id;
            download_from_peer_info->curr_pos = 0;
            download_from_peer_info->expected_seq = 1;
            printf("\n%d\n", download_from_peer_info->request_id);
            memcpy(&download_from_peer_info->sockaddr_download_from, from, sizeof(struct sockaddr_in));
            memcpy(download_from_peer_infos + download_num, download_from_peer_info, sizeof(download_from_peer_info_t));
            free(download_from_peer_info);
            download_num++;

            data_packet_t packet;
            packet.header.magicnum = htons(15441);
            packet.header.version = 0x01;
            packet.header.packet_type = 0x02;
            packet.header.header_len = htons(16);
            packet.header.packet_len = htons(16 + 20);
            memcpy(packet.data, data, 20);
            data += 20;
            sent++;

            send_packet(packet, from);
        }
        else
        {
            printf("nimazhendesile\n");
            waiting_i_have_t *waiting_i_have = malloc(sizeof(waiting_i_have_t));
            waiting_i_have->request_id = request_id;
            memcpy(&waiting_i_have->sockaddr_download_from, from, sizeof(struct sockaddr_in));
            memcpy(&waiting_i_have->hash_name, data, 20);
            memcpy(waiting_list_of_I_HAVE, waiting_i_have, sizeof(waiting_i_have_t));
            data += 20;
            free(waiting_i_have);
            waiting_count++;
        }
    }
}

void deal_with_WHOHAS(data_packet_t *curr, struct sockaddr_in *from)
{
    char *len = malloc(sizeof(char) * 2);
    binary2hex(curr->data, 1, len);
    int length = atoi(len);
    free(len);
    printf("DATA LEN: %d\n", length);

    //正式开始读whohas的hash list
    unsigned char *data = curr->data + 4;
    char *alldata = malloc(sizeof(char) * 2 * length * 20);
    binary2hex(data, 20 * length, alldata);
    printf("ALL DATA: %s\n", alldata);

    printf("\n============SEND IHAVE=================\n\n");

    // receive WHOHAS and send IHAVE
    data_packet_t packet;
    packet.header.magicnum = htons(15441);
    packet.header.version = 0x01;
    //IHAVE 的type，todo：表示成常量
    packet.header.packet_type = 0x01;
    packet.header.header_len = htons(16);
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
            unsigned char *hash_binary = malloc(sizeof(unsigned char) * 20);
            hex2binary(hash_name, strlen(hash_name), hash_binary);
            memcpy(bufp, hash_binary, 20);
            bufp += 20;
            free(hash_binary);
            printf("%ld\n", strlen(hash_name));
            printf("%s\n", packet.data);
        }
    }
    *bufp = 0;
    free(alldata);
    packet.data[0] = 0x02;
    packet.data[1] = 0x00;
    packet.data[2] = 0x00;
    packet.data[3] = 0x00;
    int packet_len = 16 + 4 + count * 20;
    packet.header.packet_len = htons(packet_len);
    send_packet(packet, from);
}