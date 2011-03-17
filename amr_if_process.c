/*
 * speech.c
 *
 * ============================================================================
 * Copyright (c) Texas Instruments Inc 2009
 *
 * Use of this software is controlled by the terms and conditions found in the
 * license agreement under which this software has been supplied or provided.
 * ============================================================================
 */

#define AMRNB_DSPCODEC_INTERNAL
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


#include "amr_if_dec.h"
#include "amr_if_enc.h"
#include "Senc1.h"


static short    amrnb_suda_AMRNB_NOCRC_Flen[16] = {
    13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 1
};

struct decoder_state {
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
Decoder_Interface_init(void)
{
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    struct decoder_state *state =
	(struct decoder_state *) malloc(sizeof(struct decoder_state));
    CERuntime_init();
    Dmai_init();
    if (state == NULL) {
	fprintf(stderr,
		"Error to malloc memory for AMR-NB decoder state variable\n");
	return (void *) state;
    }
    fprintf(stderr, "Engine opening in AMR decoder init......\n");

    if ((state->hEngine = Engine_open("encodedecode", NULL, NULL)) == NULL) {
	fprintf(stderr, "Engine open error in AMR decoder init\n");
	free(state);
	state = NULL;
	return (void *) state;
    }
    fprintf(stderr, "Engine opened in AMR decoder init......\n");

    state->decParams = Sdec1_Params_DEFAULT;
    state->decDynParams = Sdec1_DynamicParams_DEFAULT;
    state->decParams.packingType = 0;
    state->decParams.bitRate = 7;

    if ((state->hSd1 =
	 Sdec1_create(state->hEngine, "amrnbdec", &(state->decParams),
		      &(state->decDynParams)))
	== NULL) {
	fprintf(stderr, "Create AMR-NB decoder handle error\n");
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
		"Failed to allocate buffer for AMR-NB decoder input and output\n");
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
Decoder_Interface_exit(void *s)
{
    struct decoder_state *state = (struct decoder_state *) s;
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
Decoder_Interface_Decode(void *s, const unsigned char *in, short *out,
			 int bfi)
{
    struct decoder_state *state = (struct decoder_state *) s;

    int             mode = (int) ((in[0] >> 3) & 0x0f);
    int             len = amrnb_suda_AMRNB_NOCRC_Flen[mode];
    unsigned char  *pIn =
	(unsigned char *) Buffer_getUserPtr(state->hInBuf);
    unsigned char  *pOut =
	(unsigned char *) Buffer_getUserPtr(state->hOutBuf);

    memcpy(pIn, in, len);
    Buffer_setNumBytesUsed(state->hInBuf, len);

    if (Sdec1_process(state->hSd1, state->hInBuf, state->hOutBuf) < 0) {
	fprintf(stderr, "AMR-NB Failed to decode speech buffer\n");
    }

    memcpy((unsigned char *) out, pOut, 160 * 2);
}

struct encoder_state {
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
Encoder_Interface_init(int dtx, int mode)
{
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    struct encoder_state *state =
	(struct encoder_state *) malloc(sizeof(struct encoder_state));

    CERuntime_init();
    Dmai_init();
    fprintf(stderr, "Encoder_Interface_init init..................\n");
    if (state == NULL) {
	fprintf(stderr,
		"Error to malloc memory for AMR-NB encoder state variable\n");
	return (void *) state;
    }

    if ((state->hEngine = Engine_open("encodedecode", NULL, NULL)) == NULL) {
	fprintf(stderr, "Engine open error in AMR encoder init\n");
	free(state);
	state = NULL;
	return (void *) state;
    }
    fprintf(stderr,
	    "Engine opened in AMR encoder init..................\n");

    state->encParams = Senc1_Params_DEFAULT;
    state->encDynParams = Senc1_DynamicParams_DEFAULT;
    state->encParams.vadSelection = dtx;
    state->encParams.packingType = 0;
    state->encParams.bitRate = mode;
    state->encDynParams.vadFlag = dtx;
    state->encDynParams.bitRate = mode;

    if ((state->hSe1 =
	 Senc1_create(state->hEngine, "amrnbenc", &(state->encParams),
		      &(state->encDynParams)))
	== NULL) {
	fprintf(stderr, "Create AMR-NB encoder handle error\n");
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
		"Failed to allocate buffer for AMR-NB encoder input and output\n");
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
Encoder_Interface_exit(void *s)
{
    struct encoder_state *state = (struct encoder_state *) s;
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
Encoder_Interface_ctrl(void *s, int dtx, int mode)
{
    struct encoder_state *state = (struct encoder_state *) s;
    SPHENC1_Status  encStatus;
    XDAS_Int32      status;
    SPHENC1_Handle  hEncode;

    if (state == NULL)
	return;

    state->encDynParams.vadFlag = dtx;
    state->encDynParams.bitRate = mode;
    encStatus.size = sizeof(SPHENC1_Status);
    encStatus.data.buf = NULL;
    hEncode = Senc1_getVisaHandle(state->hSe1);
    status =
	SPHENC1_control(hEncode, XDM_SETPARAMS, &(state->encDynParams),
			&encStatus);
    if (status != SPHENC1_EOK) {
	fprintf(stderr, "AMR-NB encoder error to set parameters\n");
    }
}

int
Encoder_Interface_Encode(void *s, const short *speech, unsigned char *out)
{
    struct encoder_state *state = (struct encoder_state *) s;

    unsigned char  *pIn =
	(unsigned char *) Buffer_getUserPtr(state->hInBuf);
    unsigned char  *pOut =
	(unsigned char *) Buffer_getUserPtr(state->hOutBuf);

    int             len;

    memcpy(pIn, (unsigned char *) speech, 160 * 2);
    Buffer_setNumBytesUsed(state->hInBuf, 160 * 2);

    if (Senc1_process(state->hSe1, state->hInBuf, state->hOutBuf) < 0) {
	fprintf(stderr, "AMR-NB failed to encode one frame of speech\n");
    }

    len = Buffer_getNumBytesUsed(state->hOutBuf);
    memcpy(out, pOut, len);

    return len;
}
