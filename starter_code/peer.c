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
#include "utils.h"

void peer_run(bt_config_t *config);

bt_config_t config;
upload_to_peer_info_t *upload_to_peer_infos;
download_from_peer_info_t *download_from_peer_infos;
waiting_i_have_t *waiting_list_of_I_HAVE;
chunk_status_t *chunk_status;
int chunk_num;
char outputfile_name[BT_FILENAME_LEN];
char requestfile_name[BT_FILENAME_LEN];

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

  return curr->header.packet_type;
}

void process_get(char *chunkfile, char *outputfile)
{
  printf("\n============SEND WHOHAS=================\n\n");
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in toaddr, myaddr;
  bzero(&myaddr, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &myaddr.sin_addr);
  bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
  inet_aton("127.0.0.1", &toaddr.sin_addr);
  toaddr.sin_family = AF_INET;

  memset(outputfile_name, 0, BT_FILENAME_LEN);
  memcpy(outputfile_name, outputfile, sizeof(char) * strlen(outputfile));
  memset(requestfile_name, 0, BT_FILENAME_LEN);
  memcpy(requestfile_name, chunkfile, sizeof(char) * strlen(chunkfile));

  //发送WHOHAS
  data_packet_t packet;
  //搜索要找的的hash值
  FILE *fp;
  fp = fopen(chunkfile, "r+");
  char bbf[60];
  unsigned char *bufp = packet.data + 4;
  for (int i = 0; i < 1000; i++)
  {
    packet.data[i] = 0x00;
  }
  // 判断有几个chunk hash
  int count = 0;
  while (fgets(bbf, 60, fp))
  {
    count++;
  }
  fseek(fp, 0, SEEK_SET);
  chunk_status = malloc(sizeof(chunk_status_t) * count);
  chunk_num = count;
  packet.header = make_packet_header(PACKET_TYPE_WHOHAS, LEN_OF_PACKET_HEADER, count * LEN_OF_CHUNK_HASH + LEN_OF_PACKET_HEADER + 4, -1, -1);
  packet.data[0] = count;
  packet.data[1] = 0x00;
  packet.data[2] = 0x00;
  packet.data[3] = 0x00;
  count = 0;
  // 遍历chunk file，并将hash值填入WHOHAS报文中
  while (fgets(bbf, 60, fp))
  {
    char *hash_name;
    hash_name = strchr(bbf, ' ') + 1;
    hash_name[strlen(hash_name) - 1] = 0;
    unsigned char *hash_binary = malloc(sizeof(unsigned char) * 20);
    hex2binary(hash_name, strlen(hash_name), hash_binary);

    char request_id_char[10];
    memcpy(request_id_char, bbf, strchr(bbf, ' ') - bbf);
    chunk_status_t *this_chunk_status = malloc(sizeof(chunk_status_t));
    this_chunk_status->id = atoi(request_id_char);
    this_chunk_status->status = 0;
    memcpy(this_chunk_status->hash, hash_binary, 20);
    memcpy(chunk_status + count, this_chunk_status, sizeof(chunk_status_t));
    count++;

    memcpy(bufp, hash_binary, LEN_OF_CHUNK_HASH);
    bufp += LEN_OF_CHUNK_HASH;
    free(hash_binary);
  }
  char *bb = malloc(sizeof(char) * LEN_OF_CHUNK_HASH * 4);
  binary2hex(packet.data, LEN_OF_CHUNK_HASH * 2, bb);
  printf("%s\n", bb);
  free(bb);
  *bufp = 0;

  //遍历peers，向除自己以外的所有peer发送WHOHAS报文
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
  setbuf(stdout, NULL);
  spiffy_init(config->identity, (struct sockaddr *)&myaddr, sizeof(myaddr));
  upload_to_peer_infos = malloc(sizeof(upload_to_peer_info_t) * config->max_conn);
  memset(upload_to_peer_infos, 0, sizeof(upload_to_peer_info_t) * config->max_conn);
  download_from_peer_infos = malloc(sizeof(download_from_peer_info_t) * config->max_conn);
  memset(download_from_peer_infos, 0, sizeof(download_from_peer_info_t) * config->max_conn);
  //一次最多10个排队的i have
  waiting_list_of_I_HAVE = malloc(sizeof(waiting_i_have_t) * 10);
  memset(waiting_list_of_I_HAVE, 0, sizeof(waiting_i_have_t) * 10);
  struct timeval next_tv;
  next_tv.tv_sec = 1000000;
  while (1)
  {
    int nfds;
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(sock, &readfds);
    nfds = select(sock + 1, &readfds, NULL, NULL, &next_tv);

    if (/*nfds > 0*/ 0 == 0)
    {
      try_send_GET_in_waiting_list();
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
        continue;
      }
      struct timeval curr_tv;
      gettimeofday(&curr_tv, NULL);
      if (curr_tv.tv_sec * 1000000 + curr_tv.tv_usec - upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 - upload_to_peer_infos[0].last_sent_time.tv_usec > 1000000)
      {
        FILE *fp_master_tar;
        printf("FILE NAME: %s\n", upload_to_peer_infos[0].master_tar_name);
        fp_master_tar = fopen(upload_to_peer_infos[0].master_tar_name, "r+");

        int ack = upload_to_peer_infos[0].max_ack;
        fseek(fp_master_tar, upload_to_peer_infos[0].request_id * BT_CHUNK_SIZE + ack * LEN_OF_SENT_DATA, SEEK_SET);
        printf("%d\n", (upload_to_peer_infos[0].request_id * BT_CHUNK_SIZE + ack * LEN_OF_SENT_DATA) / LEN_OF_SENT_DATA);
        gettimeofday(&(upload_to_peer_infos[0].last_sent_time), NULL);
        printf("LAST MODIFIED TIME: %ld\n", upload_to_peer_infos[0].last_sent_time.tv_sec * 1000000 + upload_to_peer_infos[0].last_sent_time.tv_usec);

        data_packet_t packet;
        packet.header = make_packet_header(PACKET_TYPE_DATA, LEN_OF_PACKET_HEADER, LEN_OF_PACKET_HEADER + LEN_OF_SENT_DATA, ack + 1, -1);

        fread(packet.data, 1, LEN_OF_SENT_DATA, fp_master_tar);
        send_packet(packet, &(upload_to_peer_infos[0].sockaddr_upload_to));
        fclose(fp_master_tar);
      }
      next_tv.tv_sec = 1 - (curr_tv.tv_sec - upload_to_peer_infos[0].last_sent_time.tv_sec) - 1;
      next_tv.tv_usec = 1000000 - (curr_tv.tv_usec - upload_to_peer_infos[0].last_sent_time.tv_usec);
      printf("NEXT TIME: %ld\n", next_tv.tv_sec * 1000000 + next_tv.tv_usec);
    }
  }
}
