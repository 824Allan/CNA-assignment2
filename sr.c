#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12  // SR需要2倍窗口大小以区分seq
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);
    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

/************* Sender A **************/
static struct pkt buffer[SEQSPACE];
static bool acked[SEQSPACE];
static bool used[SEQSPACE];

static int windowfirst;  // 最早未ACK的分组位置
static int windowlast;   // 最近发送的分组位置
static int windowcount;  // 当前窗口中未ACK的包数量
static int A_nextseqnum; // 下一个要使用的序号

void A_output(struct msg message) {
    if (windowcount >= WINDOWSIZE) {
        if (TRACE > 0) printf("A_output: Window full, message dropped.\n");
        window_full++;
        return;
    }

    struct pkt p;
    p.seqnum = A_nextseqnum;
    p.acknum = NOTINUSE;
    memcpy(p.payload, message.data, 20);
    p.checksum = ComputeChecksum(p);

    buffer[A_nextseqnum] = p;
    acked[A_nextseqnum] = false;
    used[A_nextseqnum] = true;

    tolayer3(A, p);
    if (TRACE > 0) printf("A_output: Sent pkt %d\n", p.seqnum);

    if (windowcount == 0) starttimer(A, RTT);
    windowcount++;

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
    windowlast = (windowlast + 1) % WINDOWSIZE;
}

void A_input(struct pkt packet) {
    if (IsCorrupted(packet)) {
        if (TRACE > 0) printf("A_input: Corrupted ACK %d\n", packet.acknum);
        return;
    }

    int ack = packet.acknum;
    if (!acked[ack] && used[ack]) {
        acked[ack] = true;
        new_ACKs++;
        total_ACKs_received++;
        if (TRACE > 0) printf("A_input: ACK %d received\n", ack);
    }

    while (windowcount > 0 && acked[buffer[windowfirst].seqnum]) {
        acked[buffer[windowfirst].seqnum] = false;
        used[buffer[windowfirst].seqnum] = false;
        windowfirst = (windowfirst + 1) % WINDOWSIZE;
        windowcount--;
    }

    stoptimer(A);
    if (windowcount > 0) starttimer(A, RTT);
}

void A_timerinterrupt(void) {
    if (TRACE > 0) printf("A_timerinterrupt: Resending all unACKed packets\n");
    for (int i = 0; i < SEQSPACE; i++) {
        if (used[i] && !acked[i]) {
            tolayer3(A, buffer[i]);
            packets_resent++;
            if (TRACE > 0) printf("Resent pkt %d\n", buffer[i].seqnum);
        }
    }
    starttimer(A, RTT);
}

void A_init(void) {
    for (int i = 0; i < SEQSPACE; i++) {
        acked[i] = false;
        used[i] = false;
    }
    A_nextseqnum = 0;
    windowfirst = 0;
    windowlast = -1;
    windowcount = 0;
}

/************* Receiver B **************/
static struct pkt recv_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expectedseqnum;

void B_input(struct pkt packet) {
    struct pkt ackpkt;
    int seq = packet.seqnum;

    if (IsCorrupted(packet)) {
        if (TRACE > 0) printf("B_input: Corrupted packet %d\n", seq);
        return;
    }

    if (!received[seq]) {
        recv_buffer[seq] = packet;
        received[seq] = true;
        if (TRACE > 0) printf("B_input: Packet %d stored\n", seq);
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = seq;
    memset(ackpkt.payload, 0, 20);
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);

    while (received[expectedseqnum]) {
        tolayer5(B, recv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        packets_received++;
    }
}

void B_init(void) {
    for (int i = 0; i < SEQSPACE; i++) received[i] = false;
    expectedseqnum = 0;
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
