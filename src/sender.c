#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h> // included for getaddrinfo
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>

#define DATA_LEN 5000

//unsigned long long int DATA_LEN = 1024;
unsigned long long int byte_sent = 0;
unsigned long long int packet_in_total = 0;

struct header_seg{
    uint64_t seq_number;
    uint64_t ack_number;
    uint64_t data_len;
    uint64_t  start;
    uint64_t  fin;
    char data[DATA_LEN];
};

struct header_seg *packet_window;
int *ack_received;

int cwnd = 1;
int ssthresh = INT_MAX;

void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void pack_data(int sockfd, struct addrinfo *p, char *filename, unsigned long long int ToTransfer){
    FILE *file = fopen(filename, "r");
    if(file == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    (void)sockfd;
    (void)p;  
    int tot_pack = (ToTransfer + (DATA_LEN) - 1) / DATA_LEN;
    packet_in_total = tot_pack;
    uint64_t seq_num = 0;
    for(int count = 0; count < tot_pack ; count++){
        int to_send = DATA_LEN <= byte_sent ? DATA_LEN : byte_sent;

        char buffer[sizeof(struct header_seg)];
        struct header_seg *header = (struct header_seg*)buffer;

        size_t bytes_read = fread(header->data, 1, to_send, file);
        if(bytes_read == -1){
            perror("fread");
            exit(EXIT_FAILURE);
        }

        seq_num += bytes_read;

        header->data[bytes_read] = '\0';
        header->seq_number = htonl(seq_num);
        header->ack_number = htonl(count);
        header->data_len = htonl(bytes_read);
        header->start = htonl(0);
        header->fin = htonl(0);
        packet_window[count] = *header;
        ack_received[count] = 0;

        byte_sent -= to_send;
    }
    printf("%u packets to be sent\n", tot_pack);
    cwnd = 5;
    fclose(file);
    return;
}

int check_ack(){

    for(int i = 0 ; i < packet_in_total; i++){
        if(ack_received[i] == 0){
            return 1;
        }
    }
    return 0;
}

void sendData_recvAck(int sockfd, struct addrinfo *p,unsigned long long int ToTransfer){
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    int index = 0;

    //run until ACK for all packets received
    while(check_ack(ToTransfer)){
        int start_ptr = index;
        int end_ptr = start_ptr + cwnd;

        for(int count = start_ptr ; count <= end_ptr && count < packet_in_total ; count++){
            if(sendto(sockfd, (char*)&packet_window[count], sizeof(struct header_seg), 0, p->ai_addr, p->ai_addrlen) == -1){
                perror("sendto");
                exit(EXIT_FAILURE);
            }
        }
        for(int count = start_ptr ; count <= end_ptr && count < packet_in_total ; count++){
            char buffer[sizeof(struct header_seg)];
            if(recvfrom(sockfd, buffer, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, &addr_len) == -1){
                if(errno != EAGAIN && errno != EWOULDBLOCK){
                    perror("recvfrom");
                    exit(EXIT_FAILURE);
                }
                ssthresh = cwnd / 2;
                cwnd = 10;
                break;
            }
            struct header_seg *header = (struct header_seg*)buffer;
            ack_received[ntohl(header->ack_number)] = 1;
            if(cwnd < ssthresh){
                cwnd *= 2;
            } else {
                cwnd += 1/cwnd;
            }
        }
        //retranmission logic, when duplicate ack received
        for(int i = 0; i < packet_in_total; i++){
            if(ack_received[i] == 0){
                index = i;
                break; 
            }
        }
    }
    printf("File transfer complete\n");
    printf("=============================================\n");
    char last_packet[sizeof(struct header_seg)];
    char last_packet_recieved[sizeof(struct header_seg)];
    struct header_seg *header_3 = (struct header_seg*)last_packet;
    header_3->seq_number = htonl(0);
    header_3->ack_number = htonl(0);
    header_3->data_len = htonl(0);
    header_3->start = htonl(0);
    header_3->fin = htonl(1);
    header_3->data[0] = '\0';
    do{
        if(sendto(sockfd, last_packet, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, addr_len) == -1){
            perror("sendto");
            exit(EXIT_FAILURE);
        }
        else{
            printf("FIN sent\n");
        }
    }
    while(recvfrom(sockfd, last_packet_recieved, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, &addr_len) == -1);
    printf("Connection closed\n");
        //packet loss
}



//Making connection with the receiver

int connect_udp(int sockfd, struct addrinfo *p){
    int n;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    for(int attempts = 0; attempts < 5; attempts++) {
        char buffer[sizeof(struct header_seg)];
        struct header_seg *header = (struct header_seg*)buffer;
        header->seq_number = htonl(0);
        header->ack_number = htonl(0);
        header->data_len = htonl(0);
        header->start = htonl(1);
        header->fin = htonl(0);
        header->data[0] = '\0';

        if ((n = sendto(sockfd, buffer, sizeof(struct header_seg), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("sendto");
            sleep(0.001); // Wait before trying again
            continue;
        }

        char packet[sizeof(struct header_seg)];
        if ((n = recvfrom(sockfd, packet, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            sleep(0.01); // Wait before trying again
            perror("recvfrom");
            continue;
        }
        return 0; // Connection established
    }
    return -1; // Failed to establish connection after 5 attempts
}

/*
  You will use UDP to implement your own version of TCP.
  Your implementation must be able to tolerate packet drops,
  allow other concurrent connections a fair chance, and must
  not be overly nice to other connections (should not give up the
  entire bandwidth to other connections).
*/

/*
  This function should transfer the first bytesToTransfer bytes of
  filename to the receiver at hostname:hostUDPport correctly and
  efficiently, even if the network drops or reorders
*/
void rsend(char* hostname,
            unsigned short int hostUDPport,
            char* filename,
            unsigned long long int bytesToTransfer)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    char port[6]; // Maximum 5 digits for port number + null terminator
    sprintf(port, "%d", hostUDPport);

    if((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return ;
    }

    for(p = servinfo; p != NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("talker: socket");
            continue;
        }
        if(connect_udp(sockfd, p)){
            close(sockfd);
            continue;
        }
        break;
    }
    printf("Handshake complete\n");

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    byte_sent = bytesToTransfer;

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    unsigned long long int total_packets = (bytesToTransfer + (DATA_LEN)- 1) / DATA_LEN;

    packet_window = malloc(total_packets * sizeof(struct header_seg));
    ack_received = malloc(total_packets * sizeof(int));

    pack_data(sockfd, p, filename, bytesToTransfer);
    sendData_recvAck(sockfd, p, bytesToTransfer);

    freeaddrinfo(servinfo);
    close(sockfd);
}

int main(int argc, char** argv) {
    // This is a skeleton of a main function.
    // You should implement this function more completely
    // so that one can invoke the file transfer from the
    // command line.
    int hostUDPport;
    unsigned long long int bytesToTransfer;
    char* hostname = NULL;
    char* filename = NULL;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    hostUDPport = (unsigned short int) atoi(argv[2]);
    hostname = argv[1];
    filename = argv[3];
    //if user input is greater than file size, transfer the whole file
    struct stat st;
    if (stat(filename, &st) == 0) {
        bytesToTransfer = atoll(argv[4]);
        if (bytesToTransfer > st.st_size) {
            bytesToTransfer = st.st_size;
        }
    } else {
        fprintf(stderr, "Failed to get file size\n");
        exit(1);
    }
    rsend(hostname, hostUDPport, filename, bytesToTransfer);
    return (EXIT_SUCCESS);
}