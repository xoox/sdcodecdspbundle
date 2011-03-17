/*
 * ------------------------------------------------------------------
 * AMR-NB encoder decoder library for linephone Copyright (C) 2011 Hu
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

#ifndef SUDA_AMRNB_INTERF_DEC_H
#define SUDA_AMRNB_INTERF_DEC_H

#ifdef __cplusplus
extern          "C" {
#endif

    void           *Decoder_Interface_init(void);
    void            Decoder_Interface_exit(void *state);
    void            Decoder_Interface_Decode(void *state,
					     const unsigned char *in,
					     short *out, int bfi);

#ifdef __cplusplus
}
#endif
#endif
