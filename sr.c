#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2  

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications: 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE 12      /* The SR protocol requires that the sequence space is greater than or equal to 2× the window size */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt sender_buffer[SEQSPACE]; /* array for storing packets waiting for ACK */
static bool acked[SEQSPACE];    /* array indexes of the first/last packet awaiting ACK */
static int base = 0;               /* the number of packets currently awaiting an ACK */
static int nextseqnum = 0;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
    if ((nextseqnum + SEQSPACE - base) % SEQSPACE >= WINDOWSIZE) {
        if (TRACE > 0)
            printf("----A: Window full, message dropped\n");
        window_full++;
        return;
    }

    struct pkt pkt;
    pkt.seqnum = nextseqnum;
    pkt.acknum = NOTINUSE;
    for (int i = 0; i < 20; i++) {
        pkt.payload[i] = message.data[i];
    }
    pkt.checksum = ComputeChecksum(pkt);

    sender_buffer[nextseqnum] = pkt;
    acked[nextseqnum] = false;

    tolayer3(A, pkt);
    if (TRACE > 0)
        printf("----A: Sent packet with seqnum %d\n", pkt.seqnum);

    starttimer(A, RTT);

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
}


/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: Corrupted ACK %d received, ignoring\n", packet.acknum);
        return;
    }

    if (TRACE > 0)
        printf("----A: Received valid ACK for seqnum %d\n", packet.acknum);

    total_ACKs_received++;

    int acknum = packet.acknum;

    // Ignore if already acknowledged
    if (acked[acknum]) {
        if (TRACE > 0)
            printf("----A: Duplicate ACK for seqnum %d, ignoring\n", acknum);
        return;
    }

    acked[acknum] = true;
    new_ACKs++;

    // Slide window base forward if base is ACKed
    while (acked[base]) {
        base = (base + 1) % SEQSPACE;
    }

    // If all packets are ACKed, stop the timer
    bool has_unacked = false;
    for (int i = 0; i < WINDOWSIZE; i++) {
        int index = (base + i) % SEQSPACE;
        if (!acked[index] && ((nextseqnum + SEQSPACE - index) % SEQSPACE < WINDOWSIZE)) {
            has_unacked = true;
            break;
        }
    }

    stoptimer(A);
    if (has_unacked) {
        starttimer(A, RTT);
    }
}


/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    if (TRACE > 0)
        printf("----A: Timer interrupt, scanning for unACKed packets\n");

    for (int i = 0; i < WINDOWSIZE; i++) {
        int index = (base + i) % SEQSPACE;

        if ((index == nextseqnum) || acked[index]) {
            continue;
        }

        // Resend the first unACKed packet in window
        tolayer3(A, sender_buffer[index]);
        packets_resent++;

        if (TRACE > 0)
            printf("----A: Resending packet %d due to timeout\n", index);

        // Restart timer after resending
        starttimer(A, RTT);
        return;  // Only resend one packet per timer event
    }

    // No unACKed packets, stop timer
    stoptimer(A);
}




/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
    // Initialize sender's base and next sequence number
    base = 0;
    nextseqnum = 0;

    // Clear the sender buffer and acknowledgment flags
    for (int i = 0; i < SEQSPACE; i++) {
        acked[i] = false;
    }

    if (TRACE > 0)
        printf("----A: Selective Repeat sender initialized\n");
}




/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum = 0;  // The next expected sequence number at receiver
static int B_acknum = 0;        // For alternating seqnum in ACKs
static struct pkt receiver_buffer[SEQSPACE];  // buffer for out-of-order packets
static bool received[SEQSPACE];               // bitmap to track which packets are stored



/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    struct pkt ackpkt;

    if (IsCorrupted(packet)) {
        if (TRACE > 0)
//             printf("----B: Received corrupted packet, resend ACK %d\n", B_acknum);

        ackpkt.seqnum = 0;
        ackpkt.acknum = B_acknum;
        for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
        ackpkt.checksum = ComputeChecksum(ackpkt);
        tolayer3(B, ackpkt);
        return;
    }

    int seq = packet.seqnum;

    // Store if not received before
    // Avoid duplicate storage
    if (!received[seq]) {
        receiver_buffer[seq] = packet;
        received[seq] = true;

        if (TRACE > 0)
            printf("----B: Buffered new packet %d\n", seq);
    } else {
        if (TRACE > 0)
            printf("----B: Duplicate packet %d received, already buffered\n", seq);
    }


    // Always send ACK
    ackpkt.seqnum = 0;
    // Use last in-order received packet as ACK
    ackpkt.acknum = (expectedseqnum + SEQSPACE - 1) % SEQSPACE;

    for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);

    if (TRACE > 0)
        printf("----B: ACK sent for packet %d\n", seq);

    // Deliver all in-order packets starting from expectedseqnum
    while (received[expectedseqnum]) {
        tolayer5(B, receiver_buffer[expectedseqnum].payload);

        // Clear buffer to avoid duplicate delivery
        received[expectedseqnum] = false;

        if (TRACE > 0)
//             printf("----B: Delivered buffered packet %d to application\n", expectedseqnum);

        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        packets_received++;
        B_acknum = expectedseqnum;
    }

}


/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    expectedseqnum = 0;
    B_acknum = 0;

    for (int i = 0; i < SEQSPACE; i++) {
        received[i] = false;
    }

    if (TRACE > 0)
        printf("----B: Receiver initialized\n");
}


/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

