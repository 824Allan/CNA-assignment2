/* sr.c - Selective Repeat Protocol Implementation */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

/* --- Sender State --- */
static struct pkt buffer[SEQSPACE];
static bool acked[SEQSPACE];
static bool in_use[SEQSPACE];
static float send_times[SEQSPACE];
static int A_base;
static int A_nextseqnum;

/* --- Receiver State --- */
static struct pkt recv_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expectedseqnum;

int ComputeChecksum(struct pkt packet)
{
    int checksum = packet.seqnum + packet.acknum;
    int i;
    for (i = 0; i < 20; i++)
        checksum += packet.payload[i];
    return checksum;
}

bool IsCorrupted(struct pkt packet)
{
    return ComputeChecksum(packet) != packet.checksum;
}

void A_output(struct msg message)
{
    if (((A_nextseqnum + SEQSPACE - A_base) % SEQSPACE) < WINDOWSIZE) {
        struct pkt sendpkt;
        int i;
        sendpkt.seqnum = A_nextseqnum;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++)
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);

        buffer[A_nextseqnum] = sendpkt;
        in_use[A_nextseqnum] = true;
        acked[A_nextseqnum] = false;
        send_times[A_nextseqnum] = 0.0;

        tolayer3(A, sendpkt);
        starttimer(A, RTT);

        A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
            printf("A_output: window full\n");
        window_full++;
    }
}

void A_input(struct pkt packet)
{
    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("A_input: corrupted ACK\n");
        return;
    }

    if (in_use[packet.acknum] && !acked[packet.acknum]) {
        acked[packet.acknum] = true;
        new_ACKs++;
        total_ACKs_received++;

        while (acked[A_base]) {
            in_use[A_base] = false;
            stoptimer(A);
            A_base = (A_base + 1) % SEQSPACE;
        }

        if (((A_nextseqnum + SEQSPACE - A_base) % SEQSPACE) > 0)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void)
{
    int i;
    for (i = 0; i < SEQSPACE; i++) {
        if (in_use[i] && !acked[i]) {
            tolayer3(A, buffer[i]);
            packets_resent++;
        }
    }
    starttimer(A, RTT);
}

void A_init(void)
{
    int i;
    A_base = 0;
    A_nextseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        in_use[i] = false;
        acked[i] = false;
        send_times[i] = 0.0;
    }
}

void B_input(struct pkt packet)
{
    struct pkt ackpkt;
    int i;

    if (!IsCorrupted(packet)) {
        if (!received[packet.seqnum]) {
            recv_buffer[packet.seqnum] = packet;
            received[packet.seqnum] = true;
            packets_received++;
        }

        while (received[expectedseqnum]) {
            tolayer5(B, recv_buffer[expectedseqnum].payload);
            received[expectedseqnum] = false;
            expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        }
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = packet.seqnum;
    for (i = 0; i < 20; i++)
        ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
}

void B_init(void)
{
    int i;
    expectedseqnum = 0;
    for (i = 0; i < SEQSPACE; i++)
        received[i] = false;
}
