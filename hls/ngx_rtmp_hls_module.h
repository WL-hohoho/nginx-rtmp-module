
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_cmd_module.h>
#include <ngx_rtmp_codec_module.h>
#include "ngx_rtmp_mpegts.h"

typedef struct {
    uint64_t                            id;
    double                              duration;
    unsigned                            active:1;
    unsigned                            discont:1; /* before */
    ngx_uint_t                          hits;
} ngx_rtmp_hls_frag_t;


typedef struct {
    ngx_str_t                           suffix;
    ngx_array_t                         args;
} ngx_rtmp_hls_variant_t;


typedef struct {
    unsigned                            opened:1;

    ngx_file_t                          file;

    ngx_str_t                           playlist;
    ngx_str_t                           playlist_bak;
    ngx_str_t                           var_playlist;
    ngx_str_t                           var_playlist_bak;
    ngx_str_t                           stream;
    ngx_str_t                           name;

    uint64_t                            frag;
    uint64_t                            frag_ts;
    ngx_uint_t                          nfrags;
    ngx_rtmp_hls_frag_t                *frags; /* circular 2 * winfrags + 1 */

    ngx_uint_t                          audio_cc;
    ngx_uint_t                          video_cc;

    uint64_t                            aframe_base;
    uint64_t                            aframe_num;

    ngx_buf_t                          *aframe;
    uint64_t                            aframe_pts;

    ngx_rtmp_hls_variant_t             *var;

    ngx_uint_t                          viewers;    /* updated on each new fragment */
} ngx_rtmp_hls_ctx_t;


typedef struct {
    ngx_str_t                           path;
    ngx_msec_t                          playlen;
} ngx_rtmp_hls_cleanup_t;


typedef struct {
    ngx_flag_t                          hls;
    ngx_msec_t                          fraglen;
    ngx_msec_t                          max_fraglen;
    ngx_msec_t                          muxdelay;
    ngx_msec_t                          sync;
    ngx_msec_t                          playlen;
    ngx_uint_t                          winfrags;
    ngx_flag_t                          continuous;
    ngx_flag_t                          nested;
    ngx_str_t                           path;
    ngx_uint_t                          naming;
    ngx_uint_t                          slicing;
    ngx_uint_t                          type;
    ngx_path_t                         *slot;
    ngx_msec_t                          max_audio_delay;
    size_t                              audio_buffer_size;
    ngx_flag_t                          cleanup;
    ngx_array_t                        *variant;
    ngx_str_t                           base_url;
    ngx_int_t                           granularity;
} ngx_rtmp_hls_app_conf_t;

extern ngx_module_t  ngx_rtmp_hls_module;
