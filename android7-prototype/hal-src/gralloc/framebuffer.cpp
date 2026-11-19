/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>

#include <dlfcn.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#ifdef __ANDROID__
#include <linux/fb.h>
#endif

#include "gralloc_priv.h"
#include "gr.h"
#include "kd-frame-protocol.h"

/*****************************************************************************/

// Set TARGET_USE_PAN_DISPLAY to true at compile time if the
// board uses FBIOPAN_DISPLAY to setup page flipping, otherwise
// default ioctl to do page-flipping is FBIOPUT_VSCREENINFO.
#ifndef USE_PAN_DISPLAY
#define USE_PAN_DISPLAY 0
#endif

// numbers of buffers for page flipping
#define NUM_BUFFERS 2


enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
};

/*****************************************************************************/

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

static int fb_setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h)
{
    if (((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;
        
    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    m->info.reserved[0] = 0x54445055; // "UPDT";
    m->info.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->info.reserved[2] = (uint16_t)(l+w) | ((uint32_t)(t+h) << 16);
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (private_handle_t::validate(buffer) < 0)
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(buffer);
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO failed");
            m->base.unlock(&m->base, buffer); 
            return -errno;
        }
        m->currentBuffer = buffer;
        
    } else {
        // If we can't do the page_flip, just copy the buffer to the front 
        // FIXME: use copybit HAL instead of memcpy
        
        void* fb_vaddr;
        void* buffer_vaddr;
        
        int err = m->base.lock(&m->base, m->framebuffer, 
                GRALLOC_USAGE_SW_WRITE_RARELY, 
                0, 0, m->info.xres, m->info.yres,
                &fb_vaddr);
        if (err) return err;

        err = m->base.lock(&m->base, buffer, 
                GRALLOC_USAGE_SW_READ_RARELY, 
                0, 0, m->info.xres, m->info.yres,
                &buffer_vaddr);
        if (err) { m->base.unlock(&m->base, m->framebuffer); return err; }

        const size_t pixels = m->finfo.line_length * m->info.yres;
        kd_frame_meta* commit = reinterpret_cast<kd_frame_meta*>(
                static_cast<char*>(fb_vaddr) + kd_frame_offset(pixels));
        // Serialize writers; the host reader never takes this Android-private lock.
        pthread_mutex_lock(&m->lock);
        kd_frame_begin(commit);
        memcpy(fb_vaddr, buffer_vaddr, pixels);
        kd_frame_end(commit);
        pthread_mutex_unlock(&m->lock);
        
        m->base.unlock(&m->base, buffer); 
        m->base.unlock(&m->base, m->framebuffer); 
    }
    
    return 0;
}

/*****************************************************************************/

// kindleroid: there is no display device for Android on the Kindle (the e-ink panel belongs to the
// native UI). The framebuffer is a single memfd-backed buffer; post() copies each composed frame into
// it, so it always holds the latest frame for an external reader. Geometry comes from properties.
static int fbProperty(const char* name, int def)
{
    char value[PROPERTY_VALUE_MAX];
    property_get(name, value, "");
    int v = atoi(value);
    return v > 0 ? v : def;
}

int mapFrameBufferLocked(struct private_module_t* module)
{
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }

    const int xres = fbProperty("ro.kindleroid.fb.width", 1264);
    const int yres = fbProperty("ro.kindleroid.fb.height", 1680);
    const int bpp = fbProperty("ro.kindleroid.fb.bpp", 16) == 32 ? 32 : 16;
    const int dpi = fbProperty("ro.kindleroid.fb.dpi", 300);
    const int fps = fbProperty("ro.kindleroid.fb.fps", 30);

    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    memset(&info, 0, sizeof(info));
    memset(&finfo, 0, sizeof(finfo));
    info.xres = info.xres_virtual = xres;
    info.yres = info.yres_virtual = yres;
    info.bits_per_pixel = bpp;
    if (bpp == 16) {
        info.red.offset = 11;   info.red.length = 5;
        info.green.offset = 5;  info.green.length = 6;
        info.blue.offset = 0;   info.blue.length = 5;
    } else {
        // RGBX_8888: red at offset 0 (see fb_device_open's format selection)
        info.red.offset = 0;    info.red.length = 8;
        info.green.offset = 8;  info.green.length = 8;
        info.blue.offset = 16;  info.blue.length = 8;
    }
    info.width  = (int)((xres * 25.4f) / dpi + 0.5f);
    info.height = (int)((yres * 25.4f) / dpi + 0.5f);
    strncpy(finfo.id, "kindleroid-mem", sizeof(finfo.id) - 1);
    finfo.line_length = xres * (bpp >> 3);

    size_t pixelBytes = finfo.line_length * info.yres_virtual;
    size_t fbSize = kd_frame_offset(pixelBytes) + KD_FRAME_PAGE;
    finfo.smem_len = fbSize;
    int fd = ashmem_create_region("kindleroid-framebuffer", fbSize);
    if (fd < 0) {
        ALOGE("couldn't create framebuffer memory (%s)", strerror(errno));
        return -errno;
    }
    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        close(fd);
        return -errno;
    }
    memset(vaddr, 0, fbSize);
    kd_frame_init(reinterpret_cast<kd_frame_meta*>(static_cast<char*>(vaddr) +
            kd_frame_offset(pixelBytes)), xres, yres, finfo.line_length);

    ALOGI("kindleroid memory framebuffer %dx%d %d bpp, %d dpi, %d fps, %zu bytes",
            xres, yres, bpp, dpi, fps, fbSize);

    module->flags = 0;          // no page flipping: one front buffer, post() copies into it
    module->info = info;
    module->finfo = finfo;
    module->xdpi = dpi;
    module->ydpi = dpi;
    module->fps = fps;
    module->framebuffer = new private_handle_t(fd, fbSize, 0);
    module->framebuffer->base = intptr_t(vaddr);
    module->numBuffers = 1;
    module->bufferMask = 0;
    return 0;
}

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        /* initialize our state here */
        fb_context_t *dev = (fb_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect = 0;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            int format = (m->info.bits_per_pixel == 32)
                         ? (m->info.red.offset ? HAL_PIXEL_FORMAT_BGRA_8888 : HAL_PIXEL_FORMAT_RGBX_8888)
                         : HAL_PIXEL_FORMAT_RGB_565;
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = format;
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) = 1;
            const_cast<int&>(dev->device.maxSwapInterval) = 1;
            *device = &dev->device.common;
        }
    }
    return status;
}
