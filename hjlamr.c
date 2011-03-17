/*
 * AMR-NB encoder decoder library for linephone Copyright (C) 2011 Hu
 * Jianling Soochow University, All rights reserved. jlhu@suda.edu.cn
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the aggrement of Hu Jianling; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 */

#include <mediastreamer2/msfilter.h>

#include "amr_if_dec.h"
#include "amr_if_enc.h"

/*
 * Class A total speech Index Mode bits bits
 * ----------------------------------------
 *  0 AMR 4.75 42 95
 *  1 AMR 5.15 49 103
 *  2 AMR 5.9 55 118
 *  3 AMR 6.7 58 134
 *  4 AMR 7.4 61 148
 *  5 AMR 7.95 75 159
 *  6 AMR 10.2 65 204
 *  7 AMR 12.2 81 244
 *  8 AMR SID 39 39 
 */

static const int amr_frame_sizes[] = {
    12,
    13,
    15,
    17,
    19,
    20,
    26,
    31,
    5,
    0, 0, 0, 0, 0, 0, 0
};


#define toc_get_f(toc) ((toc) >> 7)
#define toc_get_index(toc)	((toc>>3) & 0xf)
#define VERSION "0.0.1"

typedef struct EncState {
    void           *enc;
    MSBufferizer   *mb;
    uint32_t        ts;
    bool_t          dtx;
    int             mode;
} EncState;

static int
toc_list_check(uint8_t * tl, size_t buflen)
{
    int             s = 1;
    while (toc_get_f(*tl)) {
	tl++;
	s++;
	if (s > buflen) {
	    return -1;
	}
    }
    return s;
}

static int
set_bitrate(MSFilter * f, void *arg)
{
    EncState       *s = (EncState *) f->data;

    s->mode = *(int *) arg;

    Encoder_Interface_ctrl(s->enc, s->dtx, s->mode);

    return 0;
}

static int
enable_vad(MSFilter * f, void *arg)
{
    EncState       *s = (EncState *) f->data;

    s->dtx = *(bool_t *) arg;

    Encoder_Interface_ctrl(s->enc, s->dtx, s->mode);

    return 0;
}

static void
dec_init(MSFilter * f)
{

    f->data = Decoder_Interface_init();
    ms_warning("libmyamr: dec inited.");
}

static void
dec_process(MSFilter * f)
{
    static const int nsamples = 160;
    mblk_t         *im,
                   *om;
    uint8_t        *tocs;
    int             toclen;
    uint8_t         tmp[32];

    while ((im = ms_queue_get(f->inputs[0])) != NULL) {
	int             sz = msgdsize(im);
	int             i;
	if (sz < 2) {
	    freemsg(im);
	    continue;
	}
	/*
	 * skip payload header, ignore CMR 
	 */
	im->b_rptr++;
	/*
	 * see the number of TOCs :
	 */
	tocs = im->b_rptr;
	toclen = toc_list_check(tocs, sz);
	if (toclen == -1) {
	    ms_warning("Bad AMR toc list");
	    freemsg(im);
	    continue;
	}
	im->b_rptr += toclen;
	/*
	 * iterate through frames, following the toc list
	 */
	for (i = 0; i < toclen; ++i) {
	    int             framesz =
		amr_frame_sizes[toc_get_index(tocs[i])];
	    if (im->b_rptr + framesz > im->b_wptr) {
		ms_warning("Truncated amr frame");
		break;
	    }
	    tmp[0] = tocs[i];
	    memcpy(&tmp[1], im->b_rptr, framesz);
	    om = allocb(nsamples * 2, 0);
	    Decoder_Interface_Decode(f->data, tmp, (short *) om->b_wptr,
				     0);
	    om->b_wptr += nsamples * 2;
	    im->b_rptr += framesz;
	    ms_queue_put(f->outputs[0], om);
	}
	freemsg(im);
    }
}

static void
dec_uninit(MSFilter * f)
{
    ms_warning("libmyamr: dec_uninit...");
    Decoder_Interface_exit(f->data);
}

MSFilterDesc amr_dec_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "HJLAmrDec",
    .text = "AMR-NB decoder based on DSP codec.",
    .category = MS_FILTER_DECODER,
    .enc_fmt = "AMR",
    .ninputs = 1,
    .noutputs = 1,
    .init = dec_init,
    .process = dec_process,
    .uninit = dec_uninit
};

static void
enc_init(MSFilter * f)
{
    EncState       *s = ms_new0(EncState, 1);
    s->dtx = FALSE;
    s->mb = ms_bufferizer_new();
    s->ts = 0;
    s->mode = 7;
    f->data = s;
    ms_warning("libmyamr: enc inited.");
}

static void
enc_uninit(MSFilter * f)
{
    EncState       *s = (EncState *) f->data;
    ms_warning("libmyamr: enc_uninit...");
    ms_bufferizer_destroy(s->mb);
    ms_free(s);
}

static void
enc_preprocess(MSFilter * f)
{
    ms_warning("libmyamr: enc_preprocessing...");
    EncState       *s = (EncState *) f->data;

    s->enc = Encoder_Interface_init(s->dtx, s->mode);
}

static void
enc_process(MSFilter * f)
{
    static const int nsamples = 160;
    ms_warning("libmyamr: enc_process start...");
    EncState       *s = (EncState *) f->data;
    mblk_t         *im,
                   *om;
    int16_t         samples[nsamples];

    while ((im = ms_queue_get(f->inputs[0])) != NULL) {
	ms_bufferizer_put(s->mb, im);
    }
    while ((ms_bufferizer_read(s->mb, (uint8_t *) samples, nsamples * 2))
	   >= nsamples * 2) {
	int             ret;
	om = allocb(33, 0);
	*om->b_wptr = 0xf0;
	om->b_wptr++;
	ret = Encoder_Interface_Encode(s->enc, samples, om->b_wptr);
	if (ret <= 0) {
	    ms_warning("Encoder returned %i", ret);
	    freemsg(om);
	    continue;
	}
	om->b_wptr += ret;
	mblk_set_timestamp_info(om, s->ts);
	s->ts += nsamples;
	ms_queue_put(f->outputs[0], om);
    }
}

static void
enc_postprocess(MSFilter * f)
{
    EncState       *s = (EncState *) f->data;
    ms_warning("libmyamr: enc_postprocess...");
    Encoder_Interface_exit(s->enc);
    s->enc = NULL;
    ms_bufferizer_flush(s->mb);
}

static MSFilterMethod hjlamr_methods[] = {
    {MS_FILTER_SET_BITRATE, set_bitrate},
    {MS_FILTER_ENABLE_VAD, enable_vad},
    {0, NULL}
};

MSFilterDesc amr_enc_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "HJLAmrEnc",
    .text = "AMR-NB encoder based DSP codec",
    .category = MS_FILTER_ENCODER,
    .enc_fmt = "AMR",
    .ninputs = 1,
    .noutputs = 1,
    .init = enc_init,
    .preprocess = enc_preprocess,
    .process = enc_process,
    .postprocess = enc_postprocess,
    .uninit = enc_uninit,
    .methods = hjlamr_methods
};
