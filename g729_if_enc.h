/*
 * ------------------------------------------------------------------
 * G729AB encoder decoder library for linephone Copyright (C) 2011 Hu
 * Jianling Soochow University, All rights reserved. jlhu@suda.edu.cn
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the aggrement of Hu Jianling; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. *
 * ------------------------------------------------------------------- 
 */

#ifndef SUDA_G729AB_INTERF_ENC_H
#define SUDA_G729AB_INTERF_ENC_H

#ifdef __cplusplus
extern          "C" {
#endif

    void           *G729_Encoder_Interface_init(int dtx);
    void            G729_Encoder_Interface_exit(void *state);
    void            G729_Encoder_Interface_ctrl(void *state, int dtx);
    int             G729_Encoder_Interface_Encode(void *state,
					     const short *speech,
					     unsigned char *out);

#ifdef __cplusplus
}
#endif
#endif
