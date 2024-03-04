/**
 * @file receiver.c
 * @brief Implementation of a UDP receiver for file transfer
 *
 * This file contains the implementation of a UDP receiver used for file transfer.
 * It includes functions to handle receiving packets, writing data to a file, and
 * sending acknowledgment packets.
 *
 * @author [Lakshya Saroha] ([Lakshya013])
 * @author [Madhav Kapoor] ([madhavkapoor1])
 * @bug No known bugs just don't handle different writerates.
 */

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

#define MAXBUFLEN 2000*30
#define DATA_LEN 2000

int counter = 0;
int total = 0;

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

/**
 * @brief Retrieves the IP address from a sockaddr structure
 *
 * @param sa Pointer to the sockaddr structure
 * @return Pointer to the IP address
 */
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
 * @brief Receives TCP packets , implemented using UDP, and handles them accordingly
 *
 * This function listens for packets on the specified port, receives them,
 * and processes them according to the protocol. It handles both in-order and
 * out-of-order packets, sends acknowledgments, and writes received data to
 * the specified file. Additionally, it performs handshaking with the sender
 * to establish and terminate the connection.
 * It write on the file only if the packet is in order.
 * else send the duplicate acks.
 *
 * @param myUDPport The UDP port to listen on
 * @param destinationFile The file to write the received data to
 * @param writeRate The rate at which data should be written (unused)
 */
void rrecv(unsigned short int myUDPport,
            char* destinationFile,
            unsigned long long int writeRate) {
    int sockfd;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    (void )writeRate;

    struct sockaddr_in my_addr;

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(myUDPport);
    my_addr.sin_addr.s_addr = INADDR_ANY; // use my IP
    memset(my_addr.sin_zero, '\0', sizeof my_addr.sin_zero);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("listener: socket");
        return;
    }

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof my_addr) == -1) {
        close(sockfd);
        perror("listener: bind");
        return;
    }

    printf("listener: waiting to recvfrom...\n");
    while(1){
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
            (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }

        struct header_seg *header = (struct header_seg*)buf;
        char data[DATA_LEN];
        memcpy(data, header->data, DATA_LEN);

        if(ntohl(header->fin)){
            counter = 0;
            memset(buf, 0, MAXBUFLEN);
            printf("Connection Closed\n");
            char packet[sizeof(struct header_seg)]; // +1 for null-terminator
            if((numbytes = sendto(sockfd, packet, sizeof(struct header_seg), 0,
            (struct sockaddr *)&their_addr, addr_len)) == -1) {
            perror("talker: sendto");
            exit(1);
            }
            exit(1);
        }

        char packet[sizeof(struct header_seg)]; // +1 for null-terminator

        struct header_seg *header_2 = (struct header_seg*)packet;
        header_2->seq_number = ((header->seq_number));
        header_2->ack_number = ((header->ack_number));
        header_2->data_len = ((header->data_len));

        strncpy(header_2->data, "Received!", DATA_LEN);

        if(ntohl(header->start)){
            printf("Hand Shaking!\n");
            printf("=============================================\n");
            char packet[sizeof(struct header_seg)]; // +1 for null-terminator
            if((numbytes = sendto(sockfd, packet, sizeof(struct header_seg), 0,
            (struct sockaddr *)&their_addr, addr_len)) == -1) {
            perror("talker: sendto");
            exit(1);
            }
        }

        //sending reply
        if(ntohl(header->ack_number) == counter && !ntohl(header->start)){
            // ("In order Packet");
            // ("ACK sent for ");
            total += strlen(data);
            counter++;
            FILE *file = fopen(destinationFile, "a+");
            if(file == NULL){
                printf("Error opening file!\n");
                exit(1);
            }
            fputs(data, file);
            fclose(file);
            if((numbytes = sendto(sockfd, packet, sizeof(struct header_seg), 0,
            (struct sockaddr *)&their_addr, addr_len)) == -1) {
            perror("talker: sendto");
            exit(1);
            }
        }
        else{
            // ("Out of order Packet");
            // ("Duplicate ACK sent");
            for(int i = 0; i<2;i++){
                if (counter - 2 != 0) {
                    header_2->ack_number = htonl(counter);
                } else {
                    header_2->ack_number = htonl(counter - 2);
                }
                if((numbytes = sendto(sockfd, buf, sizeof(struct header_seg), 0,
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
                perror("talker: sendto");
                exit(1);
                }
            }
            memset(buf, 0, MAXBUFLEN);// clear the buffer
        }
    //sending reply
    }
    close(sockfd);
    return;
}

/**
 * @brief Main function
 *
 * The main function of the receiver program. It parses command-line arguments
 * and invokes the rrecv function to start receiving data.
 *
 * @param argc Number of command-line arguments
 * @param argv Array of command-line arguments
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char** argv) {
    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);
    rrecv(udpPort, argv[2], 0);
    return 0;
}