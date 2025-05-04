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
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for(i=0; i<windowcount; i++) {

    if (TRACE > 0)
      printf ("---A: resending packet %d\n", (buffer[(windowfirst+i) % WINDOWSIZE]).seqnum);

    tolayer3(A,buffer[(windowfirst+i) % WINDOWSIZE]);
    packets_resent++;
    if (i==0) starttimer(A,RTT);
  }
}       



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application */
    tolayer5(B, packet.payload);

    /* send an ACK for the received packet */
    sendpkt.acknum = expectedseqnum;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;        
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
    
  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ ) 
    sendpkt.payload[i] = '0';  

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  /* send out packet */
  tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
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

