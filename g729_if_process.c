/*
 * g729_if_process.c
 *
 * ============================================================================
 * Copyright (c) Texas Instruments Inc 2009
 *
 * Use of this software is controlled by the terms and conditions found in the
 * license agreement under which this software has been supplied or provided.
 * ============================================================================
 */

#define G729AB_DSPCODEC_INTERNAL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xdc/std.h>

#include <ti/sdo/ce/Engine.h>

#include <ti/sdo/dmai/Pause.h>
#include <ti/sdo/dmai/Sound.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/Loader.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/dmai/ce/Sdec1.h>


#include "g729_if_dec.h"
#include "g729_if_enc.h"
#include "Senc1.h"


static short    g729_suda_Flen[16] = {
    11, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};

struct g729_decoder_state {
    Engine_Handle   hEngine;
    Sdec1_Handle    hSd1;
    SPHDEC1_Params  decParams;
    SPHDEC1_DynamicParams decDynParams;
    Buffer_Handle   hInBuf;
    Buffer_Handle   hOutBuf;
};

/*
 * deocder interface initialization 
 */
void           *
G729_Decoder_Interface_init(void)
{
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    struct g729_decoder_state *state =
	(struct g729_decoder_state *) malloc(sizeof(struct g729_decoder_state));
    CERuntime_init();
    Dmai_init();
    if (state == NULL) {
	fprintf(stderr,
		"Error to malloc memory for G729AB decoder state variable\n");
	return (void *) state;
    }
    fprintf(stderr, "Engine opening in G729AB decoder init......\n");

    if ((state->hEngine = Engine_open("encodedecode", NULL, NULL)) == NULL) {
	fprintf(stderr, "Engine open error in G729AB decoder init\n");
	free(state);
	state = NULL;
	return (void *) state;
    }
    fprintf(stderr, "Engine opened in G729AB decoder init......\n");

    state->decParams = Sdec1_Params_DEFAULT;
    state->decDynParams = Sdec1_DynamicParams_DEFAULT;

    if ((state->hSd1 =
	 Sdec1_create(state->hEngine, "g729dec", &(state->decParams),
		      &(state->decDynParams)))
	== NULL) {
	fprintf(stderr, "Create G729AB decoder handle error\n");
	Engine_close(state->hEngine);
	free(state);
	state = NULL;
	return (void *) state;
    }

    state->hInBuf =
	Buffer_create(Sdec1_getInBufSize(state->hSd1), &bAttrs);
    state->hOutBuf =
	Buffer_create(Sdec1_getOutBufSize(state->hSd1) * 2, &bAttrs);

    if ((state->hInBuf == NULL) || (state->hOutBuf == NULL)) {
	fprintf(stderr,
		"Failed to allocate buffer for G729AB decoder input and output\n");
	if (state->hInBuf) {
	    Buffer_delete(state->hInBuf);
	}
	if (state->hOutBuf) {
	    Buffer_delete(state->hOutBuf);
	}
	if (state->hSd1) {
	    Sdec1_delete(state->hSd1);
	}
	if (state->hEngine) {
	    Engine_close(state->hEngine);
	}
	free(state);
	state = NULL;
	return (void *) state;
    }

    return (void *) state;
}

void
G729_Decoder_Interface_exit(void *s)
{
    struct g729_decoder_state *state = (struct g729_decoder_state *) s;
    if (state->hInBuf) {
	Buffer_delete(state->hInBuf);
    }
    if (state->hOutBuf) {
	Buffer_delete(state->hOutBuf);
    }
    if (state->hSd1) {
	Sdec1_delete(state->hSd1);
    }
    if (state->hEngine) {
	Engine_close(state->hEngine);
    }
    free(state);
}

void
G729_Decoder_Interface_Decode(void *s, const unsigned char *in, short *out,
			 int bfi)
{
    struct g729_decoder_state *state = (struct g729_decoder_state *) s;

    int             mode = (int) ((in[0] >> 3) & 0x0f);
    int             len = g729_suda_Flen[mode];
    unsigned char  *pIn =
	(unsigned char *) Buffer_getUserPtr(state->hInBuf);
    unsigned char  *pOut =
	(unsigned char *) Buffer_getUserPtr(state->hOutBuf);

    memcpy(pIn, in, len);
    Buffer_setNumBytesUsed(state->hInBuf, len);

    if (Sdec1_process(state->hSd1, state->hInBuf, state->hOutBuf) < 0) {
	fprintf(stderr, "G729AB Failed to decode speech buffer\n");
    }

    memcpy((unsigned char *) out, pOut, 80 * 2);
}

struct g729_encoder_state {
    Engine_Handle   hEngine;
    Senc1_Handle    hSe1;
    SPHENC1_Params  encParams;
    SPHENC1_DynamicParams encDynParams;
    Buffer_Handle   hInBuf;
    Buffer_Handle   hOutBuf;
};

/*
 * deocder interface initialization 
 */
void           *
G729_Encoder_Interface_init(int dtx)
{
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    struct g729_encoder_state *state =
	(struct g729_encoder_state *) malloc(sizeof(struct g729_encoder_state));

    CERuntime_init();
    Dmai_init();
    fprintf(stderr, "Encoder_Interface_init init..................\n");
    if (state == NULL) {
	fprintf(stderr,
		"Error to malloc memory for G729AB encoder state variable\n");
	return (void *) state;
    }

    if ((state->hEngine = Engine_open("encodedecode", NULL, NULL)) == NULL) {
	fprintf(stderr, "Engine open error in G729AB encoder init\n");
	free(state);
	state = NULL;
	return (void *) state;
    }
    fprintf(stderr,
	    "Engine opened in G729AB encoder init..................\n");

    state->encParams = Senc1_Params_DEFAULT;
    state->encDynParams = Senc1_DynamicParams_DEFAULT;
    state->encParams.vadSelection = dtx;
    state->encDynParams.vadFlag = dtx;

    if ((state->hSe1 =
	 Senc1_create(state->hEngine, "g729enc", &(state->encParams),
		      &(state->encDynParams)))
	== NULL) {
	fprintf(stderr, "Create G729AB encoder handle error\n");
	Engine_close(state->hEngine);
	free(state);
	state = NULL;
	return (void *) state;
    }

    state->hInBuf =
	Buffer_create(Senc1_getInBufSize(state->hSe1) * 2, &bAttrs);
    state->hOutBuf =
	Buffer_create(Senc1_getOutBufSize(state->hSe1), &bAttrs);

    if ((state->hInBuf == NULL) || (state->hOutBuf == NULL)) {
	fprintf(stderr,
		"Failed to allocate buffer for G729AB encoder input and output\n");
	if (state->hInBuf) {
	    Buffer_delete(state->hInBuf);
	}
	if (state->hOutBuf) {
	    Buffer_delete(state->hOutBuf);
	}
	if (state->hSe1) {
	    Senc1_delete(state->hSe1);
	}
	if (state->hEngine) {
	    Engine_close(state->hEngine);
	}
	free(state);
	state = NULL;
	return (void *) state;
    }

    return (void *) state;
}

void
G729_Encoder_Interface_exit(void *s)
{
    struct g729_encoder_state *state = (struct g729_encoder_state *) s;
    if (state->hInBuf) {
	Buffer_delete(state->hInBuf);
    }
    if (state->hOutBuf) {
	Buffer_delete(state->hOutBuf);
    }
    if (state->hSe1) {
	Senc1_delete(state->hSe1);
    }
    if (state->hEngine) {
	Engine_close(state->hEngine);
    }
    free(state);
}

void
G729_Encoder_Interface_ctrl(void *s, int dtx)
{
    struct g729_encoder_state *state = (struct g729_encoder_state *) s;
    SPHENC1_Status  encStatus;
    XDAS_Int32      status;
    SPHENC1_Handle  hEncode;

    if (state == NULL)
	return;

    state->encDynParams.vadFlag = dtx;
    encStatus.size = sizeof(SPHENC1_Status);
    encStatus.data.buf = NULL;
    hEncode = Senc1_getVisaHandle(state->hSe1);
    status =
	SPHENC1_control(hEncode, XDM_SETPARAMS, &(state->encDynParams),
			&encStatus);
    if (status != SPHENC1_EOK) {
	fprintf(stderr, "G729AB encoder error to set parameters\n");
    }
}

int
G729_Encoder_Interface_Encode(void *s, const short *speech, unsigned char *out)
{
    struct g729_encoder_state *state = (struct g729_encoder_state *) s;

    unsigned char  *pIn =
	(unsigned char *) Buffer_getUserPtr(state->hInBuf);
    unsigned char  *pOut =
	(unsigned char *) Buffer_getUserPtr(state->hOutBuf);

    int             len;

    memcpy(pIn, (unsigned char *) speech, 80 * 2);
    Buffer_setNumBytesUsed(state->hInBuf, 80 * 2);

    if (Senc1_process(state->hSe1, state->hInBuf, state->hOutBuf) < 0) {
	fprintf(stderr, "G729AB failed to encode one frame of speech\n");
    }

    len = Buffer_getNumBytesUsed(state->hOutBuf);
    memcpy(out, pOut, len);

    return len;
}
