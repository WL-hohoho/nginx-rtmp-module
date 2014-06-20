#include "ngx_rtmp_hls_watch_module.h"

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

static void *ngx_rtmp_hls_watch_create_loc_conf(ngx_conf_t *cf);
static char *ngx_rtmp_hls_watch_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_rtmp_hls_watch_postconfiguration(ngx_conf_t *cf);

typedef struct {
    ngx_flag_t watch;
} ngx_rtmp_hls_watch_loc_conf_t;

static ngx_command_t ngx_rtmp_hls_watch_commands[] = {
    { ngx_string("hls_watch"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_hls_watch_loc_conf_t, watch),
      NULL },

    ngx_null_command
};

static ngx_rtmp_module_t  ngx_rtmp_hls_watch_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_rtmp_hls_watch_postconfiguration, /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_rtmp_hls_watch_create_loc_conf, /* create location configuration */
    ngx_rtmp_hls_watch_merge_loc_conf,  /* merge location configuration */
};


ngx_module_t  ngx_rtmp_hls_watch_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_hls_watch_module_ctx,     /* module context */
    ngx_rtmp_hls_watch_commands,        /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_rtmp_hls_frag_t *
ngx_rtmp_hls_watch_find_frag(ngx_uint_t id, ngx_rtmp_hls_app_conf_t *hacf, ngx_rtmp_hls_ctx_t *ctx)
{
    ngx_rtmp_hls_frag_t    *frag;
    size_t                  i;

    for (i = 0; i < ctx->nfrags; i++)
    {
        frag = &ctx->frags[(ctx->frag + i) % (hacf->winfrags * 2 + 1)];

        if (frag->id == id)
            return frag;
    }

    return NULL;
}

static ngx_int_t
ngx_rtmp_hls_watch_update_viewers(ngx_http_request_t *r, ngx_rtmp_session_t *s)
{
    ngx_rtmp_hls_ctx_t      *ctx;
    ngx_rtmp_hls_app_conf_t *hacf;
    ngx_rtmp_hls_frag_t     *frag;
    size_t                   i;
    ngx_str_t                path;
    size_t                   root_len;
    u_char                  *last;
    ngx_uint_t               id;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_hls_module);
    if (ctx == NULL)
        return NGX_ERROR;

    last = ngx_http_map_uri_to_path(r, &path, &root_len, 0);
    if (last == NULL)
        return NGX_ERROR;

    path.len = last - path.data;

    if (ngx_strncmp(path.data, ctx->stream.data, ctx->stream.len) != 0)
        return NGX_AGAIN;   /* this is the wrong stream - try the next one */

    hacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_hls_module);
    if (hacf == NULL)
        return NGX_ERROR;

    last = path.data + ctx->stream.len;
    id = strtoul(last, NULL, 10);

    frag = ngx_rtmp_hls_watch_find_frag(id, hacf, ctx);

    if (frag == NULL || (!frag->active) )
        return NGX_OK;

    frag->hits++;
    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
            "hls-watch: fragment '%uL' hits updated - now %ui",
            frag->id, frag->hits);

    return NGX_OK;
}

static void
ngx_rtmp_hls_watch_walk_live(ngx_http_request_t *r, ngx_rtmp_live_app_conf_t *lacf)
{
    ngx_int_t                n;
    ngx_rtmp_live_stream_t  *stream;
    ngx_rtmp_session_t      *s;
    ngx_int_t                ret;

    for (n = 0; n < lacf->nbuckets; n++)
    {
        for (stream = lacf->streams[n]; stream; stream = stream->next) {
            s = stream->ctx->session;

            if (s == NULL)
                continue;

            if (ngx_rtmp_hls_watch_update_viewers(r, s) != NGX_AGAIN)
                return;
        }
    }
}

static ngx_int_t
ngx_rtmp_hls_watch_filter(ngx_http_request_t *r)
{
    ngx_rtmp_core_main_conf_t      *cmcf;
    ngx_rtmp_core_srv_conf_t      **cscf;
    ngx_rtmp_core_app_conf_t      **cacf;
    ngx_rtmp_live_app_conf_t       *lacf;
    ngx_rtmp_hls_watch_loc_conf_t  *lcf;
    size_t                          i, j;

    lcf = ngx_http_get_module_loc_conf(r, ngx_rtmp_hls_watch_module);
    if (lcf->watch == NULL)
        goto next;

    cmcf = ngx_rtmp_core_main_conf;
    if (cmcf == NULL)
        goto next;

    cscf = cmcf->servers.elts;
    for (i = 0; i < cmcf->servers.nelts; i++, cscf++)
    {
        cacf = (*cscf)->applications.elts;
        for (j = 0; j < (*cscf)->applications.nelts; j++, cacf++)
        {
            lacf = (*cacf)->app_conf[ngx_rtmp_live_module.ctx_index];
            ngx_rtmp_hls_watch_walk_live(r, lacf);
        }
    }

next:
    return ngx_http_next_header_filter(r);
}


static void *
ngx_rtmp_hls_watch_create_loc_conf(ngx_conf_t *cf)
{
    ngx_rtmp_hls_watch_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_hls_watch_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->watch = NGX_CONF_UNSET;

    return conf;
}

static char *
ngx_rtmp_hls_watch_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_hls_watch_loc_conf_t *prev = parent;
    ngx_rtmp_hls_watch_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->watch, prev->watch, 0);

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_rtmp_hls_watch_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_rtmp_hls_watch_filter;

    return NGX_OK;
}
