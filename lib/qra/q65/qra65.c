// qra65.c
// QRA65 modes encoding/decoding functions
// 
// (c) 2020 - Nico Palermo, IV3NWV - Microtelecom Srl, Italy
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.
//
//    qracodes is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//    qracodes is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with qracodes source distribution.  
//    If not, see <http://www.gnu.org/licenses/>.

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "qra65.h"
#include "pdmath.h"	


static int	_qra65_crc6(int *x, int sz);
static void _qra65_crc12(int *y, int *x, int sz);


int qra65_init(qra65_codec_ds *pCodec, 	const qracode *pqracode)
{
	// Eb/No value for which we optimize the decoder metric (AWGN/Rayleigh cases)
	const float EbNodBMetric = 2.8f;	
	const float EbNoMetric   = (float)pow(10,EbNodBMetric/10);

	float	R;		// code effective rate (after puncturing)
	int		nm;		// bits per symbol

	if (!pCodec)
		return -1;		// why do you called me?

	if (!pqracode)
		return -2;		// invalid qra code

	if (pqracode->M!=64)
		return -3;		// QRA65 supports only codes over GF(64)

	pCodec->pQraCode = pqracode;

	// allocate buffers used by encoding/decoding functions
	pCodec->x			= (int*)malloc(pqracode->K*sizeof(int));
	pCodec->y			= (int*)malloc(pqracode->N*sizeof(int));
	pCodec->qra_v2cmsg	= (float*)malloc(pqracode->NMSG*pqracode->M*sizeof(float));
	pCodec->qra_c2vmsg	= (float*)malloc(pqracode->NMSG*pqracode->M*sizeof(float));
	pCodec->ix			= (float*)malloc(pqracode->N*pqracode->M*sizeof(float));
	pCodec->ex			= (float*)malloc(pqracode->N*pqracode->M*sizeof(float));

	if (pCodec->x== NULL			||
		pCodec->y== NULL			||
		pCodec->qra_v2cmsg== NULL	||
		pCodec->qra_c2vmsg== NULL	||
		pCodec->ix== NULL			||
		pCodec->ex== NULL) {
			qra65_free(pCodec);
			return -4; // out of memory
		}

	// compute and store the AWGN/Rayleigh Es/No ratio for which we optimize
	// the decoder metric
	nm = _qra65_get_bits_per_symbol(pqracode);
	R  = _qra65_get_code_rate(pqracode);
	pCodec->decoderEsNoMetric   = 1.0f*nm*R*EbNoMetric;

	return 1;
}

void qra65_free(qra65_codec_ds *pCodec)
{
	if (!pCodec)
		return;

	// free internal buffers
	if (pCodec->x!=NULL)
		free(pCodec->x);

	if (pCodec->y!=NULL)
		free(pCodec->y);

	if (pCodec->qra_v2cmsg!=NULL)
		free(pCodec->qra_v2cmsg);

	if (pCodec->qra_c2vmsg!=NULL)
		free(pCodec->qra_c2vmsg);

	if (pCodec->ix!=NULL)
		free(pCodec->ix);

	if (pCodec->ex!=NULL)
		free(pCodec->ex);

	pCodec->pQraCode	= NULL;
	pCodec->x			= NULL;
	pCodec->y			= NULL;
	pCodec->qra_v2cmsg	= NULL;
	pCodec->qra_c2vmsg	= NULL;
	pCodec->qra_v2cmsg	= NULL;
	pCodec->ix			= NULL;
	pCodec->ex			= NULL;

	return;
}

int qra65_encode(const qra65_codec_ds *pCodec, int *pOutputCodeword, const int *pInputMsg)
{
	const qracode *pQraCode;
	int *px;
	int *py;
	int nK;
	int nN;

	if (!pCodec)
		return -1;	// which codec?

	pQraCode = pCodec->pQraCode;
	px = pCodec->x;
	py = pCodec->y;
	nK = _qra65_get_message_length(pQraCode);
	nN = _qra65_get_codeword_length(pQraCode);

	// copy the information symbols into the internal buffer
	memcpy(px,pInputMsg,nK*sizeof(int));

	// compute and append the appropriate CRC if required
	switch (pQraCode->type) {
		case QRATYPE_NORMAL:
			break;
		case QRATYPE_CRC:
		case QRATYPE_CRCPUNCTURED:
			px[nK] = _qra65_crc6(px,nK);
			break;
		case QRATYPE_CRCPUNCTURED2:
			_qra65_crc12(px+nK,px,nK);
			break;
		default:
			return -2;	// code type not supported
	}

	// encode with the given qra code
	qra_encode(pQraCode,py,px);

	// puncture the CRC symbols as required
	// and copy the result to the destination buffer
	switch (pQraCode->type) {
		case QRATYPE_NORMAL:
		case QRATYPE_CRC:
			// no puncturing
			memcpy(pOutputCodeword,py,nN*sizeof(int));
			break;
		case QRATYPE_CRCPUNCTURED:
			// strip the single CRC symbol from the encoded codeword
			memcpy(pOutputCodeword,py,nK*sizeof(int));				// copy the systematic symbols 
			memcpy(pOutputCodeword+nK,py+nK+1,(nN-nK)*sizeof(int));	// copy the check symbols skipping the CRC symbol
			break;
		case QRATYPE_CRCPUNCTURED2:
			// strip the 2 CRC symbols from the encoded codeword
			memcpy(pOutputCodeword,py,nK*sizeof(int));				// copy the systematic symbols
			memcpy(pOutputCodeword+nK,py+nK+2,(nN-nK)*sizeof(int)); // copy the check symbols skipping the two CRC symbols 
			break;
		default:
			return -2;	// code type unsupported
	}

	return 1; // ok
}

int qra65_intrinsics(qra65_codec_ds *pCodec, float *pIntrinsics, const float *pInputEnergies)
{
	// compute observations intrinsics probabilities
	// for the AWGN/Rayleigh channels

	// NOTE:
	// A true Rayleigh channel metric would require that the channel gains were known
	// for each symbol in the codeword. Such gains cannot be estimated reliably when
	// the Es/No ratio is small. Therefore we compute intrinsic probabilities assuming
	// that, on average, these channel gains are unitary.
	// In general it is even difficult to estimate the Es/No ratio for the AWGN channel
	// Therefore we always compute the intrinsic probabilities assuming that the Es/No
	// ratio is known and equal to the constant decoderEsNoMetric. This assumption will
	// generate the true intrinsic probabilities only when the actual Eb/No ratio is
	// equal to this constant. As in all the other cases the probabilities are evaluated
	// with a wrong scaling constant we can expect that the decoder performance at different
	// Es/No will be worse. Anyway, since the EsNoMetric constant has been chosen so that the 
	// decoder error rate is about 50%, we obtain almost optimal error rates down to
	// any useful Es/No ratio.

	const qracode *pQraCode;
	int	nN, nBits;
	float EsNoMetric;

	if (pCodec==NULL)
		return -1;		// which codec?

	pQraCode = pCodec->pQraCode;
	nN		 = _qra65_get_codeword_length(pQraCode);
	nBits	 = pQraCode->m;

	EsNoMetric = pCodec->decoderEsNoMetric;
	qra_mfskbesselmetric(pIntrinsics,pInputEnergies,nBits,nN,EsNoMetric);

	return 1;	// success
}

int qra65_esnodb(const qra65_codec_ds *pCodec, float *pEsNodB, const int *ydec, const float *pInputEnergies)
{
	// compute average Es/No for the AWGN/Rayleigh channel cases

	int k,j;
	float sigplusnoise=0;
	float noise=0;
	int nN, nM;
	const float *pIn = pInputEnergies;
	const int *py = ydec;
	float EsNodB;

	nN = qra65_get_codeword_length(pCodec);
	nM = qra65_get_alphabet_size(pCodec);

	for (k=0;k<nN;k++)  {

		for (j=0;j<nM;j++) 
			if (j==py[0])
				sigplusnoise += pIn[j];
			else
				noise +=pIn[j];

		pIn += nM;
		py++;
		}

	sigplusnoise = sigplusnoise/nN;			// average Es+No
	noise = noise/(nN*(nM-1));				// average No

	if (noise==0.0f)
		EsNodB =  50.0f;	// output an arbitrary +50 dB value avoiding division overflows
	else {
		float sig;
		if (sigplusnoise<noise)
			sigplusnoise = 1.316f*noise; // limit the minimum Es/No ratio to -5 dB;
		sig = sigplusnoise-noise;
		EsNodB = 10.0f*log10f(sig/noise);
	}

	*pEsNodB = EsNodB;

	return 1;
}

//
// Fast-fading channel metric ----------------------------------------------
//
// Tables of fading energies coefficients for Ts=6912/12000 (QRA64)
#include "fadengauss.c"
#include "fadenlorentz.c"
// As the fading is assumed to be symmetric around the nominal frequency
// only the leftmost and the central coefficient are stored in the tables.
// (files have been generated with the Matlab code efgengaussenergy.m and efgenlorentzenergy.m)

// Symbol time interval in seconds
#define TS_QRA64 0.576
#define TS_QRA65 0.640
// The tables are computed assuming that the bin spacing is that of QRA64, that's to say
// 1/Ts = 12000/6912 Hz, but in QRA65 Ts is longer (0.640 s) and the table index
// corresponding to a given B90 must be scaled appropriately.
// See below.

int qra65_intrinsics_fastfading(qra65_codec_ds *pCodec, 
								float *pIntrinsics,				// intrinsic symbol probabilities output
								const float *pInputEnergies,	// received energies input
								const int submode,				// submode idx (0=A ... 4=E)
								const float B90,				// spread bandwidth (90% fractional energy)
								const int fadingModel)			// 0=Gaussian 1=Lorentzian fade model
{
	int n, k, j;
	int nM, nN, nBinsPerTone, nBinsPerSymbol, nBinsPerCodeword;
	int hidx, hlen, hhsz, hlast;
	const float *hptr;
	float fTemp, fNoiseVar, sumix, maxlogp;
	float EsNoMetric;
	float *weight;
	const float *pCurSym, *pCurBin;
	float *pCurIx;

	if (pCodec==NULL)
		return QRA65_DECODE_INVPARAMS;	// invalid pCodec pointer

	if (submode<0 || submode>4)
		return QRA65_DECODE_INVPARAMS;	// invalid submode

	// As the symbol duration in QRA65 is longer than in QRA64 the fading tables continue
	// to be valid if the B90 parameter is scaled by the actual symbol rate
	// Compute index to most appropriate weighting function coefficients
    hidx = (int)(logf(B90*TS_QRA65/TS_QRA64)/logf(1.09f) - 0.499f);

//	if (hidx<0 || hidx > 64) 
//		// index of weighting function out of range
//		// B90 out of range
//		return QRA65_DECODE_INVPARAMS;	

	// Unlike in QRA64 we accept any B90, anyway limiting it to
	// the extreme cases (0.9 to 210 Hz approx.)
	if (hidx<0)
		hidx = 0;
	else
		if (hidx > 64) 
			hidx=64;

	// select the appropriate weighting fading coefficients array
	if (fadingModel==0) {	 // gaussian fading model
		// point to gaussian energy weighting taps
		hlen = glen_tab_gauss[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = gptr_tab_gauss[hidx];     // pointer to the first (L+1)/2 coefficients of w fun
		}
	else if (fadingModel==1) {
		// point to lorentzian energy weighting taps
		hlen = glen_tab_lorentz[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = gptr_tab_lorentz[hidx];   // pointer to the first (L+1)/2 coefficients of w fun
		}
	else 
		return QRA65_DECODE_INVPARAMS;	 // invalid fading model 

	// compute (euristically) the optimal decoder metric accordingly the given spread amount
	// We assume that the decoder 50% decoding threshold is:
	// Es/No(dB) = Es/No(AWGN)(dB) + 8*log(B90)/log(240)(dB)
	// that's to say, at the maximum Doppler spread bandwidth (240 Hz for QRA64) 
	// there's a ~8 dB Es/No degradation over the AWGN case
	fTemp = 8.0f*logf(B90)/logf(240.0f); // assumed Es/No degradation for the given fading bandwidth
	EsNoMetric = pCodec->decoderEsNoMetric*powf(10.0f,fTemp/10.0f);

	nM = qra65_get_alphabet_size(pCodec);
	nN = qra65_get_codeword_length(pCodec);
	nBinsPerTone   = 1<<submode;

	nBinsPerSymbol = nM*(2+nBinsPerTone);
	nBinsPerCodeword = nN*nBinsPerSymbol;

	// In the fast fading case , the intrinsic probabilities can be computed only
	// if both the noise spectral density and the average Es/No ratio are known.

	// Assuming that the energy of a tone is spread, on average, over adjacent bins
	// with the weights given in the precomputed fast-fading tables, it turns out
	// that the probability that the transmitted tone was tone j when we observed
	// the energies En(1)...En(N) is:

	// prob(tone j| en1....enN) proportional to exp(sum(En(k,j)*w(k)/No))
	// where w(k) = (g(k)*Es/No)/(1 + g(k)*Es/No),
	// g(k) are constant coefficients given on the fading tables,
	// and En(k,j) denotes the Energy at offset k from the central bin of tone j

	// Therefore we:
	// 1) compute No - the noise spectral density (or noise variance)
	// 2) compute the coefficients w(k) given the coefficient g(k) for the given decodeer Es/No metric
	// 3) compute the logarithm of prob(tone j| en1....enN) which is simply = sum(En(k,j)*w(k)/No
	// 4) subtract from the logarithm of the probabilities their maximum, 
	// 5) exponentiate the logarithms
	// 6) normalize the result to a probability distribution dividing each value 
	//    by the sum of all of them


	// Evaluate the average noise spectral density
	fNoiseVar = 0;
	for (k=0;k<nBinsPerCodeword;k++) 
		fNoiseVar += pInputEnergies[k];
	fNoiseVar = fNoiseVar/nBinsPerCodeword;
	// The noise spectral density so computed includes also the signal power.
	// Therefore we scale it accordingly to the Es/No assumed by the decoder
	fNoiseVar = fNoiseVar/(1.0f+EsNoMetric/nBinsPerSymbol); 
	// The value so computed is an overestimate of the true noise spectral density
	// by the (unknown) factor (1+Es/No(true)/nBinsPerSymbol)/(1+EsNoMetric/nBinsPerSymbol)
	// We will take this factor in account when computing the true Es/No ratio

	// store in the pCodec structure for later use in the estimation of the Es/No ratio
	pCodec->ffNoiseVar		= fNoiseVar;
	pCodec->ffEsNoMetric	= EsNoMetric;
	pCodec->nBinsPerTone    = nBinsPerTone;
	pCodec->nBinsPerSymbol  = nBinsPerSymbol;
	pCodec->nWeights        = hlen;
	weight					= pCodec->ffWeight;

	// compute the fast fading weights accordingly to the Es/No ratio
	// for which we compute the exact intrinsics probabilities
	for (k=0;k<hlen;k++) {	
		fTemp = hptr[k]*EsNoMetric; 
		weight[k] = fTemp/(1.0f+fTemp)/fNoiseVar;
		}

	// Compute now the instrinsics as indicated above
	pCurSym = pInputEnergies + nM;	// point to the central bin of the the first symbol tone
	pCurIx  = pIntrinsics;			// point to the first intrinsic

	hhsz  = hlen-1;		// number of symmetric taps
	hlast = 2*hhsz;		// index of the central tap

	for (n=0;n<nN;n++) {			// for each symbol in the message

		// compute the logarithm of the tone probability
		// as a weighted sum of the pertaining energies
		pCurBin = pCurSym -hlen+1;	// point to the first bin of the current symbol

		maxlogp = 0.0f;
		for (k=0;k<nM;k++) {		// for each tone in the current symbol
			// do a symmetric weighted sum
			fTemp = 0.0f;
			for (j=0;j<hhsz;j++) 
				fTemp += weight[j]*(pCurBin[j] + pCurBin[hlast-j]);	
			fTemp += weight[hhsz]*pCurBin[hhsz];

			if (fTemp>maxlogp)		// keep track of the max 
				maxlogp = fTemp;
			pCurIx[k]=fTemp;

			pCurBin += nBinsPerTone;	// next tone
			}

		// exponentiate and accumulate the normalization constant
		sumix = 0.0f;
		for (k=0;k<nM;k++) {   
			fTemp = expf(pCurIx[k]-maxlogp);
			pCurIx[k]=fTemp;
			sumix  +=fTemp;
			}

		// scale to a probability distribution
		sumix = 1.0f/sumix;
		for (k=0;k<nM;k++) 
			pCurIx[k] = pCurIx[k]*sumix;

		pCurSym +=nBinsPerSymbol;	// next symbol input energies
		pCurIx  +=nM;				// next symbol intrinsics
		}

	return 1;
}

int qra65_esnodb_fastfading(
			const qra65_codec_ds *pCodec,
			float		*pEsNodB,
			const int   *ydec,
			const float *pInputEnergies)
{
	// Estimate the Es/No ratio of the decoded codeword

	int n,j;
	int nN, nM, nBinsPerSymbol, nBinsPerTone, nWeights, nTotWeights;
	const float *pCurSym, *pCurTone, *pCurBin;
	float EsPlusWNo,u, minu, ffNoiseVar, ffEsNoMetric;

	if (pCodec==NULL)
		return QRA65_DECODE_INVPARAMS;

	nN = qra65_get_codeword_length(pCodec);
	nM = qra65_get_alphabet_size(pCodec);

	nBinsPerTone   = pCodec->nBinsPerTone;
	nBinsPerSymbol = pCodec->nBinsPerSymbol;
	nWeights       = pCodec->nWeights;
	ffNoiseVar	   = pCodec->ffNoiseVar;
	ffEsNoMetric   = pCodec->ffEsNoMetric;
	nTotWeights    = 2*nWeights-1;

	// compute symbols energy (noise included) summing the 
	// energies pertaining to the decoded symbols in the codeword

	EsPlusWNo = 0.0f;
	pCurSym = pInputEnergies + nM;	// point to first central bin of first symbol tone
	for (n=0;n<nN;n++) {					
		pCurTone = pCurSym + ydec[n]*nBinsPerTone;	 // point to the central bin of the current decoded symbol
		pCurBin  = pCurTone - nWeights+1;			 // point to first bin 
		
		// sum over all the pertaining bins
		for (j=0;j<nTotWeights;j++)
			EsPlusWNo += pCurBin[j];		

		pCurSym +=nBinsPerSymbol;

		}
	EsPlusWNo =  EsPlusWNo/nN;	// Es + nTotWeigths*No


	// The noise power ffNoiseVar computed in the qra65_intrisics_fastading(...) function
	// is not the true noise power as it includes part of the signal energy.
	// The true noise variance is:
	// No = ffNoiseVar*(1+EsNoMetric/nBinsPerSymbol)/(1+EsNo/nBinsPerSymbol)

	// Therefore:
	// Es/No = EsPlusWNo/No - W = EsPlusWNo/ffNoiseVar*(1+Es/No/nBinsPerSymbol)/(1+Es/NoMetric/nBinsPerSymbol) - W
	// and:
	// Es/No*(1-u/nBinsPerSymbol) = u-W or Es/No = (u-W)/(1-u/nBinsPerSymbol)
	// where:
	// u = EsPlusNo/ffNoiseVar/(1+EsNoMetric/nBinsPerSymbol)

	u = EsPlusWNo/(ffNoiseVar*(1+ffEsNoMetric/nBinsPerSymbol));

	minu = nTotWeights+0.316f;
	if (u<minu)
		u = minu;		// Limit the minimum Es/No to -5 dB approx.

	u = (u-nTotWeights)/(1.0f -u/nBinsPerSymbol);  // linear scale Es/No
	*pEsNodB = 10.0f*log10f(u);

	return 1;
}


int qra65_decode(qra65_codec_ds *pCodec, int* pDecodedCodeword, int *pDecodedMsg, const float *pIntrinsics, const int *pAPMask, const int *pAPSymbols)
{
	const qracode *pQraCode;
	float	*ix, *ex;
	int		*px;
	int		*py;
	int		nK, nN, nM,nBits;
	int		rc;
	int		crc6;
	int		crc12[2];

	if (!pCodec)
		return QRA65_DECODE_INVPARAMS;	// which codec?

	pQraCode	= pCodec->pQraCode;
	ix			= pCodec->ix;
	ex			= pCodec->ex;

	nK			= _qra65_get_message_length(pQraCode);
	nN			= _qra65_get_codeword_length(pQraCode);
	nM			= pQraCode->M;
	nBits		= pQraCode->m;

	px			= pCodec->x;
	py			= pCodec->y;

	// Depuncture intrinsics observations as required by the code type
	switch (pQraCode->type) {
		case QRATYPE_CRCPUNCTURED:
			memcpy(ix,pIntrinsics,nK*nM*sizeof(float));							// information symbols
			pd_init(PD_ROWADDR(ix,nM,nK),pd_uniform(nBits),nM);					// crc
			memcpy(ix+(nK+1)*nM,pIntrinsics+nK*nM,(nN-nK)*nM*sizeof(float));	// parity checks
			break;
		case QRATYPE_CRCPUNCTURED2:
			memcpy(ix,pIntrinsics,nK*nM*sizeof(float));							// information symbols
			pd_init(PD_ROWADDR(ix,nM,nK),pd_uniform(nBits),nM);					// crc
			pd_init(PD_ROWADDR(ix,nM,nK+1),pd_uniform(nBits),nM);				// crc
			memcpy(ix+(nK+2)*nM,pIntrinsics+nK*nM,(nN-nK)*nM*sizeof(float));	// parity checks
			break;
		case QRATYPE_NORMAL:
		case QRATYPE_CRC:
		default:
			// no puncturing
			memcpy(ix,pIntrinsics,nN*nM*sizeof(float));							// as they are
		}

	// mask the intrinsics with the available a priori knowledge
	if (pAPMask!=NULL)
		_qra65_mask(pQraCode,ix,pAPMask,pAPSymbols);


	// Compute the extrinsic symbols probabilities with the message-passing algorithm
	// Stop if the extrinsics information does not converges to unity
	// within the given number of iterations
	rc = qra_extrinsic( pQraCode,
						ex,
						ix,
						100,
						pCodec->qra_v2cmsg,
						pCodec->qra_c2vmsg);

	if (rc<0) 
		// failed to converge to a solution
		return QRA65_DECODE_FAILED;

	// decode the information symbols (punctured information symbols included)
	qra_mapdecode(pQraCode,px,ex,ix);

	// verify CRC match 

	switch (pQraCode->type) {
		case QRATYPE_CRC:
		case QRATYPE_CRCPUNCTURED:
			crc6=_qra65_crc6(px,nK);			 // compute crc-6
			if (crc6!=px[nK]) 				
				return QRA65_DECODE_CRCMISMATCH; // crc doesn't match
			break;
		case QRATYPE_CRCPUNCTURED2:
			_qra65_crc12(crc12, px,nK);			 // compute crc-12
			if (crc12[0]!=px[nK] || 
				crc12[1]!=px[nK+1]) 
				return QRA65_DECODE_CRCMISMATCH; // crc doesn't match
			break;
		case QRATYPE_NORMAL:
		default:
			// nothing to check
			break;
		}

	// copy the decoded msg to the user buffer (excluding punctured symbols)
	if (pDecodedMsg)
		memcpy(pDecodedMsg,px,nK*sizeof(int));

	if (pDecodedCodeword==NULL)		// user is not interested in it
		return rc;					// return the number of iterations required to decode

	// crc matches therefore we can reconstruct the transmitted codeword
	//  reencoding the information available in px...

	qra_encode(pQraCode, py, px);

	// ...and strip the punctured symbols from the codeword
	switch (pQraCode->type) {
		case QRATYPE_CRCPUNCTURED:
				memcpy(pDecodedCodeword,py,nK*sizeof(int));
				memcpy(pDecodedCodeword+nK,py+nK+1,(nN-nK)*sizeof(int));	// puncture crc-6 symbol
			break;
		case QRATYPE_CRCPUNCTURED2:
				memcpy(pDecodedCodeword,py,nK*sizeof(int));
				memcpy(pDecodedCodeword+nK,py+nK+2,(nN-nK)*sizeof(int));	// puncture crc-12 symbols
			break;
		case QRATYPE_CRC:
		case QRATYPE_NORMAL:
		default:
			memcpy(pDecodedCodeword,py,nN*sizeof(int));		// no puncturing
		}

	return rc;	// return the number of iterations required to decode

}




// helper functions -------------------------------------------------------------

int _qra65_get_message_length(const qracode *pCode)
{
	// return the actual information message length (in symbols)
	// excluding any punctured symbol

	int nMsgLength;

	switch (pCode->type) {
		case QRATYPE_NORMAL:
			nMsgLength = pCode->K;
			break;
		case QRATYPE_CRC:
		case QRATYPE_CRCPUNCTURED:
			// one information symbol of the underlying qra code is reserved for CRC
			nMsgLength = pCode->K-1;
			break;
		case QRATYPE_CRCPUNCTURED2:
			// two code information symbols are reserved for CRC
			nMsgLength = pCode->K-2;
			break;
		default:
			nMsgLength = -1;
	}

	return nMsgLength;
}

int _qra65_get_codeword_length(const qracode *pCode)
{
	// return the actual codeword length (in symbols)
	// excluding any punctured symbol
	
	int nCwLength;

	switch (pCode->type) {
		case QRATYPE_NORMAL:
		case QRATYPE_CRC:
			// no puncturing
			nCwLength = pCode->N;
			break;
		case QRATYPE_CRCPUNCTURED:
			// the CRC symbol is punctured
			nCwLength = pCode->N-1;
			break;
		case QRATYPE_CRCPUNCTURED2:
			// the two CRC symbols are punctured
			nCwLength = pCode->N-2;
			break;
		default:
			nCwLength = -1;
	}

	return nCwLength;
}

float _qra65_get_code_rate(const qracode *pCode)
{
	return 1.0f*_qra65_get_message_length(pCode)/_qra65_get_codeword_length(pCode);
}

int _qra65_get_alphabet_size(const qracode *pCode)
{
	return pCode->M;
}
int _qra65_get_bits_per_symbol(const qracode *pCode)
{
	return pCode->m;
}
static void _qra65_mask(const qracode *pcode, float *ix, const int *mask, const int *x)
{
	// mask intrinsic information ix with available a priori knowledge
	
	int k,kk, smask;
	const int nM=pcode->M;	
	const int nm=pcode->m;
	int nK;

	// Exclude from masking the symbols which have been punctured.
	// nK is the length of the mask and x arrays, which do 
	// not include any punctured symbol 
	nK = _qra65_get_message_length(pcode);

	// for each symbol set to zero the probability
	// of the values which are not allowed by
	// the a priori information

	for (k=0;k<nK;k++) {
		smask = mask[k];
		if (smask) {
			for (kk=0;kk<nM;kk++) 
				if (((kk^x[k])&smask)!=0)
					// This symbol value is not allowed
					// by the AP information
					// Set its probability to zero
					*(PD_ROWADDR(ix,nM,k)+kk) = 0.f;

			// normalize to a probability distribution
			pd_norm(PD_ROWADDR(ix,nM,k),nm);
			}
		}
}

// CRC generation functions

// crc-6 generator polynomial
// g(x) = x^6 + x + 1  
#define CRC6_GEN_POL 0x30		// MSB=a0 LSB=a5    

// crc-12 generator polynomial
// g(x) = x^12 + x^11 + x^3 + x^2 + x + 1  
#define CRC12_GEN_POL 0xF01		// MSB=a0 LSB=a11

// g(x) = x^6 + x^2 + x + 1 (as suggested by Joe. See i.e.:  https://users.ece.cmu.edu/~koopman/crc/)
// #define CRC6_GEN_POL 0x38  // MSB=a0 LSB=a5. Simulation results are similar


static int _qra65_crc6(int *x, int sz)
{
	int k,j,t,sr = 0;
	for (k=0;k<sz;k++) {
		t = x[k];
		for (j=0;j<6;j++) {
			if ((t^sr)&0x01)
				sr = (sr>>1) ^ CRC6_GEN_POL;
			else
				sr = (sr>>1);
			t>>=1;
			}
		}

	return sr;
}

static void _qra65_crc12(int *y, int *x, int sz)
{
	int k,j,t,sr = 0;
	for (k=0;k<sz;k++) {
		t = x[k];
		for (j=0;j<6;j++) {
			if ((t^sr)&0x01)
				sr = (sr>>1) ^ CRC12_GEN_POL;
			else
				sr = (sr>>1);
			t>>=1;
			}
		}

	y[0] = sr&0x3F; 
	y[1] = (sr>>6);
}


