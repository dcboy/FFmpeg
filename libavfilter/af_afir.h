/*
 * Copyright (c) 2017 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_AFIR_H
#define AVFILTER_AFIR_H

#include "libavutil/float_dsp.h"
#include "libavutil/frame.h"
#include "libavutil/rational.h"
#include "libavutil/tx.h"
#include "avfilter.h"
#include "af_afirdsp.h"

#define MAX_IR_STREAMS 32

typedef struct AudioFIRSegment {
    int nb_partitions;
    int part_size;
    int block_size;
    int fft_length;
    int coeff_size;
    int input_size;
    int input_offset;

    int *output_offset;
    int *part_index;

    AVFrame *sumin;
    AVFrame *sumout;
    AVFrame *blockout;
    AVFrame *tempin;
    AVFrame *tempout;
    AVFrame *buffer;
    AVFrame *coeff;
    AVFrame *input;
    AVFrame *output;

    AVTXContext **ctx, **tx, **itx;
    av_tx_fn ctx_fn, tx_fn, itx_fn;
} AudioFIRSegment;

typedef struct AudioFIRContext {
    const AVClass *class;

    float wet_gain;
    float dry_gain;
    float length;
    int gtype;
    float ir_norm;
    float ir_link;
    float ir_gain;
    int ir_format;
    int ir_load;
    float max_ir_len;
    int response;
    int w, h;
    AVRational frame_rate;
    int ir_channel;
    int minp;
    int maxp;
    int nb_irs;
    int prev_selir;
    int selir;
    int precision;
    int format;

    int eof_coeffs[MAX_IR_STREAMS];
    int have_coeffs[MAX_IR_STREAMS];
    int nb_taps[MAX_IR_STREAMS];
    int nb_segments[MAX_IR_STREAMS];
    int max_offset[MAX_IR_STREAMS];
    int nb_channels;
    int one2many;
    int *loading;
    double *ch_gain;

    AudioFIRSegment seg[MAX_IR_STREAMS][1024];

    AVFrame *in;
    AVFrame *xfade[2];
    AVFrame *fadein[2];
    AVFrame *ir[MAX_IR_STREAMS];
    AVFrame *norm_ir[MAX_IR_STREAMS];
    int min_part_size;
    int max_part_size;
    int64_t pts;

    AudioFIRDSPContext afirdsp;
    AVFloatDSPContext *fdsp;
} AudioFIRContext;

#endif /* AVFILTER_AFIR_H */
