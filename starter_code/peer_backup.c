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
void peer_run(bt_config_t *config);

typedef struct header_s
{
  short magicnum;
  char version;
  char packet_type;
  short header_len;
  short packet_len;
  u_int seq_num;
  u_int ack_num;
} header_t;

typedef struct data_packet
{
  header_t header;
  unsigned char data[1000];
} data_packet_t;
bt_config_t config;

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

void print_received_packet_info(data_packet_t *curr)
{
  printf("MAGIC: %d\n", ntohs((curr->header).magicnum));
  printf("VERSION: %x\n", curr->header.version);
  printf("PACKET TYPE: %x\n", curr->header.packet_type);
  printf("HEADER LENGTH: %d\n", curr->header.header_len);
  printf("TOTAL PACKET LENGTH: %d\n", curr->header.packet_len);
}

data_packet_t deal_with_WHOHAS(data_packet_t *curr)
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

  return packet;
}

void process_inbound_udp(int sock)
{
  setbuf(stdout, NULL);

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

  data_packet_t packet;
  if (curr->header.packet_type == 0x00)
  {
    packet = deal_with_WHOHAS(curr);
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in toaddr, myaddr;
  setbuf(stdout, NULL);
  bzero(&myaddr, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &myaddr.sin_addr);
  bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
  inet_aton("127.0.0.1", &toaddr.sin_addr);
  toaddr.sin_port = htons(1111);
  toaddr.sin_family = AF_INET;
  spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr));

  fflush(stdout);

  printf("PROCESS_INBOUND_UDP SKELETON -- replace!\n"
         "Incoming message from %s:%d\n%s\n\n",
         inet_ntoa(from.sin_addr),
         ntohs(from.sin_port),
         buf);
}

void process_get(char *chunkfile, char *outputfile)
{
  printf("PROCESS GET SKELETON CODE CALLED.  Fill me in!  (%s, %s)\n",
         chunkfile, outputfile);
  printf("\n============SEND WHOHAS=================\n\n");
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in toaddr, myaddr;
  setbuf(stdout, NULL);
  bzero(&myaddr, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &myaddr.sin_addr);
  bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr));
  inet_aton("127.0.0.1", &toaddr.sin_addr);
  toaddr.sin_port = htons(1111);
  toaddr.sin_family = AF_INET;

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

  // packet.data[4] = 0x0e;
  // packet.data[5] = 0xdb;
  // packet.data[6] = 0x65;
  // packet.data[7] = 0x82;
  // packet.data[8] = 0x8c;
  // packet.data[9] = 0xdf;
  // packet.data[10] = 0x65;
  // packet.data[11] = 0xbd;
  // packet.data[12] = 0xb2;
  // packet.data[13] = 0xd6;
  // packet.data[14] = 0xf9;
  // packet.data[15] = 0xd2;
  // packet.data[16] = 0x69;
  // packet.data[17] = 0xcf;
  // packet.data[18] = 0x49;
  // packet.data[19] = 0x90;
  // packet.data[20] = 0x65;
  // packet.data[21] = 0xbb;
  // packet.data[22] = 0x71;
  // packet.data[23] = 0x6a;
  // packet.data[24] = 0x09;
  // packet.data[25] = 0x19;
  // packet.data[26] = 0x71;
  // packet.data[27] = 0x62;
  // packet.data[28] = 0xeb;
  // packet.data[29] = 0x7b;
  // packet.data[30] = 0x1f;
  // packet.data[31] = 0x2c;
  // packet.data[32] = 0x04;
  // packet.data[33] = 0x07;
  // packet.data[34] = 0x13;
  // packet.data[35] = 0x3b;
  // packet.data[36] = 0xea;
  // packet.data[37] = 0x05;
  // packet.data[38] = 0x37;
  // packet.data[39] = 0x2b;
  // packet.data[40] = 0x2c;
  // packet.data[41] = 0xe4;
  // packet.data[42] = 0x59;
  // packet.data[43] = 0xde;

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

  spiffy_sendto(fd, &packet, sizeof(data_packet_t), 0, (struct sockaddr *)&toaddr, sizeof(toaddr));
  printf("Sent MAGIC: %d\n", 1);
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
  while (1)
  {
    setbuf(stdout, NULL);
    int nfds;
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(sock, &readfds);
    nfds = select(sock + 1, &readfds, NULL, NULL, NULL);

    if (nfds > 0)
    {
      if (FD_ISSET(sock, &readfds))
      {
        process_inbound_udp(sock);
      }

      if (FD_ISSET(STDIN_FILENO, &readfds))
      {
        process_user_input(STDIN_FILENO, userbuf, handle_user_input,
                           "Currently unused");
      }
    }
  }
}
