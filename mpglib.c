/*
 * Mpeg Layer-3 audio decoder
 * --------------------------
 *
 * Nearly all of this code has been borrowed from the mpglib program
 * written by Michael Hipp. For the original mpglib code, see
 * http://ftp.tu-clausthal.de/pub/unix/audio/mpg123 and
 * http://www.mpg123.de/
 *
 * Copyright (c) 1995,1996,1997 by Michael Hipp. All rights reserved.
 */

#include <ctype.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mpglib.h"

/************************************************************************
 * 			Code from mpglib common.c			*
 ************************************************************************/

struct parameter param = {1, 1, 0, 0};

int tabsel_123[2][3][16] = {
    {   {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}},

    {   {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}}
};

long freqs[9] = {44100, 48000, 32000, 22050, 24000, 16000,
                 11025, 12000, 8000};

int bitindex;
unsigned char *wordpointer;
unsigned char *pcm_sample;
int pcm_point = 0;
/*
 * decode a header and write the information
 * into the frame structure
 */
int decode_header(struct frame * fr, unsigned long newhead)
{
    if (newhead & (1 << 20)) {
        fr->lsf = (newhead & (1 << 19)) ? 0x0 : 0x1;
        fr->mpeg25 = 0;
    } else {
        fr->lsf = 1;
        fr->mpeg25 = 1;
    }
//  fr->lay = 4 - ((newhead >> 17) & 3);
//#warning: lay=3 is a dirty hack
    fr->lay = 3;
    if (((newhead >> 10) & 0x3) == 0x3) {
//     fprintf(stderr, "   l.65: Stream error \n");
//    exit(1);
        return(0); /* JP */
    }
    if (fr->mpeg25) {
        fr->sampling_frequency = 6 + ((newhead >> 10) & 0x3);
    } else
        fr->sampling_frequency = ((newhead >> 10) & 0x3) + (fr->lsf * 3);
    fr->error_protection = ((newhead >> 16) & 0x1) ^ 0x1;

//  if (fr->mpeg25)		/* allow Bitrate change for 2.5 ... */
//    fr->bitrate_index = ((newhead >> 12) & 0xf);

    fr->bitrate_index = ((newhead >> 12) & 0xf);
    fr->padding = ((newhead >> 9) & 0x1);
    fr->extension = ((newhead >> 8) & 0x1);
    fr->mode = ((newhead >> 6) & 0x3);
    fr->mode_ext = ((newhead >> 4) & 0x3);
    fr->copyright = ((newhead >> 3) & 0x1);
    fr->original = ((newhead >> 2) & 0x1);
    fr->emphasis = newhead & 0x3;

    fr->stereo = (fr->mode == MPG_MD_MONO) ? 1 : 2;

    if (!fr->bitrate_index) {
        //fprintf(stderr, "l.89: Free format not supported.\n");
        return (0);
    }
    switch (fr->lay) {
    case 1:
#ifdef LAYER1
#if 0
        fr->jsbound = (fr->mode == MPG_MD_JOINT_STEREO) ?
                      (fr->mode_ext<<2)+4 : 32;
#endif
        fr->framesize  = (long) tabsel_123[fr->lsf][0][fr->bitrate_index] * 12000;
        fr->framesize /= freqs[fr->sampling_frequency];
        fr->framesize  = ((fr->framesize+fr->padding)<<2)-4;
#else
        fprintf(stderr,"Not supported!\n");
#endif
        break;
    case 2:
#ifdef LAYER2
#if 0
        fr->jsbound = (fr->mode == MPG_MD_JOINT_STEREO) ?
                      (fr->mode_ext<<2)+4 : fr->II_sblimit;
#endif
        fr->framesize = (long) tabsel_123[fr->lsf][1][fr->bitrate_index] * 144000;
        fr->framesize /= freqs[fr->sampling_frequency];
        fr->framesize += fr->padding - 4;
#else
       //fprintf(stderr, "   l.95: Not supported!\n");
#endif
        break;
    case 3:
#if 0
        fr->do_layer = do_layer3;
        if(fr->lsf)
            ssize = (fr->stereo == 1) ? 9 : 17;
        else
            ssize = (fr->stereo == 1) ? 17 : 32;
#endif

#if 0
        if(fr->error_protection)
            ssize += 2;
#endif
        fr->framesize = (long) tabsel_123[fr->lsf][2][fr->bitrate_index] * 144000;
        fr->framesize /= freqs[fr->sampling_frequency] << (fr->lsf);
        fr->framesize = fr->framesize + fr->padding - 4;
        break;
    default:
        //fprintf(stderr, "   l.103: Sorry, unknown layer type.\n");
        return (0);
    }
    return 1;
}

unsigned int getbits(int number_of_bits)
{
    unsigned long rval;

    if (!number_of_bits)
        return 0;

    {
        rval = wordpointer[0];
        rval <<= 8;
        rval |= wordpointer[1];
        rval <<= 8;
        rval |= wordpointer[2];
        rval <<= bitindex;
        rval &= 0xffffff;

        bitindex += number_of_bits;

        rval >>= (24 - number_of_bits);

        wordpointer += (bitindex >> 3);
        bitindex &= 7;
    }
    return rval;
}

unsigned int getbits_fast(int number_of_bits)
{
    unsigned long rval;

    {
        rval = wordpointer[0];
        rval <<= 8;
        rval |= wordpointer[1];
        rval <<= bitindex;
        rval &= 0xffff;
        bitindex += number_of_bits;

        rval >>= (16 - number_of_bits);

        wordpointer += (bitindex >> 3);
        bitindex &= 7;
    }
    return rval;
}

unsigned int get1bit(void)
{
    unsigned char rval;

    rval = *wordpointer << bitindex;

    bitindex++;
    wordpointer += (bitindex >> 3);
    bitindex &= 7;

    return rval >> 7;
}


/************************************************************************
 * 			Code from mpglib layer3.c			*
 ************************************************************************/

/* Global mp .. it's a hack */
struct mpstr *gmp=NULL;

static real ispow[8207];
static real gainpow2[256 + 118 + 4];

struct bandInfoStruct {
    short longIdx[23];
    short longDiff[22];
    short shortIdx[14];
    short shortDiff[13];
};

int longLimit[9][23];
int shortLimit[9][14];

struct bandInfoStruct bandInfo[9] = {

    /* MPEG 1.0 */
    {   {0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576},
        {4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158},
        {0, 4 * 3, 8 * 3, 12 * 3, 16 * 3, 22 * 3, 30 * 3, 40 * 3, 52 * 3, 66 * 3, 84 * 3, 106 * 3, 136 * 3, 192 * 3},
        {4, 4, 4, 4, 6, 8, 10, 12, 14, 18, 22, 30, 56}},

    {   {0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576},
        {4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192},
        {0, 4 * 3, 8 * 3, 12 * 3, 16 * 3, 22 * 3, 28 * 3, 38 * 3, 50 * 3, 64 * 3, 80 * 3, 100 * 3, 126 * 3, 192 * 3},
        {4, 4, 4, 4, 6, 6, 10, 12, 14, 16, 20, 26, 66}},

    {   {0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576},
        {4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26},
        {0, 4 * 3, 8 * 3, 12 * 3, 16 * 3, 22 * 3, 30 * 3, 42 * 3, 58 * 3, 78 * 3, 104 * 3, 138 * 3, 180 * 3, 192 * 3},
        {4, 4, 4, 4, 6, 8, 12, 16, 20, 26, 34, 42, 12}},
    /* MPEG 2.0 */
    {   {0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
        {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54},
        {0, 4 * 3, 8 * 3, 12 * 3, 18 * 3, 24 * 3, 32 * 3, 42 * 3, 56 * 3, 74 * 3, 100 * 3, 132 * 3, 174 * 3, 192 * 3},
        {4, 4, 4, 6, 6, 8, 10, 14, 18, 26, 32, 42, 18}},

    {   {0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 114, 136, 162, 194, 232, 278, 330, 394, 464, 540, 576},
        {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 18, 22, 26, 32, 38, 46, 52, 64, 70, 76, 36},
        {0, 4 * 3, 8 * 3, 12 * 3, 18 * 3, 26 * 3, 36 * 3, 48 * 3, 62 * 3, 80 * 3, 104 * 3, 136 * 3, 180 * 3, 192 * 3},
        {4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 32, 44, 12}},

    {   {0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
        {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54},
        {0, 4 * 3, 8 * 3, 12 * 3, 18 * 3, 26 * 3, 36 * 3, 48 * 3, 62 * 3, 80 * 3, 104 * 3, 134 * 3, 174 * 3, 192 * 3},
        {4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18}},
    /* MPEG 2.5 */
    {   {0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
        {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54},
        {0, 12, 24, 36, 54, 78, 108, 144, 186, 240, 312, 402, 522, 576},
        {4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18}},

    {   {0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
        {6, 6, 6, 6, 6, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 38, 46, 52, 60, 68, 58, 54},
        {0, 12, 24, 36, 54, 78, 108, 144, 186, 240, 312, 402, 522, 576},
        {4, 4, 4, 6, 8, 10, 12, 14, 18, 24, 30, 40, 18}},

    {   {0, 12, 24, 36, 48, 60, 72, 88, 108, 132, 160, 192, 232, 280, 336, 400, 476, 566, 568, 570, 572, 574, 576},
        {12, 12, 12, 12, 12, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 76, 90, 2, 2, 2, 2, 2},
        {0, 24, 48, 72, 108, 156, 216, 288, 372, 480, 486, 492, 498, 576},
        {8, 8, 8, 12, 16, 20, 24, 28, 36, 2, 2, 2, 26}},
};

static int mapbuf0[9][152];
static int mapbuf1[9][156];
static int mapbuf2[9][44];
static int *map[9][3];
static int *mapend[9][3];

static unsigned int n_slen2[512];	/* MPEG 2.0 slen for 'normal' mode */
static unsigned int i_slen2[256];	/* MPEG 2.0 slen for intensity stereo */

//#define MPGLIB02a	// experimental
#ifdef MPGLIB02a
static real tan1_1[16],tan2_1[16],tan1_2[16],tan2_2[16];
static real pow1_1[2][16],pow2_1[2][16],pow1_2[2][16],pow2_2[2][16];
#endif

/*
 * init tables for layer-3
 */
void init_layer3(int down_sample_sblimit)
{
    int i, j, k, l;

    for (i = -256; i < 118 + 4; i++)
        gainpow2[i + 256] = pow((real) 2.0, -0.25 * (real) (i + 210));

    for (i = 0; i < 8207; i++)
        ispow[i] = pow((real) i, (real) 4.0 / 3.0);

    for (j = 0; j < 9; j++) {
        struct bandInfoStruct *bi = &bandInfo[j];
        int *mp;
        int cb, lwin;
        short *bdf;

        mp = map[j][0] = mapbuf0[j];
        bdf = bi->longDiff;
        for (i = 0, cb = 0; cb < 8; cb++, i += *bdf++) {
            *mp++ = (*bdf) >> 1;
            *mp++ = i;
            *mp++ = 3;
            *mp++ = cb;
        }
        bdf = bi->shortDiff + 3;
        for (cb = 3; cb < 13; cb++) {
            int l = (*bdf++) >> 1;

            for (lwin = 0; lwin < 3; lwin++) {
                *mp++ = l;
                *mp++ = i + lwin;
                *mp++ = lwin;
                *mp++ = cb;
            }
            i += 6 * l;
        }
        mapend[j][0] = mp;

        mp = map[j][1] = mapbuf1[j];
        bdf = bi->shortDiff + 0;
        for (i = 0, cb = 0; cb < 13; cb++) {
            int l = (*bdf++) >> 1;

            for (lwin = 0; lwin < 3; lwin++) {
                *mp++ = l;
                *mp++ = i + lwin;
                *mp++ = lwin;
                *mp++ = cb;
            }
            i += 6 * l;
        }
        mapend[j][1] = mp;

        mp = map[j][2] = mapbuf2[j];
        bdf = bi->longDiff;
        for (cb = 0; cb < 22; cb++) {
            *mp++ = (*bdf++) >> 1;
            *mp++ = cb;
        }
        mapend[j][2] = mp;

    }

    for (j = 0; j < 9; j++) {
        for (i = 0; i < 23; i++) {
            longLimit[j][i] = (bandInfo[j].longIdx[i] - 1 + 8) / 18 + 1;
            if (longLimit[j][i] > (down_sample_sblimit))
                longLimit[j][i] = down_sample_sblimit;
        }
        for (i = 0; i < 14; i++) {
            shortLimit[j][i] = (bandInfo[j].shortIdx[i] - 1) / 18 + 1;
            if (shortLimit[j][i] > (down_sample_sblimit))
                shortLimit[j][i] = down_sample_sblimit;
        }
    }

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 6; j++) {
            for (k = 0; k < 6; k++) {
                int n = k + j * 6 + i * 36;

                i_slen2[n] = i | (j << 3) | (k << 6) | (3 << 12);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 4; k++) {
                int n = k + j * 4 + i * 16;

                i_slen2[n + 180] = i | (j << 3) | (k << 6) | (4 << 12);
            }
        }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            int n = j + i * 3;

            i_slen2[n + 244] = i | (j << 3) | (5 << 12);
            n_slen2[n + 500] = i | (j << 3) | (2 << 12) | (1 << 15);
        }
    }

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            for (k = 0; k < 4; k++) {
                for (l = 0; l < 4; l++) {
                    int n = l + k * 4 + j * 16 + i * 80;

                    n_slen2[n] = i | (j << 3) | (k << 6) | (l << 9) | (0 << 12);
                }
            }
        }
    }
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            for (k = 0; k < 4; k++) {
                int n = k + j * 4 + i * 20;

                n_slen2[n + 400] = i | (j << 3) | (k << 6) | (1 << 12);
            }
        }
    }
#ifdef MPGLIB02a
  for(i=0;i<16;i++)
  {
    double t = tan( (double) i * M_PI / 12.0 );
    tan1_1[i] = t / (1.0+t);
    tan2_1[i] = 1.0 / (1.0 + t);
    tan1_2[i] = M_SQRT2 * t / (1.0+t);
    tan2_2[i] = M_SQRT2 / (1.0 + t);

    for(j=0;j<2;j++) {
      double base = pow(2.0,-0.25*(j+1.0));
      double p1=1.0,p2=1.0;
      if(i > 0) {
        if( i & 1 )
          p1 = pow(base,(i+1.0)*0.5);
        else
          p2 = pow(base,i*0.5);
      }
      pow1_1[j][i] = p1;
      pow2_1[j][i] = p2;
      pow1_2[j][i] = M_SQRT2 * p1;
      pow2_2[j][i] = M_SQRT2 * p2;
    }
  }
#endif
}

/*
 * read additional side information
 */
static int III_get_side_info_1(struct III_sideinfo * si, int stereo,
                                int ms_stereo, long sfreq, int single)
{
    int ch, gr;
    int powdiff = (single == 3) ? 4 : 0;

    si->main_data_begin = getbits(9);
    if (stereo == 1)
        si->private_bits = getbits_fast(5);
    else
        si->private_bits = getbits_fast(3);

    for (ch = 0; ch < stereo; ch++) {
        si->ch[ch].gr[0].scfsi = -1;
        si->ch[ch].gr[1].scfsi = getbits_fast(4);
    }

    for (gr = 0; gr < 2; gr++) {
        for (ch = 0; ch < stereo; ch++) {
            register struct gr_info_s *gr_info = &(si->ch[ch].gr[gr]);

            gr_info->part2_3_length = getbits(12);
            gr_info->big_values = getbits_fast(9);
            if (gr_info->big_values > 288) {
                //fprintf(stderr, "l.403: big_values too large!\n");
                gr_info->big_values = 288;
            }
            gr_info->pow2gain = gainpow2 + 256 - getbits_fast(8) + powdiff;
            if (ms_stereo)
                gr_info->pow2gain += 2;
            gr_info->scalefac_compress = getbits_fast(4);
            /* window-switching flag == 1 for block_Type != 0 ..
             * and block-type == 0 -> win-sw-flag = 0
             */
            if (get1bit()) {
                int i;

                gr_info->block_type = getbits_fast(2);
                gr_info->mixed_block_flag = get1bit();
                gr_info->table_select[0] = getbits_fast(5);
                gr_info->table_select[1] = getbits_fast(5);
                /*
                   table_select[2] not needed, because there is no region2, but to
                   satisfy some verifications tools we set it either.
                */
                gr_info->table_select[2] = 0;
                for (i = 0; i < 3; i++)
                    gr_info->full_gain[i] = gr_info->pow2gain + (getbits_fast(3) << 3);

                gr_info->block_type = 1; /* JP: dirty hack! */
                if (gr_info->block_type == 0) {
                    fprintf(stderr,"   l.429: Blocktype == 0 and window-switching == 1 not allowed.\n");
                    return 0;
                }
                /* region_count/start parameters are implicit in this case. */
                gr_info->region1start = 36 >> 1;
                gr_info->region2start = 576 >> 1;
            } else {
                int i, r0c, r1c;

                for (i = 0; i < 3; i++)
                    gr_info->table_select[i] = getbits_fast(5);
                r0c = getbits_fast(4);
                r1c = getbits_fast(3);
                gr_info->region1start = bandInfo[sfreq].longIdx[r0c + 1] >> 1;
                gr_info->region2start = bandInfo[sfreq].longIdx[r0c + 1 + r1c + 1] >> 1;
                gr_info->block_type = 0;
                gr_info->mixed_block_flag = 0;
            }
            gr_info->preflag = get1bit();
            gr_info->scalefac_scale = get1bit();
            gr_info->count1table_select = get1bit();
        }
    }
	return !0;
}

/*
 * Side Info for MPEG 2.0 / LSF
 */
static int III_get_side_info_2(struct III_sideinfo *si,int stereo,
                                int ms_stereo,long sfreq,int single)
{
    int ch;
    int powdiff = (single == 3) ? 4 : 0;

    si->main_data_begin = getbits(8);
    if (stereo == 1)
        si->private_bits = get1bit();
    else
        si->private_bits = getbits_fast(2);

    for (ch=0; ch<stereo; ch++) {
        register struct gr_info_s *gr_info = &(si->ch[ch].gr[0]);

        gr_info->part2_3_length = getbits(12);
        gr_info->big_values = getbits_fast(9);
        if(gr_info->big_values > 288) {
            //fprintf(stderr,"l. 478: big_values too large!\n");
            gr_info->big_values = 288;
        }
        gr_info->pow2gain = gainpow2+256 - getbits_fast(8) + powdiff;
        if(ms_stereo)
            gr_info->pow2gain += 2;
        gr_info->scalefac_compress = getbits(9);
        /* window-switching flag == 1 for block_Type != 0 .. and block-type == 0 -> win-sw-flag = 0 */
        if(get1bit()) {
            int i;
            gr_info->block_type = getbits_fast(2);
            gr_info->mixed_block_flag = get1bit();
            gr_info->table_select[0] = getbits_fast(5);
            gr_info->table_select[1] = getbits_fast(5);
            /*
             * table_select[2] not needed, because there is no region2,
             * but to satisfy some verifications tools we set it either.
             */
            gr_info->table_select[2] = 0;
            for(i=0; i<3; i++)
                gr_info->full_gain[i] = gr_info->pow2gain + (getbits_fast(3)<<3);

            gr_info->block_type = 1; /* JP: dirty hack! */
            if(gr_info->block_type == 0) {
                fprintf(stderr,"l.502: Blocktype == 0 and window-switching == 1 not allowed.\n");
                return 0;
            }
            /* region_count/start parameters are implicit in this case. */
            /* check this again! */
            if(gr_info->block_type == 2)
                gr_info->region1start = 36>>1;
            else if(sfreq == 8)
                /* check this for 2.5 and sfreq=8 */
                gr_info->region1start = 108>>1;
            else
                gr_info->region1start = 54>>1;
            gr_info->region2start = 576>>1;
        } else {
            int i,r0c,r1c;
            for (i=0; i<3; i++)
                gr_info->table_select[i] = getbits_fast(5);
            r0c = getbits_fast(4);
            r1c = getbits_fast(3);
            gr_info->region1start = bandInfo[sfreq].longIdx[r0c+1] >> 1 ;
            gr_info->region2start = bandInfo[sfreq].longIdx[r0c+1+r1c+1] >> 1;
            gr_info->block_type = 0;
            gr_info->mixed_block_flag = 0;
        }
        gr_info->scalefac_scale = get1bit();
        gr_info->count1table_select = get1bit();
    }
	return !0;
}

/*
 * read scalefactors
 */
static int III_get_scale_factors_1(int *scf,struct gr_info_s *gr_info)
{
    static const unsigned char slen[2][16] = {
        {0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4},
        {0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3}};
    int numbits;
    int num0 = slen[0][gr_info->scalefac_compress];
    int num1 = slen[1][gr_info->scalefac_compress];

    if (gr_info->block_type == 2) {
        int i=18;
        numbits = (num0 + num1) * 18;

        if (gr_info->mixed_block_flag) {
            for (i=8; i; i--)
                *scf++ = getbits_fast(num0);
            i = 9;
            numbits -= num0; /* num0 * 17 + num1 * 18 */
        }

        for (; i; i--)
            *scf++ = getbits_fast(num0);
        for (i = 18; i; i--)
            *scf++ = getbits_fast(num1);
        *scf++ = 0;
        *scf++ = 0;
        *scf++ = 0; /* short[13][0..2] = 0 */
    } else {
        int i;
        int scfsi = gr_info->scfsi;

        if(scfsi < 0) { /* scfsi < 0 => granule == 0 */
            for(i=11; i; i--)
                *scf++ = getbits_fast(num0);
            for(i=10; i; i--)
                *scf++ = getbits_fast(num1);
            numbits = (num0 + num1) * 10 + num0;
            *scf++ = 0;
        } else {
            numbits = 0;
            if(!(scfsi & 0x8)) {
                for (i=0; i<6; i++)
                    *scf++ = getbits_fast(num0);
                numbits += num0 * 6;
            } else {
                scf += 6;
            }

            if(!(scfsi & 0x4)) {
                for (i=0; i<5; i++)
                    *scf++ = getbits_fast(num0);
                numbits += num0 * 5;
            } else {
                scf += 5;
            }

            if(!(scfsi & 0x2)) {
                for (i=0; i<5; i++)
                    *scf++ = getbits_fast(num1);
                numbits += num1 * 5;
            } else {
                scf += 5;
            }

            if(!(scfsi & 0x1)) {
                for (i=0; i<5; i++)
                    *scf++ = getbits_fast(num1);
                numbits += num1 * 5;
            } else {
                scf += 5;
            }
			*scf++ = 0;  /* no l[21] in original sources */
        }
    }
    return numbits;
}

static int III_get_scale_factors_2(int *scf,struct gr_info_s *gr_info,int i_stereo)
{
    unsigned char *pnt;
    int i,j;
    unsigned int slen;
    int n = 0;
    int numbits = 0;

    static unsigned char stab[3][6][4] = {
        {   { 6, 5, 5,5 }, { 6, 5, 7,3 }, { 11,10,0,0},
            { 7, 7, 7,0 }, { 6, 6, 6,3 }, {  8, 8,5,0}
        },
        {   { 9, 9, 9,9 }, { 9, 9,12,6 }, { 18,18,0,0},
            {12,12,12,0 }, {12, 9, 9,6 }, { 15,12,9,0}
        },
        {   { 6, 9, 9,9 }, { 6, 9,12,6 }, { 15,18,0,0},
            { 6,15,12,0 }, { 6,12, 9,6 }, {  6,18,9,0}
        }};

    if(i_stereo) /* i_stereo AND second channel -> do_layer3() checks this */
        slen = i_slen2[gr_info->scalefac_compress>>1];
    else
        slen = n_slen2[gr_info->scalefac_compress];

    gr_info->preflag = (slen>>15) & 0x1;

    n = 0;
    if( gr_info->block_type == 2 ) {
        n++;
        if(gr_info->mixed_block_flag)
            n++;
    }

    pnt = stab[n][(slen>>12)&0x7];

    for(i=0; i<4; i++) {
        int num = slen & 0x7;
        slen >>= 3;
        if(num) {
            for(j=0; j<(int)(pnt[i]); j++)
                *scf++ = getbits_fast(num);
            numbits += pnt[i] * num;
        } else {
            for(j=0; j<(int)(pnt[i]); j++)
                *scf++ = 0;
        }
    }

    n = (n << 1) + 1;
    for(i=0; i<n; i++)
        *scf++ = 0;

    return numbits;
}

static int pretab1[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0};
static int pretab2[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/*
 * don't forget to apply the same changes to III_dequantize_sample_ms() !!!
 */
static int III_dequantize_sample(real xr[SBLIMIT][SSLIMIT], int *scf,
                                 struct gr_info_s * gr_info, int sfreq, int part2bits, real * Energy)
{
    int shift = 1 + gr_info->scalefac_scale;
    real *xrpnt = (real *) xr;
    real energy = 0.0;
    int l[3], l3;
    int part2remain = gr_info->part2_3_length - part2bits;
    int *me;

    {
        int bv = gr_info->big_values;
        int region1 = gr_info->region1start;
        int region2 = gr_info->region2start;

        l3 = ((576 >> 1) - bv) >> 1;
        /*
         * we may lose the 'odd' bit here !!
         * check this later again
         */
        if (bv <= region1) {
            l[0] = bv;
            l[1] = 0;
            l[2] = 0;
        } else {
            l[0] = region1;
            if (bv <= region2) {
                l[1] = bv - l[0];
                l[2] = 0;
            } else {
                l[1] = region2 - l[0];
                l[2] = bv - region2;
            }
        }
    }

    if (gr_info->block_type == 2) {
        /* decoding with short or mixed mode BandIndex table */
        int i, max[4];
        int step = 0, lwin = 0, cb = 0;
        register real v = 0.0;
        register int *m, mc;

        if (gr_info->mixed_block_flag) {
            max[3] = -1;
            max[0] = max[1] = max[2] = 2;
            m = map[sfreq][0];
            me = mapend[sfreq][0];
        } else {
            max[0] = max[1] = max[2] = max[3] = -1;
            /* max[3] not really needed in this case */
            m = map[sfreq][1];
            me = mapend[sfreq][1];
        }

        mc = 0;
        for (i = 0; i < 2; i++) {
            int lp = l[i];
            struct newhuff *h = ht + gr_info->table_select[i];

            for (; lp; lp--, mc--) {
                register int x, y;

                if ((!mc)) {
                    mc = *m++;
                    xrpnt = ((real *) xr) + (*m++);
                    lwin = *m++;
                    cb = *m++;
                    if (lwin == 3) {
                        v = gr_info->pow2gain[(*scf++) << shift];
                        step = 1;
                    } else {
                        v = gr_info->full_gain[lwin][(*scf++) << shift];
                        step = 3;
                    }
                } {
                    register short *val = h->table;

                    while ((y = *val++) < 0) {
                        if (get1bit())
                            val -= y;
                        part2remain--;
                    }
                    x = y >> 4;
                    y &= 0xf;
                }
                if (x == 15) {
                    max[lwin] = cb;
                    part2remain -= h->linbits + 1;
                    x += getbits(h->linbits);
                    if (get1bit())
                        *xrpnt = -ispow[x] * v;
                    else
                        *xrpnt = ispow[x] * v;
                    energy += *xrpnt * *xrpnt;
                } else if (x) {
                    max[lwin] = cb;
                    if (get1bit())
                        *xrpnt = -ispow[x] * v;
                    else
                        *xrpnt = ispow[x] * v;
                    energy += *xrpnt * *xrpnt;
                    part2remain--;
                } else
                    *xrpnt = 0.0;
                xrpnt += step;
                if (y == 15) {
                    max[lwin] = cb;
                    part2remain -= h->linbits + 1;
                    y += getbits(h->linbits);
                    if (get1bit())
                        *xrpnt = -ispow[y] * v;
                    else
                        *xrpnt = ispow[y] * v;
                    energy += *xrpnt * *xrpnt;
                } else if (y) {
                    max[lwin] = cb;
                    if (get1bit())
                        *xrpnt = -ispow[y] * v;
                    else
                        *xrpnt = ispow[y] * v;
                    energy += *xrpnt * *xrpnt;
                    part2remain--;
                } else
                    *xrpnt = 0.0;
                xrpnt += step;
            }
        }
        for (; l3 && (part2remain > 0); l3--) {
            struct newhuff *h = htc + gr_info->count1table_select;
            register short *val = h->table, a;

            while ((a = *val++) < 0) {
                part2remain--;
                if (part2remain < 0) {
                    part2remain++;
                    a = 0;
                    break;
                }
                if (get1bit())
                    val -= a;
            }

            for (i = 0; i < 4; i++) {
                if (!(i & 1)) {
                    if (!mc) {
                        mc = *m++;
                        xrpnt = ((real *) xr) + (*m++);
                        lwin = *m++;
                        cb = *m++;
                        if (lwin == 3) {
                            v = gr_info->pow2gain[(*scf++) << shift];
                            step = 1;
                        } else {
                            v = gr_info->full_gain[lwin][(*scf++) << shift];
                            step = 3;
                        }
                    }
                    mc--;
                }
                if ((a & (0x8 >> i))) {
                    max[lwin] = cb;
                    part2remain--;
                    if (part2remain < 0) {
                        part2remain++;
                        break;
                    }
                    if (get1bit())
                        *xrpnt = -v;
                    else
                        *xrpnt = v;
                    energy += *xrpnt * *xrpnt;
                } else
                    *xrpnt = 0.0;
                xrpnt += step;
            }
        }

        while (m < me) {
            if (!mc) {
                mc = *m++;
                xrpnt = ((real *) xr) + *m++;
                if ((*m++) == 3)
                    step = 1;
                else
                    step = 3;
                m++;			/* cb */
            }
            mc--;
            *xrpnt = 0.0;
            xrpnt += step;
            *xrpnt = 0.0;
            xrpnt += step;
            /* we could add a little opt. here:
             * if we finished a band for window 3 or a long band
             * further bands could copied in a simple loop without a
             * special 'map' decoding
             */
        }

        gr_info->maxband[0] = max[0] + 1;
        gr_info->maxband[1] = max[1] + 1;
        gr_info->maxband[2] = max[2] + 1;
        gr_info->maxbandl = max[3] + 1;

        {
            int rmax = max[0] > max[1] ? max[0] : max[1];

            rmax = (rmax > max[2] ? rmax : max[2]) + 1;
            gr_info->maxb = rmax ? shortLimit[sfreq][rmax] : longLimit[sfreq][max[3] + 1];
        }

    } else {
        /* decoding with 'long' BandIndex table (block_type != 2) */
        int *pretab = gr_info->preflag ? pretab1 : pretab2;
        int i, max = -1;
        int cb = 0;
        register int *m = map[sfreq][2];
        register real v = 0.0;
        register int mc = 0;

        /* long hash table values */
        for (i = 0; i < 3; i++) {
            int lp = l[i];
            struct newhuff *h = ht + gr_info->table_select[i];

            for (; lp; lp--, mc--) {
                int x, y;

                if (!mc) {
                    mc = *m++;
                    v = gr_info->pow2gain[((*scf++) + (*pretab++)) << shift];
                    cb = *m++;
                } {
                    register short *val = h->table;

                    while ((y = *val++) < 0) {
                        if (get1bit())
                            val -= y;
                        part2remain--;
                    }
                    x = y >> 4;
                    y &= 0xf;
                }
                if (x == 15) {
                    max = cb;
                    part2remain -= h->linbits + 1;
                    x += getbits(h->linbits);
                    if (get1bit())
                        *xrpnt = -ispow[x] * v;
                    else
                        *xrpnt = ispow[x] * v;
                    energy += *xrpnt * *xrpnt;
                    xrpnt++;
                } else if (x) {
                    max = cb;
                    if (get1bit())
                        *xrpnt = -ispow[x] * v;
                    else
                        *xrpnt = ispow[x] * v;
                    energy += *xrpnt * *xrpnt;
                    xrpnt++;
                    part2remain--;
                } else
                    *xrpnt++ = 0.0;

                if (y == 15) {
                    max = cb;
                    part2remain -= h->linbits + 1;
                    y += getbits(h->linbits);
                    if (get1bit())
                        *xrpnt = -ispow[y] * v;
                    else
                        *xrpnt = ispow[y] * v;
                    energy += *xrpnt * *xrpnt;
                    xrpnt++;
                } else if (y) {
                    max = cb;
                    if (get1bit())
                        *xrpnt = -ispow[y] * v;
                    else
                        *xrpnt = ispow[y] * v;
                    energy += *xrpnt * *xrpnt;
                    xrpnt++;
                    part2remain--;
                } else
                    *xrpnt++ = 0.0;
            }
        }

        /* short (count1table) values */
        for (; l3 && (part2remain > 0); l3--) {
            struct newhuff *h = htc + gr_info->count1table_select;
            register short *val = h->table, a;

            while ((a = *val++) < 0) {
                part2remain--;
                if (part2remain < 0) {
                    part2remain++;
                    a = 0;
                    break;
                }
                if (get1bit())
                    val -= a;
            }

            for (i = 0; i < 4; i++) {
                if (!(i & 1)) {
                    if (!mc) {
                        mc = *m++;
                        cb = *m++;
                        v = gr_info->pow2gain[((*scf++) + (*pretab++)) << shift];
                    }
                    mc--;
                }
                if ((a & (0x8 >> i))) {
                    max = cb;
                    part2remain--;
                    if (part2remain < 0) {
                        part2remain++;
                        break;
                    }
                    if (get1bit())
                        *xrpnt = -v;
                    else
                        *xrpnt = v;
                    energy += *xrpnt * *xrpnt;
                    xrpnt++;
                } else
                    *xrpnt++ = 0.0;
            }
        }

        /* zero part */
        for (i = (int)((real*)xr + SBLIMIT*SSLIMIT - xrpnt) >> 1; i; i--) {
            *xrpnt++ = 0.0;
            *xrpnt++ = 0.0;
        }

        gr_info->maxbandl = max + 1;
        gr_info->maxb = longLimit[sfreq][gr_info->maxbandl];
    }

    while (part2remain > 16) {
        getbits(16);		/* Dismiss stuffing Bits */
        part2remain -= 16;
    }
    if (part2remain > 0)
        getbits(part2remain);
    else if (part2remain < 0) {
        //fprintf(stderr, "l.1032: mpg123: Can't rewind stream by %d bits!\n", -part2remain);
        return 1;			/* -> error */
    }
    energy *= 30000.0;
    *Energy += energy;
    return 0;
}


#ifdef MPGLIB02a

// TODO do_layer1 and do_layer2 in mpglib but not here

static void III_i_stereo(real xr_buf[2][SBLIMIT][SSLIMIT],int *scalefac,
                         struct gr_info_s *gr_info,int sfreq,int ms_stereo,int lsf)
{
    real (*xr)[SBLIMIT*SSLIMIT] = (real (*)[SBLIMIT*SSLIMIT] ) xr_buf;
    struct bandInfoStruct *bi = &bandInfo[sfreq];
    real *tab1,*tab2;

    if(lsf) {
        int p = gr_info->scalefac_compress & 0x1;
        if(ms_stereo) {
            tab1 = pow1_2[p];
            tab2 = pow2_2[p];
        } else {
            tab1 = pow1_1[p];
            tab2 = pow2_1[p];
        }
    } else {
        if(ms_stereo) {
            tab1 = tan1_2;
            tab2 = tan2_2;
        } else {
            tab1 = tan1_1;
            tab2 = tan2_1;
        }
    }

    if (gr_info->block_type == 2) {
        int lwin,do_l = 0;
        if( gr_info->mixed_block_flag )
            do_l = 1;

        for (lwin=0; lwin<3; lwin++) { /* process each window */
            /* get first band with zero values */
            int is_p,sb,idx,sfb = gr_info->maxband[lwin];  /* sfb is minimal 3 for mixed mode */
            if(sfb > 3)
                do_l = 0;

            for(; sfb<12; sfb++) {
                is_p = scalefac[sfb*3+lwin-gr_info->mixed_block_flag]; /* scale: 0-15 */
                if(is_p != 7) {
                    real t1,t2;
                    sb = bi->shortDiff[sfb];
                    idx = bi->shortIdx[sfb] + lwin;
                    t1 = tab1[is_p];
                    t2 = tab2[is_p];
                    for (; sb > 0; sb--,idx+=3) {
                        real v = xr[0][idx];
                        xr[0][idx] = v * t1;
                        xr[1][idx] = v * t2;
                    }
                }
            }

#if 1
            /* in the original: copy 10 to 11 , here: copy 11 to 12
            maybe still wrong??? (copy 12 to 13?) */
            is_p = scalefac[11*3+lwin-gr_info->mixed_block_flag]; /* scale: 0-15 */
            sb = bi->shortDiff[12];
            idx = bi->shortIdx[12] + lwin;
#else
            is_p = scalefac[10*3+lwin-gr_info->mixed_block_flag]; /* scale: 0-15 */
            sb = bi->shortDiff[11];
            idx = bi->shortIdx[11] + lwin;
#endif
            if(is_p != 7) {
                real t1,t2;
                t1 = tab1[is_p];
                t2 = tab2[is_p];
                for ( ; sb > 0; sb--,idx+=3 ) {
                    real v = xr[0][idx];
                    xr[0][idx] = v * t1;
                    xr[1][idx] = v * t2;
                }
            }
        } /* end for(lwin; .. ; . ) */

        if (do_l) {
            /* also check l-part, if ALL bands in the three windows are 'empty'
             * and mode = mixed_mode
             */
            int sfb = gr_info->maxbandl;
            int idx = bi->longIdx[sfb];

            for ( ; sfb<8; sfb++ ) {
                int sb = bi->longDiff[sfb];
                int is_p = scalefac[sfb]; /* scale: 0-15 */
                if(is_p != 7) {
                    real t1,t2;
                    t1 = tab1[is_p];
                    t2 = tab2[is_p];
                    for ( ; sb > 0; sb--,idx++) {
                        real v = xr[0][idx];
                        xr[0][idx] = v * t1;
                        xr[1][idx] = v * t2;
                    }
                } else
                    idx += sb;
            }
        }
    } else { /* ((gr_info->block_type != 2)) */
        int sfb = gr_info->maxbandl;
        int is_p,idx = bi->longIdx[sfb];
        for ( ; sfb<21; sfb++) {
            int sb = bi->longDiff[sfb];
            is_p = scalefac[sfb]; /* scale: 0-15 */
            if(is_p != 7) {
                real t1,t2;
                t1 = tab1[is_p];
                t2 = tab2[is_p];
                for ( ; sb > 0; sb--,idx++) {
                    real v = xr[0][idx];
                    xr[0][idx] = v * t1;
                    xr[1][idx] = v * t2;
                }
            } else
                idx += sb;
        }

        is_p = scalefac[20]; /* copy l-band 20 to l-band 21 */
        if(is_p != 7) {
            int sb;
            real t1 = tab1[is_p],t2 = tab2[is_p];

            for ( sb = bi->longDiff[21]; sb > 0; sb--,idx++ ) {
                real v = xr[0][idx];
                xr[0][idx] = v * t1;
                xr[1][idx] = v * t2;
            }
        }
    } /* ... */
}
#endif


/*
 * main layer3 handler
 */
int do_layer3(struct frame * fr)
{
    int gr, clip = 0;
    int scalefacs[2][39];	/* max 39 for short[13][3] mode, mixed: 38, long: 22 */
    struct III_sideinfo sideinfo;
    int stereo = fr->stereo;
    int single = fr->single;
    int ms_stereo, i_stereo;
    int sfreq = fr->sampling_frequency;
    int granules;
    real energy = 0.0;

    /*
      int stereo1;
      if (stereo == 1) {		/ * stream is mono * /
        stereo1 = 1;
        single = 0;
      } else if (single >= 0)	/ * stream is stereo, but force to mono * /
        stereo1 = 1;
      else
        stereo1 = 2;
    */

    if (fr->mode == MPG_MD_JOINT_STEREO) {
        ms_stereo = fr->mode_ext & 0x2;
        i_stereo = fr->mode_ext & 0x1;
    } else
        ms_stereo = i_stereo = 0;

    if (fr->lsf) {
        granules = 1;
		if(!III_get_side_info_2(&sideinfo, stereo, ms_stereo, sfreq, single))
			return -1;
    } else {
        granules = 2;
        if(!III_get_side_info_1(&sideinfo, stereo, ms_stereo, sfreq, single))
			return -1;
    }

    if (set_pointer(sideinfo.main_data_begin) == MP3_ERR)
        return -1;

    for (gr = 0; gr < granules; gr++) {
        real hybridIn[2][SBLIMIT][SSLIMIT];

        {
            struct gr_info_s *gr_info = &(sideinfo.ch[0].gr[gr]);
            long part2bits;

            if (fr->lsf)
                part2bits = III_get_scale_factors_2(scalefacs[0], gr_info, 0);
            else {
                part2bits = III_get_scale_factors_1(scalefacs[0], gr_info);
            }
            if (III_dequantize_sample(hybridIn[0], scalefacs[0], gr_info, sfreq,
                                      part2bits, &energy)) {
                fr->energy = energy;
                return clip;
            }
        }
        if (stereo == 2) {
            struct gr_info_s *gr_info = &(sideinfo.ch[1].gr[gr]);
            long part2bits;

            if (fr->lsf)
                part2bits = III_get_scale_factors_2(scalefacs[1], gr_info, i_stereo);
            else {
                part2bits = III_get_scale_factors_1(scalefacs[1], gr_info);
            }

            if (III_dequantize_sample(hybridIn[1], scalefacs[1], gr_info, sfreq,
                                      part2bits, &energy)) {
                fr->energy = energy;
                return clip;
            }

#ifdef MPGLIB02a 	// this is in mplib but was missing from here. TODO no energy calculation
            if(ms_stereo) {
                int i;
                for(i=0; i<SBLIMIT*SSLIMIT; i++) {
                    real tmp0,tmp1;
                    tmp0 = ((real *) hybridIn[0])[i];
                    tmp1 = ((real *) hybridIn[1])[i];
                    ((real *) hybridIn[0])[i] = tmp0 + tmp1;
                    ((real *) hybridIn[1])[i] = tmp0 - tmp1;
                }
            }

            if(i_stereo)
                III_i_stereo(hybridIn,scalefacs[1],gr_info,sfreq,ms_stereo,fr->lsf);

            if(ms_stereo || i_stereo || (single == 3) ) {
                if(gr_info->maxb > sideinfo.ch[0].gr[gr].maxb)
                    sideinfo.ch[0].gr[gr].maxb = gr_info->maxb;
                else
                    gr_info->maxb = sideinfo.ch[0].gr[gr].maxb;
            }

            switch(single) {

            case 3:
                if(1) {
                register int i;
                register real *in0 = (real *) hybridIn[0],*in1 = (real *) hybridIn[1];
                for(i=0; i<SSLIMIT*gr_info->maxb; i++,in0++)
                    *in0 = (*in0 + *in1++); /* *0.5 done by pow-scale */
			break; }

            case 1:
                register int i;
                register real *in0 = (real *) hybridIn[0],*in1 = (real *) hybridIn[1];
                for(i=0; i<SSLIMIT*gr_info->maxb; i++)
                    *in0++ = *in1++;
				break;
            }
#endif
        }
    }
    fr->energy = energy;
    return clip;
}



/************************************************************************
 * 			Code from mpglib interface.c			*
 ************************************************************************/


static int frameNum = 0;
static int nxtframe= 0;/* # of next frame so far read */
static int maxframe;	/* # frames in the file, 1 more than highest frame # */
static short *framepower;	/* array of frame energy levels */
static long *frameoffset;	/* array of frame offsets */


BOOL InitMP3(void)
{
    static int init = 0;

	DBGPRINT(7, "\nInitMP3");
//  DBGPRINT(7, "\ngmp=%u",gmp);

// struct mpstr *gmp=NULL; /* from line 172 */
    if (gmp==NULL) {
        gmp=(struct mpstr *)malloc(sizeof(struct mpstr));
        DBGPRINT(7, "\nafter gmp=(struct...");
    }
    memset(gmp, 0, sizeof(struct mpstr));
    DBGPRINT(7, "\nafter memset(gmp, 0,...");

    gmp->framesize = 0;
    gmp->fsizeold = -1;
    gmp->bsize = 0;
    gmp->head = gmp->tail = NULL;
    gmp->fr.single = -1;
    gmp->bsnum = 0;
    gmp->synth_bo = 1;

	if(!init) {
        init = 1;
		DBGPRINT(7, "\nbefore init_layer3\n");
#if LAYER2
        init_layer2();
#endif
		init_layer3(SBLIMIT);
        DBGPRINT(7, "\nafter init_layer3\n");
	}

    return !0;
}

void ExitMP3(void)
{
    struct buf *b, *bn;

    b = gmp->tail;
    DBGPRINT(6, "before while()\n");
    while (b) {
        free(b->pnt);
        bn = b->next;
        free(b);
        b = bn;
    }
    DBGPRINT(6, "after while()\n");
    free(framepower);
    DBGPRINT(6, "after free(framepower)\n");
    free(frameoffset);
    DBGPRINT(6, "after free(frameoffset)\n");
    free(gmp);
    DBGPRINT(6, "after free(gmp)\n");
    gmp=NULL;
    framepower=NULL;
    frameoffset=NULL;
    nxtframe=0;
    frameNum=0;
    DBGPRINT(6, "after frameNum=0\n");
}

static struct buf *addbuf(struct mpstr * mp, char *buf, int size)
{
    struct buf *nbuf;

    nbuf = malloc(sizeof(struct buf)*16); // JP: added *16
    if (!nbuf) {
        fprintf(stderr, "   l.1189: Out of memory!\n");
        return NULL;
    }
    nbuf->pnt = malloc(size);
    if (!nbuf->pnt) {
        free(nbuf);
        return NULL;
    }
    nbuf->size = size;
    memcpy(nbuf->pnt, buf, size);
    nbuf->next = NULL;
    nbuf->prev = mp->head;
    nbuf->pos = 0;

    if (!mp->tail) {
        mp->tail = nbuf;
    } else {
        mp->head->next = nbuf;
    }

    mp->head = nbuf;
    mp->bsize += size;

    return nbuf;
}

static void remove_buf(struct mpstr * mp)
{
    struct buf *buf = mp->tail;

    mp->tail = buf->next;
    if (mp->tail)
        mp->tail->prev = NULL;
    else {
        mp->tail = mp->head = NULL;
    }

    free(buf->pnt);
    free(buf);

}

static int read_buf_byte(struct mpstr * mp)
{
    unsigned int b;

    int pos;

    if (!mp || !mp->tail) {
        fprintf(stderr, "   l.1245: Fatal error!\n");
        exit(1);
    }
    DBGPRINT(5, "before pos = mp->tail->pos\n");
    pos = mp->tail->pos;
    DBGPRINT(5, "before while (pos >= mp->tail->size)\n");
    while (pos >= mp->tail->size) {
        remove_buf(mp);
        if (!mp->tail) {
            fprintf(stderr, "   l.1245: Fatal error!\n");
            exit(1);
        }
        pos = mp->tail->pos;
        DBGPRINT(5, "before if (!mp->tail)\n");
    }

    DBGPRINT(5, "before b = mp->tail->pnt[pos]\n");
    DBGPRINT(5, "pos=%i\n",pos);
    b = mp->tail->pnt[pos];
    DBGPRINT(5, "before mp->bsize--\n");
    mp->bsize--;
    DBGPRINT(5, "before mp->tail->pos++\n");
    mp->tail->pos++;


    DBGPRINT(5, "before return b\n");
    return b;
}

static long Hoffset=0;			/* Offset of current frame */
static unsigned short maxframepower;	/* Max frame power ever found */

static void read_head(struct mpstr * mp)
{
    unsigned long head=0;

    DBGPRINT(4, "before head = read_buf_byte(mp)\n");
    head = read_buf_byte(mp);
    head <<= 8;
    DBGPRINT(4, "before head |= read_buf_byte(mp)\n");
    head |= read_buf_byte(mp);
    head <<= 8;
    DBGPRINT(4, "before head |= read_buf_byte(mp)\n");
    head |= read_buf_byte(mp);
    head <<= 8;
    DBGPRINT(4, "before head |= read_buf_byte(mp)\n");
    head |= read_buf_byte(mp);

    mp->header = head;
    mp->fr.offset=Hoffset;
    Hoffset+=4;
}

int decodeMP3(char *in, int isize)
{
    int len;

    if (in) {
        if (addbuf(gmp, in, isize) == NULL) {
            return MP3_ERR;
        }
    }

    DBGPRINT(3, "before first decode header\n");
    /* First decode header */
    if (gmp->framesize == 0) {
        if (gmp->bsize < 4) {
            return MP3_NEED_MORE;
        }
        DBGPRINT(3, "before read_head(gmp)\n");
        read_head(gmp);
        DBGPRINT(3, "before decode_header(&gmp->fr, gmp->header)\n");
        decode_header(&gmp->fr, gmp->header);
//      decode_header(&gmp->fr, 0);
        DBGPRINT(3, "before gmp->framesize = gmp->fr.framesize\n");
        gmp->framesize = gmp->fr.framesize;
        DBGPRINT(3, "before Hoffset+=gmp->fr.framesize\n");
        Hoffset+=gmp->fr.framesize;
    }
    if (gmp->fr.framesize > gmp->bsize)
        return MP3_NEED_MORE;
    DBGPRINT(3, "after return MP3_NEED_MORE\n");

    wordpointer = gmp->bsspace[gmp->bsnum] + 512;
    gmp->bsnum = (gmp->bsnum + 1) & 0x1;
    bitindex = 0;

    DBGPRINT(3, "before len=0\n");
    len = 0;
    while (len < gmp->framesize) {
        int nlen;
        int blen = gmp->tail->size - gmp->tail->pos;

        if ((gmp->framesize - len) <= blen) {
            nlen = gmp->framesize - len;
        } else {
            nlen = blen;
        }
        memcpy(wordpointer + len, gmp->tail->pnt + gmp->tail->pos, nlen);
        len += nlen;
        gmp->tail->pos += nlen;
        gmp->bsize -= nlen;
        if (gmp->tail->pos == gmp->tail->size) {
            remove_buf(gmp);
        }
    }

    DBGPRINT(3, "before gmp->fr.error_protection\n");
    if (gmp->fr.error_protection)
        getbits(16);
    switch(gmp->fr.lay) {
    case 1:
#if LAYER1
		do_layer1(&gmp->fr);
        break;
#endif
    case 2:
#if LAYER2
		do_layer2(&gmp->fr);
#endif
        break;
    case 3:
		do_layer3(&gmp->fr);
        break;
    }

    gmp->fsizeold = gmp->framesize;
    gmp->framesize = 0;

    frameNum++;
    return MP3_OK;
}

int set_pointer(long backstep)
{
    unsigned char *bsbufold;

    if (gmp->fsizeold < 0 && backstep > 0) {
        //fprintf(stderr, "l.1357: Can't step back %ld!\n", backstep);
        return MP3_ERR;
    }
    bsbufold = gmp->bsspace[gmp->bsnum] + 512;
    wordpointer -= backstep;
    if (backstep)
        memcpy(wordpointer, bsbufold + gmp->fsizeold - backstep, backstep);
    bitindex = 0;
    return MP3_OK;
}

/* Get the energy level in a given frame, or
 * MP3_ERR if we can't get it
 */
unsigned int get_framelevel(FILE *zin, int framenum)
{
#define BSIZE 32768
    char buf[BSIZE];
    unsigned long len=0;
    int ret;
    long posn, filelen;

    if (gmp==NULL) InitMP3();
//  DBGPRINT(2, "gmp = %u\n",gmp);

    DBGPRINT(2, "framenum = %i\n",framenum);
    if (framenum<0) return(MP3_ERR);
//  DBGPRINT(2, "framepower = %u\n",framepower);
    if ((framepower!=NULL) && (framenum>=maxframe)) return(MP3_ERR);
//printf("nxtframe = %u\n",nxtframe);
//printf(" maxframe = %u\n",maxframe);
    if ((framepower!=NULL) && (framenum<nxtframe))
        return((int)framepower[framenum]);

    DBGPRINT(2, " nicht return \n");

    while(1) {
        if (feof(zin)) return(MP3_ERR);
        len = fread(buf,sizeof(char), BSIZE, zin);

        DBGPRINT(2, "len = %lu\n",len);

//  DBGPRINT(2, "framepower = %u\n",framepower);
        if (len <= 0) break;
//  DBGPRINT(2, "framepower = %u\n",framepower);
        if (framepower==NULL) {
            Hoffset=0;
            maxframepower=0;
        }
        DBGPRINT(2, "before ret\n");
        ret = decodeMP3(buf,len);
        DBGPRINT(2, "after ret\n");

        DBGPRINT(2, "ret = %d\n",ret);

        while (ret == MP3_OK) {
            DBGPRINT(2, "ret = MP3_OK\n");
            /* Before saving the value, create the array if
             * it doesn't exist yet
             */
            if (framepower==NULL) {
                DBGPRINT(2, "framepower==NULL\n");
                posn=ftell(zin);
                fseek(zin, 0, SEEK_END);
                filelen=ftell(zin);
                fseek(zin, posn, SEEK_SET);
                maxframe= filelen/(gmp->fr.framesize+4);
                /* We make frameoffset one element bigger so that we
                 * can save the file's length as the offset of the
                 * fictitious `last frame +1'. It makes writing out
                 * selections of frames easier.
                 */
                framepower=(short *)malloc((maxframe) * sizeof(short)*16); /* JP: added *16 */
                frameoffset=(long *)malloc((1+maxframe) * sizeof(long)*16); /* JP: added *16 */
                frameoffset[maxframe]= filelen;
            }

            if (gmp->fr.energy>65535) {
                gmp->fr.energy=65535;
            }
            framepower[nxtframe]= gmp->fr.energy;
            frameoffset[nxtframe++]= gmp->fr.offset;
            if (gmp->fr.energy>maxframepower) {
                maxframepower=gmp->fr.energy;
            }
            ret = decodeMP3(NULL,0);
        }
        if (framenum<nxtframe) return((int)framepower[framenum]);
    }
    return(MP3_ERR);
}

/* Get the offset of a given frame, or
 * MP3_ERR if we can't get it
 */
long get_frameoffset(FILE *zin, int framenum)
{
    int level;

    if (framenum==maxframe) return(frameoffset[maxframe]);	/* One off case */

    level=get_framelevel(zin,framenum);
    if (level==MP3_ERR) return(MP3_ERR);
    return(frameoffset[framenum]);
}

/* Get the biggest frame power level
 * so far encountered, or return
 * MP3_ERR if we can't get it
 */
int get_maxframepower(FILE *zin)
{
    int level;

    level=get_framelevel(zin,1);		/* Make sure we have at least 1 frame */
    if (level==MP3_ERR) return(MP3_ERR);
    return( (int)maxframepower);
}

/* Get the number of frames in the file
 * or return MP3_ERR if we can't get it
 */
long get_maxframe(FILE *zin)
{
    int level;

    level=get_framelevel(zin,1);		/* Make sure we have at least 1 frame */
    if (level==MP3_ERR) return(MP3_ERR);
    return(maxframe);
}
