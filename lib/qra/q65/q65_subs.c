// q65_subs.c

/* Fortran interface for Q65 codec

   To encode a Q65 message:
   
   integer x(13)        !Message payload, 78 bits as 13 six-bit integers
   integer y(63)        !Codeword, 63 six-bit integers
   call q65_enc(imsg,icodeword)

   To decode a Q65 message:

   parameter (LL=64,NN=63)
   real s3(LL,NN)        !Received energies
   real s3prob(LL,NN)    !Symbol-value probabilities
   integer APmask(13)
   integer APsymbols(13)
   real snr2500
   integer xdec(13)      !Decoded 78-bit message as 13 six-bit integers
   integer irc           !Return code from q65_decode()

   call q65_dec(s3,APmask,APsymbols,s3prob,snr2500,xdec,irc)
*/

#include "qra15_65_64_irr_e23.h"	// QRA code used by Q65
#include "q65.h"
#include <stdio.h>
#include <stdlib.h>

static q65_codec_ds codec;

void q65_enc_(int x[], int y[])
{

  static int first=1;
  if (first) {
    // Set the QRA code, allocate memory, and initialize
    int rc = q65_init(&codec,&qra15_65_64_irr_e23);
    if (rc<0) {
      printf("error in q65_init()\n");
      exit(0);
    }
    first=0;
  }
  // Encode message x[13], producing codeword y[63]
  q65_encode(&codec,y,x);
}

void q65_dec_(float s3[], int APmask[], int APsymbols[], float s3prob[],
	      float* snr2500, int xdec[], int* rc0)
{

/* Input:   s3[LL,NN]       Received energies
 *          APmask[13]      AP information to be used in decoding
 *          APsymbols[13]   Available AP informtion
 * Output:  s3prob[LL,NN]   Symbol-value intrinsic probabilities
 *          snr2500         SNR_2500 of decoded signal, or lower limit
 *          xdec[13]        Decoded 78-bit message as 13 six-bit integers
 *          rc0             Return code from q65_decode()
 */

  int rc;
  int ydec[63];
  float esnodb;
  static int first=1;
  
  if (first) {
    // Set the QRA code, allocate memory, and initialize
    int rc = q65_init(&codec,&qra15_65_64_irr_e23);
    if (rc<0) {
      printf("error in q65_init()\n");
      exit(0);
    }
    first=0;
  }
  //  rc = q65_intrinsics_fastfding(&codec,s3prob,s3,submode,B90,fadingModel);
  rc = q65_intrinsics(&codec,s3prob,s3);
  if(rc<0) {
    printf("error in q65_intrinsics()\n");
    exit(0);
  }

  rc = q65_decode(&codec,ydec,xdec,s3prob,APmask,APsymbols);
  *rc0=rc;
  if(rc<0) {
    printf("Error in q65_decode(), rc = %d\n",rc);
    // rc = -1:  Invalid params
    // rc = -2:  Decode failed
    // rc = -3:  CRC mismatch
    return;
  }

  //  rc = q65_esnodb_fastfading(&codec,&esnodb,ydec,s3);
  rc = q65_esnodb(&codec,&esnodb,ydec,s3);
  if(rc<0) {
    printf("error in q65_esnodb_fastfading()\n");
    exit(0);
  }
  *snr2500 = esnodb - 31.0;
}
