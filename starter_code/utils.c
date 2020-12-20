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
extern chunk_status_t *chunk_status;
extern int chunk_num;
extern char master_tar[10];

// 返回指定hash值的id，若>=0，则说明存在，若为-1，则说明不存在
int search_id_for_hash(char *request_hash_name, char *file_name)
{
    int id = -1;
    FILE *fp = fopen(file_name, "r+");
    char line_buffer[60];
    char request_id_char[10];
    while (fgets(line_buffer, 60, fp))
    {
        char *hash_name;
        hash_name = strchr(line_buffer, ' ');
        if (hash_name == NULL)
        {
            continue;
        }
        hash_name = hash_name + 1;
        hash_name[strlen(hash_name) - 1] = 0;
        printf("%s\n", hash_name);
        printf("%s\n", request_hash_name);
        if (strcmp(request_hash_name, hash_name) == 0)
        {
            memcpy(request_id_char, line_buffer, strchr(line_buffer, ' ') - line_buffer);
            id = atoi(request_id_char);
            break;
        }
    }
    fclose(fp);
    return id;
}

void search_master_tar(char *file_name)
{
    FILE *fp = fopen(file_name, "r+");
    char line_buffer[60];
    fgets(line_buffer, 60, fp);
    char *master_tar_name;
    master_tar_name = strchr(line_buffer, ' ');
    master_tar_name = master_tar_name + 1;
    master_tar_name[strlen(master_tar_name) - 1] = 0;
    printf("this: %s\n", master_tar_name);

    fclose(fp);
    memcpy(master_tar, master_tar_name, sizeof(char) * strlen(master_tar_name));
}

header_t make_packet_header(char packet_type, short header_len, short packet_len, u_int seq_num, u_int ack_num)
{
    header_t packet_header;
    packet_header.magicnum = htons(MAGIC_NUMBER);
    packet_header.version = VERSION;
    packet_header.packet_type = packet_type;
    packet_header.header_len = htons(header_len);
    packet_header.packet_len = htons(packet_len);
    if (seq_num != -1)
        packet_header.seq_num = htonl(seq_num);
    else
        packet_header.seq_num = htonl(0);
    if (ack_num != -1)
        packet_header.ack_num = htonl(ack_num);
    else
        packet_header.ack_num = htonl(0);

    return packet_header;
}

int find_free_download_info()
{
    for (int i = 0; i < config.max_conn; i++)
        if (download_from_peer_infos[i].status == 0)
            return i;
    return -1;
}

int find_free_upload_info()
{
    for (int i = 0; i < config.max_conn; i++)
        if (upload_to_peer_infos[i].status == 0)
            return i;
    return -1;
}

int find_free_waiting_entity()
{
    for (int i = 0; i < 10; i++)
        if (waiting_list_of_I_HAVE[i].status == 0)
            return i;
    return -1;
}

int check_hash(unsigned char *data, unsigned char *hash)
{
    unsigned char data_hash[LEN_OF_CHUNK_HASH];
    shahash(data, BT_CHUNK_SIZE, data_hash);
    return memcmp(data_hash, hash, LEN_OF_CHUNK_HASH);
}

void try_send_GET_in_waiting_list()
{
    // 目前下载池已经被占满，不能再发GET
    if (find_free_download_info() == -1)
        return;

    for (int i = 0; i < 10; i++)
    {
        if (waiting_list_of_I_HAVE[i].status == 0)
            continue;
        if (chunk_status[waiting_list_of_I_HAVE[i].request_id].status == 2)
        {
            memset(waiting_list_of_I_HAVE + i, 0, sizeof(waiting_i_have_t));
            continue;
        }
        else if (chunk_status[waiting_list_of_I_HAVE[i].request_id].status == 1)
            continue;

        int flag = 0;
        for (int j = 0; j < config.max_conn; j++)
            if (download_from_peer_infos[j].status != 0 && download_from_peer_infos[j].sockaddr_download_from.sin_port == waiting_list_of_I_HAVE[j].sockaddr_download_from.sin_port)
                flag = 1;
        if (flag == 1)
            continue;

        printf("\n============GET=================\n\n");
        data_packet_t packet;
        packet.header = make_packet_header(PACKET_TYPE_GET, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER + LEN_OF_CHUNK_HASH, -1, -1);

        memcpy(packet.data, waiting_list_of_I_HAVE[i].hash_name, LEN_OF_CHUNK_HASH);
        int request_id = waiting_list_of_I_HAVE[i].request_id;

        download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
        download_from_peer_info->curr_pos = 0;
        download_from_peer_info->expected_seq = 1;
        download_from_peer_info->request_id = request_id;
        download_from_peer_info->status = 1;

        memcpy(&download_from_peer_info->sockaddr_download_from, &(waiting_list_of_I_HAVE[i].sockaddr_download_from), sizeof(struct sockaddr_in));
        int download_info_id = find_free_download_info();
        memcpy(download_from_peer_infos + download_info_id, download_from_peer_info, sizeof(download_from_peer_info_t));
        free(download_from_peer_info);
        send_packet(packet, &(waiting_list_of_I_HAVE[i].sockaddr_download_from));
        memset(waiting_list_of_I_HAVE + i, 0, sizeof(waiting_i_have_t));
    }
}