# Reliable UDP Sender and Receiver

This project implements a reliable data transfer protocol over UDP. The protocol is designed to meet the following expectations:

1. **UDP Usage**: The protocol uses `SOCK_DGRAM` (UDP) for data transfer and does not use TCP in any way.
2. **Data Integrity**: The data written to disk by the receiver is exactly the same as the data sent by the sender.
3. **Bandwidth Utilization**: In steady state (averaged over 10 seconds), the protocol utilizes at least 70% of the available bandwidth when there is no competing traffic, and packets are not artificially dropped or reordered.
4. **Fair Sharing**: When two instances of the protocol compete with each other, they converge to roughly fairly sharing the link (same throughputs ±10%), within 100 RTTs. The two instances might not start at the exact same time.
5. **TCP Friendliness**: The protocol is TCP friendly. When an instance of TCP competes with the protocol, the TCP flow receives, on average, at least half as much throughput as the protocol flow.
6. **Competitiveness**: When the protocol competes with a TCP flow, the protocol flow receives, on average, at least half as much throughput as the TCP flow. The protocol is not overly nice.
7. **Packet Loss Resilience**: The protocol maintains its performance in the presence of any amount of packet drops. All flows, including the TCP flows, see the same rate of drops. The network does not introduce bit errors.
8. **Flow Control**: The protocol supports flow control via a sliding window mechanism to ensure that the receiver is not overwhelmed by the sender.

## Implementation

The protocol is implemented in C and consists of two main functions: `rsend` and `rrecv`.

`rsend` is responsible for sending data. It reads the data from a file, breaks it into packets, and sends them over a UDP connection. It uses a sliding window mechanism for flow control and retransmits any packets that are not acknowledged within a certain timeout.

`rrecv` is responsible for receiving data. It receives packets over a UDP connection, writes the data to a file, and sends acknowledgements back to the sender. It also uses a sliding window mechanism to control the flow of data.

Both `rsend` and `rrecv` use sequence numbers and acknowledgements to ensure reliable data transfer. They also handle out-of-order packets and packet loss.

## Usage

To use the protocol, compile the C code and run the sender and receiver programs with the appropriate command line arguments. The sender needs the UDP port, the file name to read from, and the write rate. The receiver needs the UDP port and the file name to write to.

For example:

```bash
gcc sender.c -o sender
gcc receiver.c -o receiver
./sender 12345 data.txt 1000
./receiver 12345 output.txt
```
This will start a sender that reads data from `data.txt` and sends it at a rate of 1000 bytes per second over UDP port 12345. The receiver listens on UDP port 12345 and writes the received data to `output.txt`.


# Testing

In this project, we are using CloudLab, as specified in Assignment 2, to simulate network traffic and test our implementation. The following steps outline our testing process:

## Creating Traffic

We create traffic using the `tc` command, a utility for manipulating traffic control settings in the Linux kernel. The command we use is:

```bash
sudo tc qdisc add dev eth1 root handle 10: tbf rate 20Mbit burst 10mb latency 1ms
```

This command sets up a Token Bucket Filter (TBF) on the `eth1` interface, which limits the rate of data transmission to 20Mbit/s, with a burst size of 10MB and a latency of 1ms.

To clear the existing traffic control settings, we use the following command:

```bash
sudo tc qdisc del dev eth1 root 2>/dev/null
```

## Sending and Receiving Packets

Once the traffic control settings are in place, the receiver starts waiting for packets. The sender then begins sending packets. In the router terminal, we can observe the traffic flow.

## Checking Bandwidth

The maximum traffic flow observed in the router terminal gives us an estimate of the bandwidth.

## Verifying Transfer Reliability

After all the packets have been sent, we verify the reliability of the transfer by comparing the size of the source and destination files. We use the `wc -c` command to count the number of bytes in each file:

```bash
wc -c <source_file>
wc -c <destination_file>
```

If the byte counts of the source and destination files are equal, it indicates that the transfer was reliable and successful.
```
Please note that the nested code blocks in the markdown might not render correctly in some markdown viewers. You might need to adjust the formatting to suit your specific needs.