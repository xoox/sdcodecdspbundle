/*
 * mediastreamer2 H264 plugin Copyright (C) 2011 Soochow
 * University(caiwenfeng@suda.edu.cn)
 * 
 */
#include <stdio.h>

#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/msticker.h"
#include "mediastreamer2/msvideo.h"
#include "mediastreamer2/rfc3984.h"

#include "ortp/b64.h"

#include <xdc/std.h>

#include <ti/sdo/ce/CERuntime.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/osal/Memory.h>

#include <ti/sdo/dmai/VideoStd.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/ce/Venc1.h>
#include <ti/sdo/dmai/ce/Vdec2.h>

#define VERSION                 "0.1"
#define ENGINE_NAME             "encodedecode"
#define DISPLAY_PIPE_SIZE       5

typedef struct _EncData {
    Engine_Handle   hEngine;
    Venc1_Handle    hVe1;
    Buffer_Handle   hVidBuf;
    Buffer_Handle   hEncBuf;
    MSVideoSize     vsize;
    int             bitrate;
    float           fps;
    int             mode;
    uint64_t        framenum;
    Rfc3984Context  packer;
    int             keyframe_int;
    bool_t          generate_keyframe;
} EncData;


static void
enc_init(MSFilter * f)
{
    EncData        *d = ms_new(EncData, 1);
    d->hEngine = NULL;
    d->hVe1 = NULL;
    d->hVidBuf = NULL;
    d->hEncBuf = NULL;
    d->bitrate = 384000;
    d->vsize = MS_VIDEO_SIZE_CIF;
    d->fps = 30;
    d->keyframe_int = 10;	/* 10 seconds */
    d->mode = 0;
    d->framenum = 0;
    d->generate_keyframe = FALSE;
    f->data = d;
}

static void
enc_uninit(MSFilter * f)
{
    EncData        *d = (EncData *) f->data;

    ms_free(d);
}

static void
enc_preprocess(MSFilter * f)
{
    EncData        *d = (EncData *) f->data;
    VIDENC1_Params  defaultEncParams = Venc1_Params_DEFAULT;
    VIDENC1_DynamicParams defaultEncDynParams =
	Venc1_DynamicParams_DEFAULT;
    VIDENC1_Params *encParams;
    VIDENC1_DynamicParams *encDynParams;
    BufferGfx_Attrs gfxAttrs = BufferGfx_Attrs_DEFAULT;
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    Int32           bufSize;

    bool_t          cleanUpQ = FALSE;

    rfc3984_init(&d->packer);
    rfc3984_set_mode(&d->packer, d->mode);
    rfc3984_enable_stap_a(&d->packer, FALSE);

    /*
     * Initialize Codec Engine runtime 
     */
    CERuntime_init();

    /*
     * Initialize Davinci Multimedia Application Interface 
     */
    Dmai_init();

    /*
     * Initialize the logs. Must be done after CERuntime_init() 
     */
    // TraceUtil_start(ENGINE_NAME);

    /*
     * Use supplied params if any, otherwise use defaults 
     */
    encParams = &defaultEncParams;
    encDynParams = &defaultEncDynParams;

    /*
     * Open the codec engine 
     */
    d->hEngine = Engine_open(ENGINE_NAME, NULL, NULL);

    if (d->hEngine == NULL) {
	ms_error("Failed to open codec engine %s\n", ENGINE_NAME);
	cleanUpQ = TRUE;
    }

    /*
     * Set the resolution to match the specified resolution 
     */
    encParams->maxWidth = d->vsize.width;
    encParams->maxHeight = d->vsize.height;
    encParams->inputChromaFormat = XDM_YUV_420P;

    /*
     * Set up encoder parameters depending on bit rate 
     */
    if (d->bitrate < 0) {
	/*
	 * Variable bit rate 
	 */
	encParams->rateControlPreset = IVIDEO_NONE;

	/*
	 * If variable bit rate use a bogus bit rate value (> 0)
	 * since it will be ignored.
	 */
	encParams->maxBitRate = 2000000;
    } else {
	/*
	 * Constant bit rate 
	 */
	encParams->rateControlPreset = IVIDEO_LOW_DELAY;
	encParams->maxBitRate = d->bitrate;
    }

    encDynParams->targetBitRate = encParams->maxBitRate;
    encDynParams->inputWidth = encParams->maxWidth;
    encDynParams->inputHeight = encParams->maxHeight;

    /*
     * Create the video encoder 
     */
    d->hVe1 = Venc1_create(d->hEngine, "h264enc", encParams, encDynParams);
    if (d->hVe1 == NULL) {
	ms_error("Failed to create video encoder: %s\n", "h264enc");
	cleanUpQ = TRUE;
    }

    gfxAttrs.colorSpace = ColorSpace_YUV420P;
    gfxAttrs.dim.width = d->vsize.width;
    gfxAttrs.dim.height = d->vsize.height;
    gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(gfxAttrs.dim.width,
						       gfxAttrs.
						       colorSpace);

    /*
     * Which input buffer size does the encoder require? 
     */
    bufSize = Venc1_getInBufSize(d->hVe1);

    /*
     * Allocate video buffer 
     */
    d->hVidBuf =
	Buffer_create(bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));

    /*
     * which output buffer size does the encoder require? 
     */
    bufSize = Venc1_getOutBufSize(d->hVe1);

    /*
     * Allocate buffer for encoded data 
     */
    d->hEncBuf = Buffer_create(bufSize, &bAttrs);

    if (cleanUpQ) {
	if (d->hVe1) {
	    Venc1_delete(d->hVe1);
	    d->hVe1 = NULL;
	}

	if (d->hEngine) {
	    Engine_close(d->hEngine);
	    d->hEngine = NULL;
	}
	if (d->hVidBuf) {
	    Buffer_delete(d->hVidBuf);
	    d->hVidBuf = NULL;
	}

	if (d->hEncBuf) {
	    Buffer_delete(d->hEncBuf);
	    d->hEncBuf = NULL;
	}
    }
}

static void
dmai_buffer_to_msgb(Buffer_Handle hEncBuf, MSQueue * nalus)
{
    mblk_t         *m;
    uint8_t        *src,
                   *end;
    src = (uint8_t *) Buffer_getUserPtr(hEncBuf) + 4;
    end = src + Buffer_getNumBytesUsed(hEncBuf) - 4;
    // tricks for the first encoded buffer from TI's demo H264 encoder
    while (src < (end - 4)) {
	m = allocb(Buffer_getNumBytesUsed(hEncBuf) - 4, 0);

	while (!(src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 1)
	       && src < (end - 4)) {
	    *(m->b_wptr)++ = *src++;
	}


	if (src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 1) {
	    src += 4;
	    if ((*(m->b_rptr) & 0x1f) == 7) {
		ms_message("A SPS is being sent.");
	    } else if ((*(m->b_rptr) & 0x1f) == 8) {
		ms_message("A PPS is being sent.");
	    }
	    ms_queue_put(nalus, m);
	} else {
	    *(m->b_wptr)++ = *src++;
	    *(m->b_wptr)++ = *src++;
	    *(m->b_wptr)++ = *src++;
	    *(m->b_wptr)++ = *src++;
	    if ((*(m->b_rptr) & 0x1f) == 7) {
		ms_message("A SPS is being sent.");
	    } else if ((*(m->b_rptr) & 0x1f) == 8) {
		ms_message("A PPS is being sent.");
	    }
	    ms_queue_put(nalus, m);
	}
    }
}

static void
enc_process(MSFilter * f)
{
    EncData        *d = (EncData *) f->data;
    uint32_t        ts = f->ticker->time * 90LL;
    mblk_t         *im;
    Int             ret = Dmai_EOK;

    MSQueue         nalus;
    ms_queue_init(&nalus);
    while ((im = ms_queue_get(f->inputs[0])) != NULL) {
	Buffer_setNumBytesUsed(d->hVidBuf, im->b_wptr - im->b_rptr);
	memcpy(Buffer_getUserPtr(d->hVidBuf), im->b_rptr,
	       Buffer_getNumBytesUsed(d->hVidBuf));

	/*
	 * Make sure the whole buffer is used for input 
	 */
	BufferGfx_resetDimensions(d->hVidBuf);

	Buffer_freeUseMask(d->hEncBuf, 0xffff);
	/*
	 * encode the video buffer 
	 */
	ret = Venc1_process(d->hVe1, d->hVidBuf, d->hEncBuf);

	if (ret < 0) {
	    ms_error("Failed to encode video buffer\n");
	}

	if (Buffer_getNumBytesUsed(d->hEncBuf) == 0) {
	    ms_error("Encoder created 0 sized output frame\n");
	}

	dmai_buffer_to_msgb(d->hEncBuf, &nalus);
	rfc3984_pack(&d->packer, &nalus, f->outputs[0], ts);
	d->framenum++;

	freemsg(im);
    }
}

static void
enc_postprocess(MSFilter * f)
{
    EncData        *d = (EncData *) f->data;
    rfc3984_uninit(&d->packer);
    /*
     * Clean up the thread before exiting 
     */
    if (d->hVe1) {
	Venc1_delete(d->hVe1);
	d->hVe1 = NULL;
    }

    if (d->hEngine) {
	Engine_close(d->hEngine);
	d->hEngine = NULL;
    }
    if (d->hVidBuf) {
	Buffer_delete(d->hVidBuf);
	d->hVidBuf = NULL;
    }

    if (d->hEncBuf) {
	Buffer_delete(d->hEncBuf);
	d->hEncBuf = NULL;
    }
}

static int
enc_set_br(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    d->bitrate = *(int *) arg;

    if (d->bitrate >= 1024000) {
	d->vsize = MS_VIDEO_SIZE_VGA;
	d->fps = 25;
    } else if (d->bitrate >= 512000) {
	d->vsize = MS_VIDEO_SIZE_VGA;
	d->fps = 15;
    } else if (d->bitrate >= 384000) {
	d->vsize = MS_VIDEO_SIZE_CIF;
	d->fps = 30;
    } else if (d->bitrate >= 256000) {
	d->vsize = MS_VIDEO_SIZE_CIF;
	d->fps = 15;
    } else if (d->bitrate >= 128000) {
	d->vsize = MS_VIDEO_SIZE_CIF;
	d->fps = 15;
    } else if (d->bitrate >= 64000) {
	d->vsize = MS_VIDEO_SIZE_CIF;
	d->fps = 10;
    } else if (d->bitrate >= 32000) {
	d->vsize = MS_VIDEO_SIZE_QCIF;
	d->fps = 10;
    } else {
	d->vsize = MS_VIDEO_SIZE_QCIF;
	d->fps = 5;
    }
    ms_message("bitrate set to %i", d->bitrate);
    return 0;
}

static int
enc_set_fps(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    d->fps = *(float *) arg;
    return 0;
}

static int
enc_get_fps(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    *(float *) arg = d->fps;
    return 0;
}

static int
enc_get_vsize(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    *(MSVideoSize *) arg = d->vsize;
    return 0;
}

static int
enc_set_vsize(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    d->vsize = *(MSVideoSize *) arg;
    return 0;
}

static int
enc_add_fmtp(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    const char     *fmtp = (const char *) arg;
    char            value[12];
    if (fmtp_get_value(fmtp, "packetization-mode", value, sizeof(value))) {
	d->mode = atoi(value);
	ms_message("packetization-mode set to %i", d->mode);
    }
    return 0;
}

static int
enc_req_vfu(MSFilter * f, void *arg)
{
    EncData        *d = (EncData *) f->data;
    d->generate_keyframe = TRUE;
    return 0;
}


static MSFilterMethod enc_methods[] = {
    {MS_FILTER_SET_FPS, enc_set_fps},
    {MS_FILTER_SET_BITRATE, enc_set_br},
    {MS_FILTER_GET_FPS, enc_get_fps},
    {MS_FILTER_GET_VIDEO_SIZE, enc_get_vsize},
    {MS_FILTER_SET_VIDEO_SIZE, enc_set_vsize},
    {MS_FILTER_ADD_FMTP, enc_add_fmtp},
    {MS_FILTER_REQ_VFU, enc_req_vfu},
    {0, NULL}
};

static MSFilterDesc h264_enc_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "SDH264Enc",
    .text = "A H264 encoder based on Davinci DSP",
    .category = MS_FILTER_ENCODER,
    .enc_fmt = "H264",
    .ninputs = 1,
    .noutputs = 1,
    .init = enc_init,
    .preprocess = enc_preprocess,
    .process = enc_process,
    .postprocess = enc_postprocess,
    .uninit = enc_uninit,
    .methods = enc_methods
};

typedef struct _DecData {
    Engine_Handle   hEngine;
    Vdec2_Handle    hVd2;
    BufTab_Handle   hBufTabImage;
    Buffer_Handle   hVidBuf;
    Buffer_Handle   hDecBuf;
    Buffer_Handle   hDispBuf;
    mblk_t         *sps,
                   *pps;
    Rfc3984Context  unpacker;
    unsigned int    packet_num;
    int             inBsBufSize;
} DecData;

static void
dec_init(MSFilter * f)
{
    DecData        *d = (DecData *) ms_new(DecData, 1);
    VIDDEC2_Params  defaultDecParams = Vdec2_Params_DEFAULT;
    VIDDEC2_DynamicParams defaultDecDynParams =
	Vdec2_DynamicParams_DEFAULT;
    VIDDEC2_Params *decParams;
    VIDDEC2_DynamicParams *decDynParams;
    BufferGfx_Attrs gfxAttrs = BufferGfx_Attrs_DEFAULT;
    Buffer_Attrs    bAttrs = Buffer_Attrs_DEFAULT;
    Int32           bufSize;

    bool_t          cleanUpQ = FALSE;

    d->hEngine = NULL;
    d->hVd2 = NULL;
    d->hBufTabImage = NULL;
    d->hVidBuf = NULL;
    d->hDecBuf = NULL;
    d->hDispBuf = NULL;
    d->sps = NULL;
    d->pps = NULL;
    rfc3984_init(&d->unpacker);
    d->packet_num = 0;
    d->inBsBufSize = 500000;
    f->data = d;

    /*
     * Initialize Codec Engine runtime 
     */
    CERuntime_init();

    /*
     * Initialize Davinci Multimedia Application Interface 
     */
    Dmai_init();

    /*
     * Initialize the logs. Must be done after CERuntime_init() 
     */
    // TraceUtil_start(ENGINE_NAME);

    /*
     * Use supplied params if any, otherwise use defaults 
     */
    decParams = &defaultDecParams;
    decDynParams = &defaultDecDynParams;

    /*
     * Open the codec engine 
     */
    d->hEngine = Engine_open(ENGINE_NAME, NULL, NULL);

    if (d->hEngine == NULL) {
	ms_error("Failed to open codec engine %s\n", ENGINE_NAME);
	cleanUpQ = TRUE;
    }

    decParams->maxWidth = MS_VIDEO_SIZE_CIF_W;
    decParams->maxHeight = MS_VIDEO_SIZE_CIF_H;
    decParams->forceChromaFormat = XDM_YUV_420P;

    /*
     * Create the video decoder 
     */
    d->hVd2 = Vdec2_create(d->hEngine, "h264dec", decParams, decDynParams);

    if (d->hVd2 == NULL) {
	ms_error("Failed to create video decoder: %s\n", "h264dec");
	cleanUpQ = TRUE;
    }
    // The default transmitting size in linphone is set to
    // MS_VIDEO_SIZE_CIF
    gfxAttrs.colorSpace = ColorSpace_YUV420P;
    gfxAttrs.dim.width = MS_VIDEO_SIZE_CIF_W;
    gfxAttrs.dim.height = MS_VIDEO_SIZE_CIF_H;
    gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(gfxAttrs.dim.width,
						       gfxAttrs.
						       colorSpace);

    /*
     * Which output buffer size does the codec require? 
     */
    bufSize = Vdec2_getOutBufSize(d->hVd2);

    /*
     * Allocate video buffers 
     */
    d->hBufTabImage = BufTab_create(DISPLAY_PIPE_SIZE, bufSize,
				    BufferGfx_getBufferAttrs(&gfxAttrs));

    if (d->hBufTabImage == NULL) {
	ms_error("Failed to create BufTab for decoder\n");
	cleanUpQ = TRUE;
    }

    /*
     * The codec is going to use this BufTab for output buffers 
     */
    Vdec2_setBufTab(d->hVd2, d->hBufTabImage);

    d->hVidBuf = BufTab_getFreeBuf(d->hBufTabImage);

    /*
     * which input buffer size does the decoder require? 
     */
    /*
     * maybe it can be obtained by the encoder, because the required 
     */
    /*
     * buffer size determined by the VDEC2 is 1.5M, but it is not 
     */
    /*
     * needed by the real decoder 
     */
    if (d->inBsBufSize == 0) {
	bufSize = Vdec2_getInBufSize(d->hVd2);
    } else {
	bufSize = d->inBsBufSize;
    }

    /*
     * Allocate buffer for encoded data 
     */
    d->hDecBuf = Buffer_create(bufSize, &bAttrs);

    if (d->hDecBuf == NULL) {
	ms_error("Failed to allocate Buffer for decoder input\n");
	cleanUpQ = TRUE;
    }

    if (cleanUpQ) {
	/*
	 * Clean up the thread before exiting 
	 */
	if (d->hVd2) {
	    Vdec2_delete(d->hVd2);
	    d->hVd2 = NULL;
	}

	if (d->hEngine) {
	    Engine_close(d->hEngine);
	    d->hEngine = NULL;
	}
	if (d->hBufTabImage) {
	    BufTab_delete(d->hBufTabImage);
	    d->hBufTabImage = NULL;
	}

	if (d->hVidBuf) {
	    Buffer_delete(d->hVidBuf);
	    d->hVidBuf = NULL;
	}

	if (d->hDecBuf) {
	    Buffer_delete(d->hDecBuf);
	    d->hDecBuf = NULL;
	}
    }
}

static void
dec_uninit(MSFilter * f)
{
    DecData        *d = (DecData *) f->data;
    rfc3984_uninit(&d->unpacker);
    /*
     * Clean up the thread before exiting 
     */
    if (d->hVd2) {
	Vdec2_delete(d->hVd2);
	d->hVd2 = NULL;
    }

    if (d->hEngine) {
	Engine_close(d->hEngine);
	d->hEngine = NULL;
    }
    if (d->hBufTabImage) {
	BufTab_delete(d->hBufTabImage);
	d->hBufTabImage = NULL;
    }

    if (d->hVidBuf) {
	Buffer_delete(d->hVidBuf);
	d->hVidBuf = NULL;
    }

    if (d->hDecBuf) {
	Buffer_delete(d->hDecBuf);
	d->hDecBuf = NULL;
    }

    if (d->sps)
	freemsg(d->sps);
    if (d->pps)
	freemsg(d->pps);
    ms_free(d);
}

static void
update_sps(DecData * d, mblk_t * sps)
{
    if (d->sps)
	freemsg(d->sps);
    d->sps = dupb(sps);
}

static void
update_pps(DecData * d, mblk_t * pps)
{
    if (d->pps)
	freemsg(d->pps);
    if (pps)
	d->pps = dupb(pps);
    else
	d->pps = NULL;
}

static bool_t
check_sps_pps_change(DecData * d, mblk_t * sps, mblk_t * pps)
{
    bool_t          ret1 = FALSE,
	ret2 = FALSE;
    if (d->sps) {
	if (sps) {
	    ret1 = (msgdsize(sps) != msgdsize(d->sps))
		|| (memcmp(d->sps->b_rptr, sps->b_rptr, msgdsize(sps)) !=
		    0);
	    if (ret1) {
		update_sps(d, sps);
		ms_message("SPS changed !");
		update_pps(d, NULL);
	    }
	}
    } else if (sps) {
	ms_message("Receiving first SPS");
	update_sps(d, sps);
    }
    if (d->pps) {
	if (pps) {
	    ret2 = (msgdsize(pps) != msgdsize(d->pps))
		|| (memcmp(d->pps->b_rptr, pps->b_rptr, msgdsize(pps)) !=
		    0);
	    if (ret2) {
		ms_message("PPS changed ! %i,%i", msgdsize(pps),
			   msgdsize(d->pps));
		update_pps(d, pps);
	    }
	}
    } else if (pps) {
	ms_message("Receiving first PPS");
	update_pps(d, pps);
    }
    return ret1 || ret2;
}

static void
get_as_yuvmsg(Buffer_Handle hVidBuf, mblk_t * orig)
{
    orig = allocb(Buffer_getNumBytesUsed(hVidBuf), 0);

    memcpy(orig->b_wptr, Buffer_getUserPtr(hVidBuf),
	   Buffer_getNumBytesUsed(hVidBuf));
    orig->b_wptr += Buffer_getNumBytesUsed(hVidBuf);
}

static void
nalusToFrame(DecData *d, Buffer_Handle hDecBuf, MSQueue * naluq)
{
    mblk_t         *im;
    uint8_t        *dst,
                   *src,
                   *end;
    int             nal_len;
    bool_t          start_picture = TRUE;
    uint8_t         nalu_type;

    dst = (uint8_t *) Buffer_getUserPtr(hDecBuf);
    end = dst + Buffer_getSize(hDecBuf);
    while ((im = ms_queue_get(naluq)) != NULL) {
	src = im->b_rptr;
	nal_len = im->b_wptr - src;

        nalu_type = (*src) & 0x1f;
        if (nalu_type == 7)
            check_sps_pps_change(d, im, NULL);
        if (nalu_type == 8)
            check_sps_pps_change(d, NULL, im);
        if (start_picture || nalu_type == 7/*SPS*/ || nalu_type == 8/*PPS*/ ) {
            *dst++ = 0;
            start_picture = FALSE;
        }
        /*prepend nal marker*/
        *dst++ = 0;
        *dst++ = 0;
        *dst++ = 1;
        *dst++ = *src++;

	while (src < (im->b_wptr - 3)) {
	    if (src[0] == 0 && src[1] == 0 && src[2] < 3) {
                *dst++ = 0;
                *dst++ = 0;
                *dst++ = 3;
		src += 2;
	    }
	    *dst++ = *src++;
	}
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
	freemsg(im);
    }
    Buffer_setNumBytesUsed(hDecBuf,
			   (Int8 *) dst - Buffer_getUserPtr(hDecBuf));
}

static void
dec_process(MSFilter * f)
{
    DecData        *d = (DecData *) f->data;
    mblk_t         *im;
    MSQueue         nalus;
    Int             ret = Dmai_EOK;
    mblk_t         *orig = NULL;

    ms_queue_init(&nalus);
    while ((im = ms_queue_get(f->inputs[0])) != NULL) {
        /*push the sps/pps given in sprop-parameter-sets if any*/
        // if (d->packet_num==0 && d->sps && d->pps){
        //     mblk_set_timestamp_info(d->sps,mblk_get_timestamp_info(im));
        //     mblk_set_timestamp_info(d->pps,mblk_get_timestamp_info(im));
        //     rfc3984_unpack(&d->unpacker,d->sps,&nalus);
        //     rfc3984_unpack(&d->unpacker,d->pps,&nalus);
        //     d->sps=NULL;
        //     d->pps=NULL;
        // }
	rfc3984_unpack(&d->unpacker, im, &nalus);
	if (!ms_queue_empty(&nalus)) {

	    nalusToFrame(d, d->hDecBuf, &nalus);
	    /*
	     * Make sure the whole buffer is used for input and output 
	     */
	    BufferGfx_resetDimensions(d->hVidBuf);

	    ret = Vdec2_process(d->hVd2, d->hDecBuf, d->hVidBuf);

	    if (ret != Dmai_EOK) {
		ms_error("Failed to decode video buffer\n");
		break;
	    }

	    /*
	     * Send display frames to display thread 
	     */
	    d->hDispBuf = Vdec2_getDisplayBuf(d->hVd2);
	    while (d->hDispBuf) {
		get_as_yuvmsg(d->hDispBuf, orig);
		ms_queue_put(f->outputs[0], orig);

		d->hDispBuf = Vdec2_getDisplayBuf(d->hVd2);
	    }


	    /*
	     * Free up released frames 
	     */
	    d->hVidBuf = Vdec2_getFreeBuf(d->hVd2);
	    while (d->hVidBuf) {
		Buffer_freeUseMask(d->hVidBuf, 0xffff);
		d->hVidBuf = Vdec2_getFreeBuf(d->hVd2);
	    }

	    /*
	     * Get a free buffer 
	     */
	    d->hVidBuf = BufTab_getFreeBuf(d->hBufTabImage);

	    if (d->hVidBuf == NULL) {
		ms_error
		    ("Failed to get free buffer from BufTab (in while)\n");
	    }

	}
	d->packet_num++;
    }
}

static int
dec_add_fmtp(MSFilter * f, void *arg)
{
    DecData        *d = (DecData *) f->data;
    const char     *fmtp = (const char *) arg;
    char            value[256];
    if (fmtp_get_value(fmtp, "sprop-parameter-sets", value, sizeof(value))) {
	char           *b64_sps = value;
	char           *b64_pps = strchr(value, ',');
	if (b64_pps) {
	    *b64_pps = '\0';
	    ++b64_pps;
	    ms_message("Got sprop-parameter-sets : sps=%s , pps=%s",
		       b64_sps, b64_pps);
	    d->sps = allocb(sizeof(value), 0);
	    d->sps->b_wptr +=
		b64_decode(b64_sps, strlen(b64_sps), d->sps->b_wptr,
			   sizeof(value));
	    d->pps = allocb(sizeof(value), 0);
	    d->pps->b_wptr +=
		b64_decode(b64_pps, strlen(b64_pps), d->pps->b_wptr,
			   sizeof(value));
	}
    }
    return 0;
}

static MSFilterMethod h264_dec_methods[] = {
    {MS_FILTER_ADD_FMTP, dec_add_fmtp},
    {0, NULL}
};

static MSFilterDesc h264_dec_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "SDH264Dec",
    .text = "A H264 decoder based on Davinci DSP.",
    .category = MS_FILTER_DECODER,
    .enc_fmt = "H264",
    .ninputs = 1,
    .noutputs = 1,
    .init = dec_init,
    .process = dec_process,
    .uninit = dec_uninit,
    .methods = h264_dec_methods
};

void
libsdh264_init(void)
{
    ms_filter_register(&h264_enc_desc);
    ms_filter_register(&h264_dec_desc);
    ms_message("SD-H264-" VERSION " plugin registered.");
}
