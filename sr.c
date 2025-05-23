#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
    Selected Repeat (SR) protocol.  Adapted from J.F.Kurose
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
    - added SR implementation
**********************************************************************/

/* Key differences from Go-Back-N:
    - SR only retransmits the specific packet that was lost or corrupted,
      not everything after it.
    - The receiver can buffer out-of-order packets and wait for the missing ones.
    - Each packet gets its own ACK, instead of cumulative ACKs.
    - Needs a bigger sequence number space (at least 2 × window size).
*/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
                        /* still missing messages, test with adjust this*/ 
#define SEQSPACE 12      /* this one is different from GBN, should be time windowsize with 2*/
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
    the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
    original checksum.  This procedure must generate a different checksum to the original  if
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
/* */

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static bool acked_pkt[WINDOWSIZE];          /* same length with window size, result like [0,1,1,0...]*/

/* successfully test, this one doesn't need adjusted*/
/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ )
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    /* windowlast will always be 0 for alternating bit; but not for GoBackN */
    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
    In this practical this will always be an ACK as B never sends data.
*/
/* for GBN, the ack is accumulated checking*/
/* for SR, the ack is a process of One by ONe, so array needed to check the ack for each*/

void A_input(struct pkt packet)
{
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    if (!acked_pkt[packet.acknum]) {
      int packets_to_remove = 0; 
      int i;
      if (TRACE > 0)
      printf("----A: ACK %d is not a duplicate\n", packet.acknum);
      new_ACKs++;
      acked_pkt[packet.acknum] = true;
  
      /*check the acked_pkt from windowfirst*/
      
      for (i = 0; i < windowcount; i++) {
        int current_index = (windowfirst + i) % WINDOWSIZE;
        if (acked_pkt[buffer[current_index].seqnum]) {
          packets_to_remove++;
        } else {
          break;
        }
      }
  
      /* window slide, update windowfirst and windowcount numbers */
      if (packets_to_remove > 0) {
        windowfirst = (windowfirst + packets_to_remove) % WINDOWSIZE;
        windowcount -= packets_to_remove;
        stoptimer(A);
        if (windowcount > 0)
          starttimer(A, RTT);
      }
    }
    else if (TRACE > 0)
      printf("----A: duplicate ACK received, do nothing!\n");
  }
  else if (TRACE > 0)
    printf("----A: corrupted ACK is received, do nothing!\n");
}

/* diff with GBN, not going through all, just need to resend the very left unacked pktt*/
void A_timerinterrupt(void)
{

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  if (TRACE > 0)
    printf ("---A: resending packet %d\n", buffer[(windowfirst)].seqnum);

  tolayer3(A,buffer[(windowfirst)]);
  packets_resent++;
  if (windowcount != 0)
    starttimer(A,RTT);
}


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
/* just migrate from GBN*/
void A_init(void)
{
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */
static bool received[SEQSPACE]; /* track the received pkts*/
static struct pkt received_pkts[SEQSPACE]; /* buffer for storing pkts*/

/* called from layer 3, when a packet arrives for layer 4 at B*/
/* got a buffer for the loss pkt and store*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  if  (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application but check the receiving status of pkt first*/
    if (received[packet.seqnum] == false) {
      received[packet.seqnum] = true; /* if not received before then change the status*/
      for (i = 0; i < 20; i++)
        received_pkts[packet.seqnum].payload[i] = packet.payload[i]; /* struct the pkt to the pre-defined buffer */
    }

    while (received[expectedseqnum] == true) {
      tolayer5(B, packet.payload);
      received[expectedseqnum] = false; /* empty the space by updating the status to false*/
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE; /* plus 1 and proceed the while check for true status*/
    }
    /*update sendpkt bits*/
    sendpkt.acknum = packet.seqnum;
    sendpkt.seqnum = NOTINUSE;

    for (i =0; i < 20 ; i++) /* i < 20 because it's predefined in the emulator datasent cahr[20]*/
      sendpkt.payload[i] = '0';
    sendpkt.checksum = ComputeChecksum(sendpkt);

    tolayer3(B, sendpkt);
  }
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