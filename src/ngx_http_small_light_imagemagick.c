/**
  Copyright (c) 2012-2016 Tatsuhiko Kubo <cubicdaiya@gmail.com>
  Copyright (c) 1996-2011 livedoor Co.,Ltd.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include "ngx_http_small_light_imagemagick.h"
#include "ngx_http_small_light_size.h"
#include "ngx_http_small_light_parser.h"
#include "ngx_http_small_light_type.h"

extern const char *ngx_http_small_light_image_exts[];
extern const char *ngx_http_small_light_image_types[];

void ngx_http_small_light_imagemagick_adjust_image_offset(
    ngx_http_request_t *r,
    ngx_http_small_light_imagemagick_ctx_t *ictx,
    ngx_http_small_light_image_size_t *sz
) {
    MagickBooleanType status;
    size_t            w, h;
    ssize_t           x, y;

    status = MagickGetImagePage(ictx->wand, &w, &h, &x, &y);
    if (status == MagickFalse) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "failed to get image page %s:%d",
                        __FUNCTION__,
                        __LINE__);
        return;
    }

    if (x != 0) {
        sz->sx = (double)x + sz->sx;
    }
    if (y != 0) {
        sz->sy = (double)y + sz->sy;
    }
}

ngx_int_t ngx_http_small_light_imagemagick_init(
    ngx_http_request_t *r, 
    ngx_http_small_light_ctx_t *ctx
) {
    ngx_http_small_light_imagemagick_ctx_t *ictx;
    ictx            = (ngx_http_small_light_imagemagick_ctx_t *)ctx->ictx;
    ictx->wand      = NewMagickWand();
    ictx->image     = ctx->content;
    ictx->image_len = ctx->content_length;
    ictx->type      = ngx_http_small_light_type_detect(ictx->image, ictx->image_len);
    ictx->complete  = 0;
    if (ictx->type == NGX_HTTP_SMALL_LIGHT_IMAGE_NONE) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "failed to get image type %s:%d",
                        __FUNCTION__,
                        __LINE__);
        return NGX_ERROR;
    }
    return NGX_OK;
}

void ngx_http_small_light_imagemagick_term(void *data)
{
    ngx_http_small_light_ctx_t *ctx;
    ngx_http_small_light_imagemagick_ctx_t *ictx;
    ctx  = (ngx_http_small_light_ctx_t *)data;
    ictx = (ngx_http_small_light_imagemagick_ctx_t *)ctx->ictx;

    if (ictx->complete) {
        MagickRelinquishMemory(ctx->content);
    }

    DestroyMagickWand(ictx->wand);
}

/**
* following original functions are brought from
* mod_small_light(Dynamic image transformation module for Apache2) and customed
*/

ngx_int_t ngx_http_small_light_imagemagick_process(
    ngx_http_request_t *r, 
    ngx_http_small_light_ctx_t *ctx
) {
    ngx_http_small_light_imagemagick_ctx_t *ictx;
    ngx_http_small_light_image_size_t       sz;
    MagickBooleanType                       status;
    int                                     rmprof_flg, progressive_flg, cmyk2rgb_flg, bluropt_flag;
    double                                  iw, ih, q;
    char                                   *unsharp, *sharpen, *blur, *of, *of_orig;
    MagickWand                             *canvas_wand, *canvas_bg_wand, *source_wand;
    DrawingWand                            *border_wand;
    PixelWand                              *bg_color, *canvas_color, *border_color;
    GeometryInfo                            geo;
    ngx_fd_t                                fd;
    MagickWand                             *icon_wand, *merge_wand;
    u_char                                 *p, *embedicon;
    size_t                                  embedicon_path_len, embedicon_len, sled_image_size;
    ngx_int_t                               type;
    u_char                                  jpeg_size_opt[32], embedicon_path[256];
    ColorspaceType                          color_space;
#if MagickLibVersion >= 0x690
    int                                     autoorient_flg, backgroundfill_flg;
#endif
   status = MagickFalse;
   ictx = (ngx_http_small_light_imagemagick_ctx_t *)ctx->ictx;

   /* adjust image size */
   ngx_http_small_light_calc_image_size(r, ctx, &sz, 10000.0, 10000.0);

   /* prepare */
   if (sz.jpeghint_flg != 0 &&
       sz.dw != NGX_HTTP_SMALL_LIGHT_COORD_INVALID_VALUE &&
       sz.dh != NGX_HTTP_SMALL_LIGHT_COORD_INVALID_VALUE) 
    {
        p = ngx_snprintf((u_char *)jpeg_size_opt, sizeof(jpeg_size_opt) - 1, "%dx%d", (ngx_int_t)sz.dw, (ngx_int_t)sz.dh);
        *p = '\0';
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "jpeg_size_opt:%s", jpeg_size_opt);
        MagickSetOption(ictx->wand, "jpeg:size", (char *)jpeg_size_opt);
    }

   /* load image. */
   status = MagickReadImageBlob(ictx->wand, (void *)ictx->image, ictx->image_len);
   if (status == MagickFalse) {
       ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                     "couldn't read image %s:%d",
                     __FUNCTION__,
                     __LINE__);
       return NGX_ERROR;
   }

   /* set the first frame for animated-GIF */
   MagickSetFirstIterator(ictx->wand);

   color_space = MagickGetImageColorspace(ictx->wand);

   /* remove all profiles */
    rmprof_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "rmprof"));
    if (rmprof_flg != 0) {
        status = MagickProfileImage(ictx->wand, "*", NULL, 0);
        if (status == MagickFalse) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "couldn't profiling image %s:%d",
                            __FUNCTION__,
                            __LINE__);
        }
    }

    of_orig = MagickGetImageFormat(ictx->wand);
    status = MagickTrue;

#if MagickLibVersion >= 0x690
    /* auto-orient */
    autoorient_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "autoorient"));
    if (autoorient_flg != 0) {
        status = MagickAutoOrientImage(ictx->wand);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyString(of_orig);
            return NGX_ERROR;
        }
    }
#endif

    /* rotate. */
    if (sz.angle) {
        bg_color = NewPixelWand();
        PixelSetRed(bg_color,   sz.cc.r / 255.0);
        PixelSetGreen(bg_color, sz.cc.g / 255.0);
        PixelSetBlue(bg_color,  sz.cc.b / 255.0);
        PixelSetAlpha(bg_color, sz.cc.a / 255.0);

        switch (sz.angle) {
        case 90:
        case 180:
        case 270:
            MagickRotateImage(ictx->wand, bg_color, sz.angle);
            break;
        default:
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "image not rotated. 'angle'(%ui) must be 90 or 180 or 270. %s:%d",
                            sz.angle,
                            __FUNCTION__,
                            __LINE__);
            break;
        }

        DestroyPixelWand(bg_color);
    }

    /* fix stretch image when jpeghint=y */
    if (sz.jpeghint_flg != 0) {
        /* calc source size. */
        source_wand = NewMagickWand();
        status = MagickReadImageBlob(source_wand, (void *)ictx->image, ictx->image_len);
        if (status == MagickFalse) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "couldn't read image %s:%d",
                            __FUNCTION__,
                            __LINE__);
            return NGX_ERROR;
        }

        #if MagickLibVersion >= 0x690
            /* auto-orient */
            autoorient_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "autoorient"));
            if (autoorient_flg != 0) {
                status = MagickAutoOrientImage(source_wand);
                if (status == MagickFalse) {
                    r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                    DestroyString(of_orig);
                    return NGX_ERROR;
                }
            }
        #endif

        iw = (double)MagickGetImageWidth(source_wand);
        ih = (double)MagickGetImageHeight(source_wand);
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                        "source width:%f,source height:%f,dw:%f,dh:%f",
                        iw, ih, sz.dw, sz.dh);
        if(iw < sz.dw || ih < sz.dh) {
                MagickAdaptiveResizeImage(ictx->wand, iw, ih);
        }
        DestroyMagickWand(source_wand);
    }

   /* calc size. */
    iw = (double)MagickGetImageWidth(ictx->wand);
    ih = (double)MagickGetImageHeight(ictx->wand);
    ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                    "source width:%f,source height:%f,dw:%f,dh:%f",
                    iw, ih, sz.dw, sz.dh);

    /* dpr adjustment */
    if (sz.img_dpr > 1 && (iw < sz.dw || ih < sz.dh )) {
        MagickAdaptiveResizeImage(ictx->wand, iw*sz.img_dpr, ih*sz.img_dpr);

        sz.sw = iw * sz.img_dpr;
        sz.sh = ih * sz.img_dpr;
        sz.scale_flg = 1;

        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                        "dpr info:iw=%f,ih=%f,sw=%f,sh=%f,resized source to %f x %f",
                        iw, ih, sz.sw, sz.sh, iw*sz.img_dpr, ih*sz.img_dpr);

    }

    /* calc size again */
    iw = (double)MagickGetImageWidth(ictx->wand);
    ih = (double)MagickGetImageHeight(ictx->wand);
    ngx_http_small_light_calc_image_size(r, ctx, &sz, iw, ih);


    /* adjust image offset automatically */
    ngx_http_small_light_imagemagick_adjust_image_offset(r, ictx, &sz);

    /* pass through. */
    if (sz.pt_flg != 0) {
        ctx->of = ctx->inf;
        DestroyString(of_orig);
        return NGX_OK;
    }

    /* adjust destination size */
    if (sz.dw == NGX_HTTP_SMALL_LIGHT_COORD_INVALID_VALUE) {
        sz.dw = sz.sw;
    }
    if (sz.dh == NGX_HTTP_SMALL_LIGHT_COORD_INVALID_VALUE) {
        sz.dh = sz.sh;
    }

    /* crop, scale. */
    if (sz.scale_flg != 0) {
        /* crop */
        status = MagickCropImage(ictx->wand, sz.sw, sz.sh, sz.sx, sz.sy);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        /* scale */
        status = MagickResizeImage(ictx->wand, sz.dw, sz.dh, LanczosFilter);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyString(of_orig);
            return NGX_ERROR;
        }
    }

    /* create canvas then draw image to the canvas. */
    if (sz.cw > 0.0 && sz.ch > 0.0) {
        canvas_wand  = NewMagickWand();
        canvas_color = NewPixelWand();
        PixelSetRed(canvas_color,   sz.cc.r / 255.0);
        PixelSetGreen(canvas_color, sz.cc.g / 255.0);
        PixelSetBlue(canvas_color,  sz.cc.b / 255.0);
        PixelSetAlpha(canvas_color, sz.cc.a / 255.0);
        status = MagickNewImage(canvas_wand, sz.cw, sz.ch, canvas_color);
        DestroyPixelWand(canvas_color);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyMagickWand(canvas_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        status = MagickSetImageDepth(canvas_wand, MagickGetImageDepth(ictx->wand));
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyMagickWand(canvas_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        status = MagickTransformImageColorspace(canvas_wand, color_space);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyMagickWand(canvas_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        ngx_http_small_light_adjust_canvas_image_offset(&sz);

        backgroundfill_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "backgroundfill"));
        if (backgroundfill_flg == 1) {

            ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "TEST backgroundfill_flg:%d", backgroundfill_flg);
            // first trim whitespace off the original image
            MagickTrimImage(ictx->wand, 1.0);

            canvas_bg_wand = CloneMagickWand(ictx->wand);
            MagickResizeImage(canvas_bg_wand, sz.cw/4, sz.ch/4, LanczosFilter);
            MagickGaussianBlurImage(canvas_bg_wand, 0, 1);
            MagickResizeImage(canvas_bg_wand, sz.cw*2, sz.ch*2, LanczosFilter);
            MagickSetImageAlpha(canvas_bg_wand, 0.5);

            status = MagickCompositeImageGravity(canvas_wand, canvas_bg_wand, AtopCompositeOp, CenterGravity);
            if (status == MagickFalse) {
                r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                DestroyMagickWand(canvas_wand);
                DestroyString(of_orig);
                return NGX_ERROR;
            }
            DestroyMagickWand(canvas_bg_wand);

        }

        status = MagickCompositeImage(
            canvas_wand,
            ictx->wand,
            AtopCompositeOp,
            MagickFalse,
            sz.dx,
            sz.dy
        );
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyMagickWand(canvas_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        DestroyMagickWand(ictx->wand);
        ictx->wand = canvas_wand;
    }

    /* CMYK to sRGB */
    cmyk2rgb_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "cmyk2rgb"));
    if (cmyk2rgb_flg != 0 && color_space == CMYKColorspace) {
        status = MagickTransformImageColorspace(ictx->wand, sRGBColorspace);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyString(of_orig);
            return NGX_ERROR;
        }
    }

    /* optimized blur. */
    bluropt_flag = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "bluropt"));
    if (bluropt_flag != 0) {
        // The blur in the form of command line is: 
        /**
         * convert test.jpg -resize 10% -background "#fff" -flatten -strip -filter Gaussian \ 
         *      -unsharp 0.25x0.08+8.3+0.045 -quality 35 -define png:compression-filter=5 \ 
         *      -define png:compression-level=9 -define png:compression-strategy=1 \
         *      -define png:exclude-chunk=all  -define filter:sigma=4.5 -resize 1000% o-o-1.jpg
         */

        // Resize image to 10% if image width and height are larger than 15px.
        if (
            iw > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE 
            && ih > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE
        ) {
            status = MagickResizeImage(ictx->wand, iw/10, ih/10, CubicFilter);
            if (status == MagickFalse) {
                r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                DestroyString(of_orig);
                return NGX_ERROR;
            }
        }
        

        // Set background color.
        bg_color = NewPixelWand();
        PixelSetColor(bg_color, "white");
        MagickSetImageBackgroundColor(ictx->wand, bg_color);

        // Flatten the image to handle if image has multiple layers.
        merge_wand = NewMagickWand();
        merge_wand = MagickMergeImageLayers(ictx->wand, FlattenLayer);
        if (merge_wand == NULL) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        ictx->wand = CloneMagickWand(merge_wand);

        // Strip image from all profiles and comments.
        MagickStripImage(ictx->wand);

        // Unsharp mask.
        status = MagickUnsharpMaskImage(ictx->wand, 0.25, 0.08, 8.3, 0.045);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Set image compression quality.
        status = MagickSetImageCompressionQuality(ictx->wand, 35);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Set PNG options.
        MagickSetOption(ictx->wand, "png:compression-filter", "5");
        MagickSetOption(ictx->wand, "png:compression-level", "9");
        MagickSetOption(ictx->wand, "png:compression-strategy", "1");
        MagickSetOption(ictx->wand, "png:exclude-chunk", "all");

        // size_t t = 0;
        // unsigned char *blob = MagickGetImageBlob(ictx->wand, &t);
        // MagickReadImageBlob(ictx->wand, blob, t);

        // Blur image using Gaussian blur.
        status = MagickGaussianBlurImage(ictx->wand, 0, 4.5);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Resize image to 1000% if image width and height are larger than 15px.
        if (
            iw > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE 
            && ih > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE
        ) {
            status = MagickResizeImage(ictx->wand, iw, ih, CubicFilter);
            if (status == MagickFalse) {
                r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                DestroyPixelWand(bg_color);
                DestroyMagickWand(merge_wand);
                DestroyString(of_orig);
                return NGX_ERROR;
            }
        }
        
        DestroyPixelWand(bg_color);
        DestroyMagickWand(merge_wand);
    }

    /* unsharp */
    unsharp = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "unsharp");
    if (ngx_strlen(unsharp) > 0) {
        ParseGeometry(unsharp, &geo);
        if (bluropt_flag != 0) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As bluropt is set, unsharp is ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else if (geo.rho > ctx->radius_max || geo.sigma > ctx->sigma_max) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As unsharp geometry is too large, ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else {
            status = MagickUnsharpMaskImage(ictx->wand, geo.rho, geo.sigma, geo.xi, geo.psi);
            if (status == MagickFalse) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "unsharp failed %s:%d",
                                __FUNCTION__,
                                __LINE__);
            }
        }
    }

    /* sharpen. */
    sharpen = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "sharpen");
    if (ngx_strlen(sharpen) > 0) {
        ParseGeometry(sharpen, &geo);
        if (bluropt_flag != 0) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As bluropt is set, sharpen is ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else if (geo.rho > ctx->radius_max || geo.sigma > ctx->sigma_max) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As sharpen geometry is too large, ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else {
            status = MagickSharpenImage(ictx->wand, geo.rho, geo.sigma);
            if (status == MagickFalse) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "sharpen failed %s:%d",
                                __FUNCTION__,
                                __LINE__);
            }
        }
    }

    /* blur. */
    blur = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "blur");
    if (ngx_strlen(blur) > 0) {
        ParseGeometry(blur, &geo);
        if (bluropt_flag != 0) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As bluropt is set, blur is ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else if (geo.rho > ctx->radius_max || geo.sigma > ctx->sigma_max) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                            "As blur geometry is too large, ignored. %s:%d",
                            __FUNCTION__,
                            __LINE__);
        } else {
            status = MagickBlurImage(ictx->wand, geo.rho, geo.sigma);
            if (status == MagickFalse) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                "blur failed %s:%d",
                                __FUNCTION__,
                                __LINE__);
            }
        }
    }

    /* border. */
    if (sz.bw > 0.0 || sz.bh > 0.0) {
        border_wand = NewDrawingWand();
        border_color = NewPixelWand();
        PixelSetRed(border_color,   sz.bc.r / 255.0);
        PixelSetGreen(border_color, sz.bc.g / 255.0);
        PixelSetBlue(border_color,  sz.bc.b / 255.0);
        PixelSetAlpha(border_color, sz.bc.a / 255.0);
        DrawSetFillColor(border_wand, border_color);
        DrawSetStrokeColor(border_wand, border_color);
        DrawSetStrokeWidth(border_wand, 1);

        if (sz.cw > 0.0 && sz.ch > 0.0) {
            DrawRectangle(border_wand, 0, 0, sz.cw - 1, sz.bh - 1);
            DrawRectangle(border_wand, 0, 0, sz.bw - 1, sz.ch - 1);
            DrawRectangle(border_wand, 0, sz.ch - sz.bh, sz.cw - 1, sz.ch - 1);
            DrawRectangle(border_wand, sz.cw - sz.bw, 0, sz.cw - 1, sz.ch - 1);
        } else {
            DrawRectangle(border_wand, 0, 0, sz.dw - 1, sz.bh - 1);
            DrawRectangle(border_wand, 0, 0, sz.bw - 1, sz.dh - 1);
            DrawRectangle(border_wand, 0, sz.dh - sz.bh, sz.dw - 1, sz.dh - 1);
            DrawRectangle(border_wand, sz.dw - sz.bw, 0, sz.dw - 1, sz.dh - 1);
        }
        MagickDrawImage(ictx->wand, border_wand);
        DestroyPixelWand(border_color);
        DestroyDrawingWand(border_wand);
    }

    /* embed icon */
    embedicon = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "embedicon");
    if (ctx->material_dir->len > 0 && ngx_strlen(embedicon) > 0) {
        if (ngx_strstrn((u_char *)embedicon, "/", 1 - 1)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "invalid parameter 'embedicon':%s %s:%d",
                            embedicon,
                            __FUNCTION__,
                            __LINE__);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        embedicon_len      = ngx_strlen(embedicon);
        embedicon_path_len = ctx->material_dir->len + ngx_strlen("/") + embedicon_len;
        if (embedicon_path_len > sizeof(embedicon_path) - 1) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "embedicon path is too long. maximun value is %z %s:%d",
                            sizeof(embedicon_path) - 1,
                            __FUNCTION__,
                            __LINE__);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        p = embedicon_path;
        p = ngx_cpystrn(p, ctx->material_dir->data, ctx->material_dir->len + 1);
        p = ngx_cpystrn(p, (u_char *)"/", 1 + 1);
        p = ngx_cpystrn(p, embedicon, embedicon_len + 1);

        if ((fd = ngx_open_file(embedicon_path, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0)) == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to open embeddedicon file:%s %s:%d",
                            embedicon_path,
                            __FUNCTION__,
                            __LINE__);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        if (ngx_close_file(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to close:%s %s:%d",
                            embedicon_path,
                            __FUNCTION__,
                            __LINE__);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        if (ngx_strstrn(embedicon_path, "..", 2 - 1)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "invalid embeddedicon_path:%s %s:%d",
                            embedicon_path,
                            __FUNCTION__,
                            __LINE__);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        icon_wand = NewMagickWand();
        if (MagickReadImage(icon_wand, (char *)embedicon_path) == MagickFalse) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to read embed icon image file:%s %s:%d",
                            embedicon_path,
                            __FUNCTION__,
                            __LINE__);
            DestroyMagickWand(icon_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }

        MagickCompositeImage(ictx->wand, icon_wand, OverCompositeOp, MagickFalse, sz.ix, sz.iy);
        DestroyMagickWand(icon_wand);
    }

    /* set params. */
    q = ngx_http_small_light_parse_double(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "q"));
    if (q > 0.0 && q <= 100) {
        MagickSetImageCompressionQuality(ictx->wand, q);
    }

    progressive_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "progressive"));
    if (progressive_flg != 0) {
        MagickSetInterlaceScheme(ictx->wand, LineInterlace);
    }

    of = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "of");
    if (ngx_strlen(of) > 0) {
        type = ngx_http_small_light_type(of);
        if (type == NGX_HTTP_SMALL_LIGHT_IMAGE_NONE) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "of is invalid(%s) %s:%d",
                            of,
                            __FUNCTION__,
                            __LINE__);
            of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
        } else if (type == NGX_HTTP_SMALL_LIGHT_IMAGE_WEBP) {
            if( iw > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_WEBP ||
                ih > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_WEBP) {
                of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
                    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                        "width=%f or height=%f is too large for webp transformation, MAX SIZE WEBP : %d. Resetting type to %s",
                        iw, ih,
                        NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_WEBP,
                        of
                    );
            } else {
#if defined(MAGICKCORE_WEBP_DELEGATE)
                ictx->type = type;
#else
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "WebP is not supported %s:%d",
                            __FUNCTION__,
                            __LINE__);
            of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
#endif
            }
        } else if (type == NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF) {
            if (
                iw > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF ||
                ih > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF
            ) {
                of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
                ngx_log_error(
                    NGX_LOG_WARN, r->connection->log, 0,
                    "width=%f or height=%f is too large for avif transformation, MAX SIZE AVIF : %d. Resetting type to %s",
                    iw, ih,
                    NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF,
                    of
                );
            } else {
#if defined(MAGICKCORE_HEIC_DELEGATE)
                ictx->type = type;
#else
                ngx_log_error(
                    NGX_LOG_ERR, r->connection->log, 0,
                    "AVIF is not supported %s:%d",
                    __FUNCTION__,
                    __LINE__);
                
                of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
#endif
            }
        } else {
            ictx->type = type;
        }

        // Even if the library supports the format, the encoder/decoder may not be available.
        status = MagickSetFormat(ictx->wand, of);
        if (status == MagickFalse) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to set format(%s) %s:%d",
                            of,
                            __FUNCTION__,
                            __LINE__);
            MagickSetFormat(ictx->wand, of_orig);
            ctx->of = ctx->inf;
        } else {
            ctx->of = ngx_http_small_light_image_types[ictx->type - 1];
        }
    } else {
        MagickSetFormat(ictx->wand, of_orig);
        ctx->of = ctx->inf;
    }

    DestroyString(of_orig);

    ctx->content        = MagickGetImageBlob(ictx->wand, &sled_image_size);
    ctx->content_length = sled_image_size;

    ngx_pfree(r->pool, ctx->content_orig);

    ictx->complete = 1;

    return NGX_OK;
}

void ngx_http_small_light_imagemagick_genesis(void)
{
    MagickCoreGenesis(NULL, MagickTrue);
    MagickWandGenesis();
}

void ngx_http_small_light_imagemagick_terminus(void)
{
    MagickCoreTerminus();
    MagickWandTerminus();
}

int ngx_http_small_light_imagemagick_set_thread_limit(int limit)
{
    MagickBooleanType status;
    status = MagickSetResourceLimit(ThreadResource, limit);
    if (status == MagickFalse) {
        return 0;
    }
    return 1;
}