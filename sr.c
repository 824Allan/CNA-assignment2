#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    for (int i = 0; i < 20; i++)
        checksum += (int)(packet.payload[i]);
    checksum += packet.seqnum + packet.acknum;
    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

static struct pkt sender_buffer[SEQSPACE];
static bool acked[SEQSPACE];
static int base = 0;
static int nextseqnum = 0;

static struct pkt receiver_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expectedseqnum = 0;
static int B_acknum = 0;

void A_output(struct msg message) {
    if ((nextseqnum + SEQSPACE - base) % SEQSPACE >= WINDOWSIZE) {
        if (TRACE > 0)
            printf("----A: Window full, message dropped\n");
        window_full++;
        return;
    }

    struct pkt pkt;
    pkt.seqnum = nextseqnum;
    pkt.acknum = NOTINUSE;
    for (int i = 0; i < 20; i++)
        pkt.payload[i] = message.data[i];
    pkt.checksum = ComputeChecksum(pkt);

    sender_buffer[nextseqnum] = pkt;
    acked[nextseqnum] = false;

    tolayer3(A, pkt);
    if (TRACE > 0)
        printf("----A: Sent packet with seqnum %d\n", pkt.seqnum);

    if (base == nextseqnum)
        starttimer(A, RTT);

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet) {
    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: Corrupted ACK %d received, ignoring\n", packet.acknum);
        return;
    }

    int acknum = packet.acknum;
    if (!acked[acknum]) {
        acked[acknum] = true;
        new_ACKs++;
    } else {
        if (TRACE > 0)
            printf("----A: Duplicate ACK %d received, ignoring\n", acknum);
        return;
    }

    while (acked[base])
        base = (base + 1) % SEQSPACE;

    stoptimer(A);
    for (int i = 0; i < WINDOWSIZE; i++) {
        int idx = (base + i) % SEQSPACE;
        if (!acked[idx]) {
            starttimer(A, RTT);
            break;
        }
    }
}

void A_timerinterrupt(void) {
    if (TRACE > 0)
        printf("----A: Timer interrupt, checking unACKed packets\n");

    for (int i = 0; i < WINDOWSIZE; i++) {
        int index = (base + i) % SEQSPACE;
        if (!acked[index] && sender_buffer[index].checksum != 0) {
            tolayer3(A, sender_buffer[index]);
            packets_resent++;
            if (TRACE > 0)
                printf("----A: Resending packet %d\n", index);
            starttimer(A, RTT);
            return;
        }
    }
    stoptimer(A);
}

void A_init(void) {
    base = 0;
    nextseqnum = 0;
    for (int i = 0; i < SEQSPACE; i++)
        acked[i] = false;
    if (TRACE > 0)
        printf("----A: Sender initialized\n");
}

void B_input(struct pkt packet) {
    struct pkt ackpkt;

    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----B: Corrupted packet, resending ACK %d\n", B_acknum);
        ackpkt.seqnum = 0;
        ackpkt.acknum = B_acknum;
        for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
        ackpkt.checksum = ComputeChecksum(ackpkt);
        tolayer3(B, ackpkt);
        return;
    }

    int seq = packet.seqnum;

    if (!received[seq]) {
        receiver_buffer[seq] = packet;
        received[seq] = true;
        if (TRACE > 0)
            printf("----B: Buffered packet %d\n", seq);
    } else {
        if (TRACE > 0)
            printf("----B: Duplicate packet %d, already buffered\n", seq);
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = seq;
    for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);

    if (TRACE > 0)
        printf("----B: ACK sent for packet %d\n", seq);

    while (received[expectedseqnum]) {
        tolayer5(B, receiver_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        if (TRACE > 0)
            printf("----B: Delivered packet %d\n", expectedseqnum);
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        packets_received++;
        B_acknum = expectedseqnum;
    }
}

void B_init(void) {
    expectedseqnum = 0;
    B_acknum = 0;
    for (int i = 0; i < SEQSPACE; i++)
        received[i] = false;
    if (TRACE > 0)
        printf("----B: Receiver initialized\n");
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
