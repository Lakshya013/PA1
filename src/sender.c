#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h> // included for getaddrinfo
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>

#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>

#define DATA_LEN 1024

unsigned long long int byte_sent = 0;
unsigned long long int packet_in_total = 0;

/**
 * @brief Struct representing the header segment of a packet
 */
struct header_seg{
    uint64_t seq_number; /**< Sequence number */
    uint64_t ack_number; /**< Acknowledgment number */
    uint64_t data_len; /**< Length of data */
    uint64_t  start; /**< Start flag */
    uint64_t  fin; /**< Finish flag */
    char data[DATA_LEN]; /**< Data payload */
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

/**
 * @brief Creates packets from a file to be sent over a network connection.
 * 
 * This function reads data from a file and creates packets to be sent over a network connection.
 * Each packet contains a header segment and a portion of the file's data.
 * The function calculates the total number of packets needed to transfer the entire file.
 * It then iterates over the file, reading a portion of the data and creating a packet for each portion.
 * The packet's header segment is populated with sequence number, acknowledgment number, data length, start flag, and fin flag.
 * The function also updates the packet window and acknowledgment received arrays.
 * Finally, it prints the total number of packets to be sent and closes the file.
 * 
 * @param sockfd The socket file descriptor for the network connection.
 * @param p The address information for the network connection.
 * @param filename The name of the file to read data from.
 * @param ToTransfer The total number of bytes to transfer from the file.
 */
void make_packets(int sockfd, struct addrinfo *p, char *filename, unsigned long long int ToTransfer){
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

        seq_num += to_send;

        char buffer[sizeof(struct header_seg)];
        struct header_seg *header = (struct header_seg*)buffer;

        size_t bytes_read = fread(header->data, 1, to_send, file);
        if(bytes_read == -1){
            perror("fread");
            exit(EXIT_FAILURE);
        }
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
    fclose(file);
    return;
}

/**
 * Checks if all the packets have been acknowledged.
 * 
 * @return 1 if there are unacknowledged packets, 0 otherwise.
 */
int check_ack(){
    for(int i = 0 ; i < packet_in_total; i++){
        if(ack_received[i] == 0){
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Sends and receives data over a socket.
 *
 * This function sends packets of data over the specified socket and receives acknowledgments
 * from the receiver. It uses a sliding window protocol to control the number of packets sent
 * at a time. The function continues sending packets and receiving acknowledgments until all
 * packets have been acknowledged.
 *
 * @param sockfd The socket file descriptor.
 * @param p Pointer to the address information.
 * @param ToTransfer The total number of packets to transfer.
 */
void send_and_recv_data(int sockfd, struct addrinfo *p,unsigned long long int ToTransfer){
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    
    cwnd = 5;
    int index = 0;
    int flag = 0;
    while(check_ack()){
        int start_ptr = (index < flag) ? index : flag;
        int end_ptr = start_ptr + cwnd;
        //send packets
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
                cwnd = 5;
            }

            struct header_seg *header = (struct header_seg*)buffer;
            if(ack_received[ntohl(header->ack_number)] == 1){
                    flag = ntohl(header->ack_number);
                    break;
                }
            else{
            ack_received[ntohl(header->ack_number)] = 1;
            if(cwnd < ssthresh){
                cwnd += 2;
            } else {
                cwnd += 1;
            }
            }
        }
        
        for (int i = 0; i < packet_in_total; i++) {
            if (ack_received[i] == 0) {
                index = i;
                break;
            } else {
                index = packet_in_total;
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

    for(int attempts = 0; attempts < 5; attempts++) {
    if(sendto(sockfd, last_packet, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, addr_len) == -1){
            perror("sendto");
            sleep(0.001); // Wait before trying again
            continue;
        }
    if (recvfrom(sockfd, last_packet_recieved, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, &addr_len) == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available, skip to the next iteration
            continue;
        } else {
            perror("recvfrom");
            exit(1);
    }
}
    }
    printf("Connection Closed\n");
}
/**
 * Establishes a UDP connection with the specified socket and address information.
 *
 * @param sockfd The socket file descriptor.
 * @param p The address information.
 * @return 0 if the connection is successfully established, -1 otherwise.
 */

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
/**
 * Sends data to a specified host using UDP protocol.
 *
 * @param hostname The hostname or IP address of the destination host.
 * @param hostUDPport The UDP port number of the destination host.
 * @param filename The name of the file to be sent.
 * @param bytesToTransfer The number of bytes to be transferred.
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
    timeout.tv_usec = 100000;

    byte_sent = bytesToTransfer;

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    unsigned long long int total_packets = (bytesToTransfer + (DATA_LEN)- 1) / DATA_LEN;

    packet_window = malloc(total_packets * sizeof(struct header_seg));
    ack_received = malloc(total_packets * sizeof(int));

    make_packets(sockfd, p, filename, bytesToTransfer);
    send_and_recv_data(sockfd, p, bytesToTransfer);

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