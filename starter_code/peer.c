/*
 * peer.c
 * 
 * Author: Yi Lu <19212010040@fudan.edu.cn>,
 *
 * Modified from CMU 15-441,
 * Original Authors: Ed Bardsley <ebardsle+441@andrew.cmu.edu>,
 *                   Dave Andersen
 * 
 * Class: Networks (Spring 2015)
 *
 */

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

void peer_run(bt_config_t *config);

// // 报文的头部结构
// typedef struct header_s
// {
//   // magic number，为定值15441
//   short magicnum;
//   // version, 为定值0x01
//   char version;
//   // 根据报文的种类不同，有不同的packet type，取值从0x00到0x05
//   char packet_type;
//   // 报文头部的长度
//   short header_len;
//   // 报文的总长度
//   short packet_len;
//   // 发送的DATA序号，仅在DATA类型的报文中存在
//   u_int seq_num;
//   // 发从的ACK号，进在ACK类型的报文中存在
//   u_int ack_num;
// } header_t;

// // 报文的整体结构
// typedef struct data_packet
// {
//   header_t header;
//   // 用于存放Chunk Hash或文件DATA
//   unsigned char data[1500];
// } data_packet_t;

// // 记录了本peer作为“服务端”时，其他向我方请求数据的peer信息
// typedef struct upload_to_peer_info
// {
//   // 其他peer所请求的Chunk Hash在Master Chunks中的id
//   int request_id;
//   // Master Chunk的文件名
//   char master_tar_name[20];
//   // 向我方请求资源的peer的addr信息
//   struct sockaddr_in sockaddr_upload_to;
//   // 最后一次向该peer发送DATA的时间，用于判断超时
//   struct timeval last_sent_time;
//   // 当前收到的最大的ack
//   int max_ack;
// } upload_to_peer_info_t;

// // 记录了本peer作为“客户端”时，正在向对方请求数据的peer信息
// typedef struct download_from_peer_info
// {
//   // 当前请求的Chunk Hash在Request Chunks中的id
//   int request_id;
//   // 向对方请求数据的peer信息
//   struct sockaddr_in sockaddr_download_from;
//   // 当前待写入的位置（在当前请求的chunk中的偏移值，即在定长512KB的chunk中的偏移值，而不是在整个文件中的偏移值）
//   int curr_pos;
//   // 期望等待的DATA的序号
//   int expected_seq;
// } download_from_peer_info_t;

// // 记录了某个peer节点所拥有的Chunk，但由于当前正在从该peer下载数据，而必须等在并在之后在发GET请求进行下载
// typedef struct waiting_I_HAVE
// {
//   // 拥有该chunk的addr
//   struct sockaddr_in sockaddr_download_from;
//   // chunk的hash
//   unsigned char hash_name[20];
//   // 该hash在master chunks中的id
//   int request_id;
// } waiting_i_have_t;

bt_config_t config;
upload_to_peer_info_t *upload_to_peer_infos;
download_from_peer_info_t *download_from_peer_infos;
waiting_i_have_t *waiting_list_of_I_HAVE;
// char outputfile_name[20];
char requestfile_name[20];
// int waiting_count = 0;
// int download_num = 0;

int main(int argc, char **argv)
{
  bt_init(&config, argc, argv);

  DPRINTF(DEBUG_INIT, "peer.c main beginning\n");

#ifdef TESTING
  config.identity = 1; // your group number here
  strcpy(config.chunk_file, "chunkfile");
  strcpy(config.has_chunk_file, "haschunks");
#endif

  bt_parse_command_line(&config);

#ifdef DEBUG
  if (debug & DEBUG_INIT)
  {
    bt_dump_config(&config);
  }
#endif

  peer_run(&config);
  return 0;
}

// void print_received_packet_info(data_packet_t *curr)
// {
//   printf("MAGIC: %d\n", ntohs((curr->header).magicnum));
//   printf("VERSION: %x\n", curr->header.version);
//   printf("PACKET TYPE: %x\n", curr->header.packet_type);
//   printf("HEADER LENGTH: %d\n", ntohs((curr->header).header_len));
//   printf("TOTAL PACKET LENGTH: %d\n", ntohs((curr->header).packet_len));
// }

// void send_packet(data_packet_t packet, struct sockaddr_in *from)
// {
//   int fd = socket(AF_INET, SOCK_DGRAM, 0);
//   struct sockaddr_in toaddr, myaddr;
//   bzero(&myaddr, sizeof(myaddr));
//   myaddr.sin_family = AF_INET;
//   inet_aton("127.0.0.1", &myaddr.sin_addr);
//   bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
//   inet_aton("127.0.0.1", &toaddr.sin_addr);
//   toaddr.sin_port = from->sin_port;
//   toaddr.sin_family = AF_INET;

//   printf("DATA NUMBER SENT: %ld\n", spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr)));
//   printf("WAITING COUNT: %d\n", waiting_count);
//   printf("CURR POS: %d\n", download_from_peer_infos[0].curr_pos);
//   printf("SEND TO: %d\n", ntohs(from->sin_port));
//   close(fd);
// }

// void deal_with_ACK(data_packet_t *curr, struct sockaddr_in *from)
// {
//   printf("\n\n===================RECEIVED ACK===============\n\n");
//   u_int ack = ntohl((curr->header).ack_num);
//   printf("ACK NUM: %d\n", ack);
//   printf("INFO: %s\n", upload_to_peer_infos[0].master_tar_name);
//   printf("INFO: %d\n", upload_to_peer_infos[0].request_id);
//   printf("INFO: %d\n", ntohs(upload_to_peer_infos[0].sockaddr_upload_to.sin_port));
//   if (ack >= 512)
//     return;
//   printf("\n===================ACK NEXT DATA================\n\n");
//   FILE *fp_master_tar;
//   printf("FILE NAME: %s\n", upload_to_peer_infos[0].master_tar_name);
//   fp_master_tar = fopen(upload_to_peer_infos[0].master_tar_name, "r+");

//   fseek(fp_master_tar, upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024, SEEK_SET);
//   printf("%d\n", (upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024) / 1024);
//   gettimeofday(&(upload_to_peer_infos[0].last_sent_time), NULL);
//   // 更新最后发送数据的时间
//   printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 + upload_to_peer_infos[0].last_sent_time.tv_usec);
//   upload_to_peer_infos[0].max_ack = ack;
//   data_packet_t packet;
//   packet.header.magicnum = htons(15441);
//   packet.header.version = 0x01;
//   packet.header.packet_type = 0x03;
//   packet.header.header_len = htons(16);
//   packet.header.packet_len = htons(16 + 1024);
//   packet.header.seq_num = htonl(ack + 1);

//   fread(packet.data, 1, 1024, fp_master_tar);
//   send_packet(packet, from);
//   fclose(fp_master_tar);
// }

// void deal_with_DATA(data_packet_t *curr, struct sockaddr_in *from)
// {
//   printf("\n\n===================RECEIVED DATA===============\n\n");
//   printf("\n\n===================SEND ACK===============\n\n");
//   data_packet_t packet;
//   u_int seq = ntohl(((curr->header).seq_num));
//   printf("SEQ NUM: %d\n", seq);

//   // 找到发送数据的peer所对应的download_from_peer_info
//   int curr_download_index = -1;
//   for (int i = 0; i < download_num; i++)
//   {
//     printf("%d\n", download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr);
//     if (download_from_peer_infos[i].sockaddr_download_from.sin_addr.s_addr == from->sin_addr.s_addr && download_from_peer_infos[i].sockaddr_download_from.sin_port == from->sin_port)
//     {
//       printf("DOWNLOAD ID: %d\n", download_from_peer_infos[i].request_id);
//       printf("EXPECTED SEQ: %d\n", download_from_peer_infos[i].expected_seq);
//       curr_download_index = i;
//     }
//   }

//   // 如果data的序列号与当前期望的数据号不符，则返回期望值-1的ACK（窗口大小为1）
//   if (seq != download_from_peer_infos[curr_download_index].expected_seq)
//   {
//     packet.header.magicnum = htons(15441);
//     packet.header.version = 0x01;
//     packet.header.packet_type = 0x04;
//     packet.header.header_len = htons(16);
//     packet.header.packet_len = htons(16);
//     packet.header.ack_num = htonl(download_from_peer_infos[curr_download_index].expected_seq - 1);
//     send_packet(packet, from);
//     return;
//   }

//   // 打开或创建目标文件
//   FILE *outputfile = fopen(outputfile_name, "rb+");
//   if (outputfile == NULL)
//   {
//     outputfile = fopen(outputfile_name, "wb+");
//   }

//   // 寻址并写入目标文件，发送ACK
//   printf("CURR POS OF ALL: %d\n", (download_from_peer_infos[curr_download_index].request_id * 512 * 1024 + download_from_peer_infos[curr_download_index].curr_pos) / 1024);
//   fseek(outputfile, download_from_peer_infos[curr_download_index].request_id * 512 * 1024 + download_from_peer_infos[curr_download_index].curr_pos, SEEK_SET);
//   int real_length_of_dataload = ntohs((curr->header).packet_len) - ntohs((curr->header).header_len);
//   printf("REAL LENGTH OF DATALOAD: %d\n", real_length_of_dataload);

//   download_from_peer_infos[curr_download_index].curr_pos += real_length_of_dataload;
//   download_from_peer_infos[curr_download_index].expected_seq += 1;
//   fwrite(curr->data, 1, real_length_of_dataload, outputfile);
//   fclose(outputfile);
//   packet.header.magicnum = htons(15441);
//   packet.header.version = 0x01;
//   packet.header.packet_type = 0x04;
//   packet.header.header_len = htons(16);
//   packet.header.packet_len = htons(16);
//   packet.header.ack_num = htonl(seq);
//   send_packet(packet, from);

//   // 若chunk size写完，则寻找等待发送的get，并发送GET请求
//   if (download_from_peer_infos[curr_download_index].curr_pos >= 524288 && waiting_count > 0)
//   {
//     waiting_count--;
//     printf("\n============GET=================\n\n");
//     data_packet_t packet;
//     packet.header.magicnum = htons(15441);
//     packet.header.version = 0x01;
//     packet.header.packet_type = 0x02;
//     packet.header.header_len = htons(16);
//     packet.header.packet_len = htons(16 + 20);
//     memcpy(packet.data, waiting_list_of_I_HAVE[0].hash_name, 20);

//     FILE *fp;
//     fp = fopen(requestfile_name, "r+");
//     char bbf[60];
//     char request_id_char[10];
//     while (fgets(bbf, 60, fp))
//     {
//       char *hash_name;
//       hash_name = strchr(bbf, ' ') + 1;
//       hash_name[strlen(hash_name) - 1] = 0;
//       char *alldata = malloc(sizeof(char) * 2 * 20);
//       binary2hex(waiting_list_of_I_HAVE[0].hash_name, 20 * 1, alldata);
//       printf("THIS HASH: %s\n", alldata);
//       if (strcmp(alldata, hash_name) == 0)
//       {
//         memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
//         free(alldata);
//         break;
//       }
//       free(alldata);
//     }
//     fclose(fp);
//     int request_id = atoi(request_id_char);

//     download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
//     download_from_peer_info->curr_pos = 0;
//     download_from_peer_info->expected_seq = 1;
//     download_from_peer_info->request_id = request_id;
//     printf("\n%d\n", download_from_peer_info->request_id);
//     memcpy(&download_from_peer_info->sockaddr_download_from, &(waiting_list_of_I_HAVE[0].sockaddr_download_from), sizeof(struct sockaddr_in));
//     memcpy(download_from_peer_infos + download_num, download_from_peer_info, sizeof(download_from_peer_info_t));
//     free(download_from_peer_info);
//     download_num++;

//     send_packet(packet, from);
//   }
// }

// void deal_with_GET(data_packet_t *curr, struct sockaddr_in *from)
// {
//   // 找到要GET的Chunk Hash在Master Chunks中所对应的ID，并将信息记入upload_to_peer_info，发送第一个DATA包
//   printf("\n\n===================RECEIVED GET================\n\n");
//   char *request_hash = malloc(sizeof(char) * 2 * 20);
//   binary2hex(curr->data, 20, request_hash);
//   printf("requesting hash%s\n", request_hash);
//   FILE *fp;
//   //开master chunk file
//   fp = fopen(config.chunk_file, "r+");
//   char bbf[60];
//   char request_id_char[10];
//   int count = 0;

//   char *master_tar_name = "C.tar";
//   while (fgets(bbf, 60, fp))
//   {
//     if (count == 0)
//     {
//       // master_tar_name = strchr(bbf, ' ') + 1;
//       count++;
//       continue;
//     }
//     else if (count == 1)
//     {
//       count++;
//       continue;
//     }
//     char *hash_name;
//     hash_name = strchr(bbf, ' ') + 1;
//     hash_name[strlen(hash_name) - 1] = 0;
//     if (strcmp(hash_name, request_hash) == 0)
//     {
//       memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
//     }
//   }
//   fclose(fp);
//   int request_id = atoi(request_id_char);
//   free(request_hash);
//   FILE *fp_master_tar;
//   printf("FILE NAME: %s\n", master_tar_name);
//   fp_master_tar = fopen(master_tar_name, "r+");
//   fseek(fp_master_tar, request_id * 512 * 1024, SEEK_SET);

//   upload_to_peer_info_t *upload_to_peer_info = malloc(sizeof(upload_to_peer_info_t));
//   memset(upload_to_peer_info, 0, sizeof(upload_to_peer_info_t));
//   memcpy(upload_to_peer_info->master_tar_name, master_tar_name, strlen(master_tar_name) * sizeof(char));
//   printf("\n%s\n", upload_to_peer_info->master_tar_name);
//   upload_to_peer_info->request_id = request_id;
//   upload_to_peer_info->max_ack = 0;
//   printf("\n%d\n", upload_to_peer_info->request_id);
//   gettimeofday(&(upload_to_peer_info->last_sent_time), NULL);
//   printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_info->last_sent_time.tv_sec * 1000000 + upload_to_peer_info->last_sent_time.tv_usec);

//   memcpy(&upload_to_peer_info->sockaddr_upload_to, from, sizeof(struct sockaddr_in));
//   memcpy(upload_to_peer_infos, upload_to_peer_info, sizeof(upload_to_peer_info_t));
//   free(upload_to_peer_info);

//   printf("\n===================DATA================\n\n");
//   data_packet_t packet;
//   packet.header.magicnum = htons(15441);
//   packet.header.version = 0x01;
//   packet.header.packet_type = 0x03;
//   packet.header.header_len = htons(16);
//   packet.header.packet_len = htons(16 + 1024);
//   packet.header.seq_num = htonl(1);
//   fread(packet.data, 1, 1024 /*+ (request_id - 2) * 512*/, fp_master_tar);
//   send_packet(packet, from);
//   fclose(fp_master_tar);
// }

// void deal_with_IHAVE(data_packet_t *curr, struct sockaddr_in *from)
// {
//   //发GET，对于第一个Hash，直接发送GET请求，之后的记入waiting_i_have
//   //实际上是number of chunks
//   char *len = malloc(sizeof(char) * 2);
//   binary2hex(curr->data, 1, len);
//   int length = atoi(len);
//   free(len);
//   printf("DATA LEN: %d\n", length);

//   //正式开始读ihave的hash list，跳过第一行
//   unsigned char *data = curr->data + 4;
//   char *alldata = malloc(sizeof(char) * 2 * length * 20);
//   binary2hex(data, 20 * length, alldata);
//   int sent = 0;
//   for (int i = 0; i < length; i++)
//   {
//     printf("\n============GET=================\n\n");
//     FILE *fp;
//     //开master chunk file
//     fp = fopen(requestfile_name, "r+");
//     char bbf[60];
//     char request_id_char[10];
//     char *thisdata = malloc(sizeof(char) * 2 * 20);
//     binary2hex(data, 20, thisdata);
//     while (fgets(bbf, 60, fp))
//     {
//       char *hash_name;
//       hash_name = strchr(bbf, ' ') + 1;
//       hash_name[strlen(hash_name) - 1] = 0;
//       printf("%s\n", thisdata);

//       if (strcmp(thisdata, hash_name) == 0)
//       {
//         printf("%s\n", thisdata);
//         memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
//         break;
//       }
//     }
//     free(thisdata);
//     fclose(fp);
//     int request_id = atoi(request_id_char);
//     int under_trans = 0;
//     for (int i = 0; i < download_num; i++)
//     {
//       if (request_id == download_from_peer_infos[i].request_id)
//       {
//         printf("%d\n", request_id);
//         under_trans = 1;
//       }
//     }
//     if (under_trans == 1)
//     {
//       data += 20;
//       continue;
//     }
//     if (sent == 0)
//     {
//       download_from_peer_info_t *download_from_peer_info = malloc(sizeof(download_from_peer_info_t));
//       download_from_peer_info->request_id = request_id;
//       download_from_peer_info->curr_pos = 0;
//       download_from_peer_info->expected_seq = 1;
//       printf("\n%d\n", download_from_peer_info->request_id);
//       memcpy(&download_from_peer_info->sockaddr_download_from, from, sizeof(struct sockaddr_in));
//       memcpy(download_from_peer_infos + download_num, download_from_peer_info, sizeof(download_from_peer_info_t));
//       free(download_from_peer_info);
//       download_num++;

//       data_packet_t packet;
//       packet.header.magicnum = htons(15441);
//       packet.header.version = 0x01;
//       packet.header.packet_type = 0x02;
//       packet.header.header_len = htons(16);
//       packet.header.packet_len = htons(16 + 20);
//       memcpy(packet.data, data, 20);
//       data += 20;
//       sent++;

//       send_packet(packet, from);
//     }
//     else
//     {
//       printf("nimazhendesile\n");
//       waiting_i_have_t *waiting_i_have = malloc(sizeof(waiting_i_have_t));
//       waiting_i_have->request_id = request_id;
//       memcpy(&waiting_i_have->sockaddr_download_from, from, sizeof(struct sockaddr_in));
//       memcpy(&waiting_i_have->hash_name, data, 20);
//       memcpy(waiting_list_of_I_HAVE, waiting_i_have, sizeof(waiting_i_have_t));
//       data += 20;
//       free(waiting_i_have);
//       waiting_count++;
//     }
//   }
// }

// void deal_with_WHOHAS(data_packet_t *curr, struct sockaddr_in *from)
// {
//   char *len = malloc(sizeof(char) * 2);
//   binary2hex(curr->data, 1, len);
//   int length = atoi(len);
//   free(len);
//   printf("DATA LEN: %d\n", length);

//   //正式开始读whohas的hash list
//   unsigned char *data = curr->data + 4;
//   char *alldata = malloc(sizeof(char) * 2 * length * 20);
//   binary2hex(data, 20 * length, alldata);
//   printf("ALL DATA: %s\n", alldata);

//   printf("\n============SEND IHAVE=================\n\n");

//   // receive WHOHAS and send IHAVE
//   data_packet_t packet;
//   packet.header.magicnum = htons(15441);
//   packet.header.version = 0x01;
//   //IHAVE 的type，todo：表示成常量
//   packet.header.packet_type = 0x01;
//   packet.header.header_len = htons(16);
//   FILE *fp;
//   fp = fopen(config.has_chunk_file, "r+");
//   char bbf[60];
//   unsigned char *bufp = packet.data + 4;
//   int count = 0;
//   while (fgets(bbf, 60, fp))
//   {
//     char *hash_name;
//     hash_name = strchr(bbf, ' ') + 1;
//     hash_name[strlen(hash_name) - 1] = 0;
//     printf("%s\n", hash_name);
//     if (strstr(alldata, hash_name) != NULL)
//     {
//       count++;
//       unsigned char *hash_binary = malloc(sizeof(unsigned char) * 20);
//       hex2binary(hash_name, strlen(hash_name), hash_binary);
//       memcpy(bufp, hash_binary, 20);
//       bufp += 20;
//       free(hash_binary);
//       printf("%ld\n", strlen(hash_name));
//       printf("%s\n", packet.data);
//     }
//   }
//   *bufp = 0;
//   free(alldata);
//   packet.data[0] = 0x02;
//   packet.data[1] = 0x00;
//   packet.data[2] = 0x00;
//   packet.data[3] = 0x00;
//   int packet_len = 16 + 4 + count * 20;
//   packet.header.packet_len = htons(packet_len);
//   send_packet(packet, from);
// }

char process_inbound_udp(int sock)
{
#define BUFLEN 1500
  struct sockaddr_in from;
  socklen_t fromlen;
  char buf[BUFLEN];
  data_packet_t *curr;
  fromlen = sizeof(from);
  spiffy_recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr *)&from, &fromlen);
  curr = (data_packet_t *)buf;
  //打印提示信息
  print_received_packet_info(curr);
  printf("============%d===============", ntohs(from.sin_port));

  if (curr->header.packet_type == 0x00)
  {
    deal_with_WHOHAS(curr, &from);
  }
  else if (curr->header.packet_type == 0x01)
  {
    printf("shabishabi%s\n", config.chunk_file);

    deal_with_IHAVE(curr, &from);
  }
  else if (curr->header.packet_type == 0x02)
  {
    deal_with_GET(curr, &from);
  }
  else if (curr->header.packet_type == 0x03)
  {
    deal_with_DATA(curr, &from);
  }
  else if (curr->header.packet_type == 0x04)
  {
    deal_with_ACK(curr, &from);
  }
  fflush(stdout);

  printf("PROCESS_INBOUND_UDP SKELETON -- replace!\n"
         "Incoming message from %s:%d\n%s\n\n",
         inet_ntoa(from.sin_addr),
         ntohs(from.sin_port),
         buf);
  return curr->header.packet_type;
}

void process_get(char *chunkfile, char *outputfile)
{
  printf("PROCESS GET SKELETON CODE CALLED.  Fill me in!  (%s, %s)\n",
         chunkfile, outputfile);
  printf("\n============SEND WHOHAS=================\n\n");
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in toaddr, myaddr;
  bzero(&myaddr, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &myaddr.sin_addr);
  bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
  inet_aton("127.0.0.1", &toaddr.sin_addr);
  // toaddr.sin_port = htons(1111);
  toaddr.sin_family = AF_INET;

  // memset(outputfile_name, 0, 20);
  // memcpy(outputfile_name, outputfile, sizeof(char) * strlen(outputfile));
  strcpy((&config)->output_file, outputfile);
  memset(requestfile_name, 0, 20);
  memcpy(requestfile_name, chunkfile, sizeof(char) * strlen(chunkfile));

  //发送WHOHAS
  data_packet_t packet;
  packet.header.magicnum = htons(15441);
  packet.header.version = 0x01;

  //WHOHAS 的type，todo：表示成常量
  packet.header.packet_type = 0x00;
  packet.header.header_len = htons(16);
  packet.header.packet_len = htons(60);
  packet.header.ack_num = 0;
  packet.header.seq_num = 0;
  //搜索要找的的hash值
  FILE *fp;
  fp = fopen(chunkfile, "r+");
  char bbf[60];
  unsigned char *bufp = packet.data + 4;
  for (int i = 0; i < 1000; i++)
  {
    packet.data[i] = 0x00;
  }
  packet.data[0] = 0x02;
  packet.data[1] = 0x00;
  packet.data[2] = 0x00;
  packet.data[3] = 0x00;

  toaddr.sin_port = htons(1111);

  //   config.peers = config.peers->next;
  while (fgets(bbf, 60, fp))
  {
    char *hash_name;
    hash_name = strchr(bbf, ' ') + 1;
    hash_name[strlen(hash_name) - 1] = 0;
    printf("%ld\n", strlen(hash_name));
    unsigned char *hash_binary = malloc(sizeof(unsigned char) * 20);

    hex2binary(hash_name, strlen(hash_name), hash_binary);
    memcpy(bufp, hash_binary, 20);
    bufp += 20;
    free(hash_binary);
  }
  char *bb = malloc(sizeof(char) * 80);
  binary2hex(packet.data, 40, bb);
  printf("%s\n", bb);
  free(bb);
  *bufp = 0;

  //todo: 一个packet的最大大小为1500bytes，超过大小的whohas要分割
  // memset(&(packet.data), 0, 100);
  bt_peer_t *curr_peer = config.peers;
  for (; curr_peer != NULL; curr_peer = curr_peer->next)
  {
    if (curr_peer->id != config.identity)
    {
      printf("%d\n\n\n", ntohs(curr_peer->addr.sin_port));
      toaddr.sin_port = curr_peer->addr.sin_port;
      spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr));
    }
  }

  // spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr));
  // toaddr.sin_port = htons(2222);

  // spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr));

  printf("Sent MAGIC: %d\n", 1);
  // }
}

void handle_user_input(char *line, void *cbdata)
{
  char chunkf[128], outf[128];

  bzero(chunkf, sizeof(chunkf));
  bzero(outf, sizeof(outf));

  if (sscanf(line, "GET %120s %120s", chunkf, outf))
  {
    if (strlen(outf) > 0)
    {
      process_get(chunkf, outf);
    }
  }
}

void peer_run(bt_config_t *config)
{
  int sock;
  struct sockaddr_in myaddr;
  fd_set readfds;
  struct user_iobuf *userbuf;

  if ((userbuf = create_userbuf()) == NULL)
  {
    perror("peer_run could not allocate userbuf");
    exit(-1);
  }

  if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1)
  {
    perror("peer_run could not create socket");
    exit(-1);
  }

  bzero(&myaddr, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myaddr.sin_port = htons(config->myport);

  if (bind(sock, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1)
  {
    perror("peer_run could not bind socket");
    exit(-1);
  }
  spiffy_init(config->identity, (struct sockaddr *)&myaddr, sizeof(myaddr));
  upload_to_peer_infos = malloc(sizeof(upload_to_peer_info_t) * config->max_conn);
  download_from_peer_infos = malloc(sizeof(download_from_peer_info_t) * config->max_conn);
  //一次最多10个排队的i have
  waiting_list_of_I_HAVE = malloc(sizeof(waiting_i_have_t) * 10);
  struct timeval next_tv;
  while (1)
  {
    int nfds;
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(sock, &readfds);
    nfds = select(sock + 1, &readfds, NULL, NULL, &next_tv);

    if (/*nfds > 0*/ 0 == 0)
    {
      char c = 0x00;
      if (FD_ISSET(sock, &readfds))
      {
        c = process_inbound_udp(sock);
      }

      if (FD_ISSET(STDIN_FILENO, &readfds))
      {
        process_user_input(STDIN_FILENO, userbuf, handle_user_input,
                           "Currently unused");
      }
      if ((c == 0x00 || c == 0x01 || c == 0x03) && (nfds > 0))
      {
        printf("NOP!!!\n");
        continue;
      }
      struct timeval curr_tv;
      gettimeofday(&curr_tv, NULL);
      if (curr_tv.tv_sec * 1000000 + curr_tv.tv_usec - upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 - upload_to_peer_infos[0].last_sent_time.tv_usec > 200000)
      {
        FILE *fp_master_tar;
        printf("FILE NAME: %s\n", upload_to_peer_infos[0].master_tar_name);
        fp_master_tar = fopen(upload_to_peer_infos[0].master_tar_name, "r+");

        int ack = upload_to_peer_infos[0].max_ack;
        fseek(fp_master_tar, upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024, SEEK_SET);
        printf("%d\n", (upload_to_peer_infos[0].request_id * 512 * 1024 + ack * 1024) / 1024);
        gettimeofday(&(upload_to_peer_infos[0].last_sent_time), NULL);
        printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 + upload_to_peer_infos[0].last_sent_time.tv_usec);

        data_packet_t packet;
        packet.header.magicnum = htons(15441);
        packet.header.version = 0x01;
        packet.header.packet_type = 0x03;
        packet.header.header_len = htons(16);
        packet.header.packet_len = htons(16 + 1024);
        packet.header.seq_num = htonl(ack + 1);

        fread(packet.data, 1, 1024, fp_master_tar);
        send_packet(packet, &(upload_to_peer_infos[0].sockaddr_upload_to));
        fclose(fp_master_tar);
      }
      next_tv.tv_sec = 1 - (curr_tv.tv_sec - upload_to_peer_infos[0].last_sent_time.tv_sec) - 1;
      next_tv.tv_usec = 200000 - (curr_tv.tv_usec - upload_to_peer_infos[0].last_sent_time.tv_usec);
      printf("NEXT TIME: %ld\n", next_tv.tv_sec * 1000000 + next_tv.tv_usec);
    }
  }
}
