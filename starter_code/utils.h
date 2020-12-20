// void print_received_packet_info(data_packet_t *curr);
header_t make_packet_header(char packet_type, short header_len, short packet_len, u_int seq_num, u_int ack_num);
int check_hash(unsigned char *data, unsigned char *hash);
int search_id_for_hash(char *request_hash_name, char *file_name);
void search_master_tar(char *file_name);
int find_free_upload_info();
int find_free_waiting_entity();
int find_free_download_info();
void try_send_GET_in_waiting_list();