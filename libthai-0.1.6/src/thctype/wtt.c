/*
 * $Id: wtt.c,v 1.4 2006-08-01 11:38:21 thep Exp $
 * wtt.h - WTT I/O implementation
 * Created: 2001-08-04
 * Author:  Theppitak Karoonboonyanan <thep@links.nectec.or.th>
 */

#include <thai/wtt.h>

short TACchtype_[256] = {
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /*  0 -  7 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /*  8 - 15 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 16 - 23 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 24 - 31 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 32 - 39 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 40 - 47 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 48 - 55 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 56 - 63 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 64 - 71 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 72 - 79 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 80 - 87 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 88 - 95 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 96 - 103 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 104 - 111 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 112 - 119 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  CTRL,  /* 120 - 127 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 128 - 135 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 136 - 143 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 144 - 151 */
    CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL, CTRL,  /* 152 - 159 */
    NON,  CONS, CONS, CONS, CONS, CONS, CONS, CONS,  /* 160 - 167 */
    CONS, CONS, CONS, CONS, CONS, CONS, CONS, CONS,  /* 168 - 175 */
    CONS, CONS, CONS, CONS, CONS, CONS, CONS, CONS,  /* 176 - 183 */
    CONS, CONS, CONS, CONS, CONS, CONS, CONS, CONS,  /* 184 - 191 */
    CONS, CONS, CONS, CONS,  FV3, CONS,  FV3, CONS,  /* 192 - 199 */
    CONS, CONS, CONS, CONS, CONS, CONS, CONS, NON,   /* 200 - 207 */
    FV1,  AV2,  FV1,  FV1,  AV1,  AV3,  AV2,  AV3,   /* 208 - 215 */
    BV1,  BV2,  BD,   NON,  NON,  NON,  NON,  NON,   /* 216 - 223 */
    LV,   LV,   LV,   LV,   LV,   FV2,  NON,  AD2,   /* 224 - 231 */
    TONE, TONE, TONE, TONE, AD1,  AD1,  AD3,  NON,   /* 232 - 239 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  NON,   /* 240 - 247 */
    NON,  NON,  NON,  NON,  NON,  NON,  NON,  CTRL   /* 248 - 255 */
};

/* Table for Thai Cell Manipulation */
short TACio_op_[17][17] = {
      /* Table 2: WTT I/O sequence check rules */
      /* row: leading char,  column: following char */
/* CTRL NON CONS LV FV1 FV2 FV3 BV1 BV2 BD TONE AD1 AD2 AD3 AV1 AV2 AV3 */
   {XC, AC, AC, AC, AC, AC, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*CTRL*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*NON*/
  ,{XC, AC, AC, AC, AC, SR, AC, CP, CP, CP, CP, CP, CP, CP, CP, CP, CP}/*CONS*/
  ,{XC, SR, AC, SR, SR, SR, SR, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*LV*/
  ,{XC, AC, AC, AC, AC, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*FV1*/
  ,{XC, AC, AC, AC, AC, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*FV2*/
  ,{XC, AC, AC, AC, SR, AC, SR, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*FV3*/
  ,{XC, AC, AC, AC, AC, SR, AC, RJ, RJ, RJ, CP, CP, RJ, RJ, RJ, RJ, RJ}/*BV1*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, CP, RJ, RJ, RJ, RJ, RJ, RJ}/*BV2*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*BD*/
  ,{XC, AC, AC, AC, AC, AC, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*TONE*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*AD1*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*AD2*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ, RJ}/*AD3*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, CP, CP, RJ, RJ, RJ, RJ, RJ}/*AV1*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, CP, RJ, RJ, RJ, RJ, RJ, RJ}/*AV2*/
  ,{XC, AC, AC, AC, SR, SR, AC, RJ, RJ, RJ, CP, RJ, CP, RJ, RJ, RJ, RJ}/*AV3*/
};

#undef TACchtype
#undef TACio_op

WTTClass TACchtype(thchar_t c)
{
    return TACchtype_[c];
}

WTTOp    TACio_op(thchar_t c1, thchar_t c2)
{
    return TACio_op_[TACchtype_[c1]][TACchtype_[c2]];
}

