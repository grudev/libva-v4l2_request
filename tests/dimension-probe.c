/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Guarded, real-device VP9 admission probe. No slice data is submitted.
 * The executable observes ioctl calls without changing their arguments/results.
 * Full uninstrumented decode comparisons are a separate acceptance gate. */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_dec_vp9.h>

static unsigned int queries, enumerations, formats, allocations, controls, queues;
static unsigned int capability_controls;

int ioctl(int fd, unsigned long request, ...)
{
    static int (*real_ioctl)(int, unsigned long, ...);
    if (!real_ioctl) {
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
        assert(real_ioctl);
    }
    queries += request == VIDIOC_QUERYCAP;
    enumerations += request == VIDIOC_ENUM_FRAMESIZES;
    formats += request == VIDIOC_S_FMT;
    allocations += request == MEDIA_IOC_REQUEST_ALLOC;
    controls += request == VIDIOC_S_EXT_CTRLS;
    queues += request == MEDIA_REQUEST_IOC_QUEUE;
    if (request == MEDIA_REQUEST_IOC_QUEUE || request == MEDIA_REQUEST_IOC_REINIT)
        return real_ioctl(fd, request);
    va_list ap;
    va_start(ap, request);
    void *argument = va_arg(ap, void *);
    va_end(ap);
    return real_ioctl(fd, request, argument);
}

static void check(VAStatus status)
{
    if (status != VA_STATUS_SUCCESS) {
        fprintf(stderr, "unexpected VA status: %s\n", vaErrorStr(status));
        abort();
    }
}

static void selected_library(void)
{
    const char *directory = getenv("LIBVA_DRIVERS_PATH");
    assert(directory && !strchr(directory, ':'));
    char requested[PATH_MAX], expected[PATH_MAX], line[8192];
    assert(snprintf(requested, sizeof(requested), "%s/v4l2_request_drv_video.so",
                    directory) < (int)sizeof(requested));
    assert(realpath(requested, expected));
    FILE *maps = fopen("/proc/self/maps", "r");
    assert(maps);
    bool found = false;
    while (fgets(line, sizeof(line), maps)) {
        if (!strstr(line, "v4l2_request_drv_video.so"))
            continue;
        char *path = strchr(line, '/');
        assert(path);
        path[strcspn(path, "\n")] = 0;
        assert(!strcmp(path, expected));
        found = true;
    }
    assert(!fclose(maps) && found);
}

static void attributes(VADisplay display, VAConfigID config, int profile)
{
    unsigned int count = 0, seen = 0;
    check(vaQuerySurfaceAttributes(display, config, NULL, &count));
    assert(count && count <= 64);
    VASurfaceAttrib *attrs = calloc(count, sizeof(*attrs));
    assert(attrs);
    unsigned int before = enumerations;
    check(vaQuerySurfaceAttributes(display, config, attrs, &count));
    assert(enumerations > before);
    for (unsigned int i = 0; i < count; i++) {
        int expected;
        switch (attrs[i].type) {
        case VASurfaceAttribMinWidth: case VASurfaceAttribMinHeight:
            expected = 64; break;
        case VASurfaceAttribMaxWidth: case VASurfaceAttribMaxHeight:
            expected = 4096; break;
        default: continue;
        }
        assert(attrs[i].value.type == VAGenericValueTypeInteger);
        assert(attrs[i].value.value.i == expected);
        seen++;
    }
    assert(seen == 4);
    free(attrs);
    printf("{\"case\":\"attributes\",\"profile\":%d,\"minimum\":64,\"maximum\":4096}\n", profile);
}

static void context(VADisplay display, VAConfigID config, int profile,
                    unsigned int width, unsigned int height, bool valid)
{
    unsigned int old_formats = formats, old_allocations = allocations;
    VAContextID id = VA_INVALID_ID;
    VAStatus result = vaCreateContext(display, config, width, height,
                                      VA_PROGRESSIVE, NULL, 0, &id);
    if (valid) {
        check(result);
        assert(id != VA_INVALID_ID);
        /* These establish that the observer sees real context-setup ioctls. */
        assert(formats > old_formats && allocations > old_allocations);
        check(vaDestroyContext(display, id));
    } else {
        assert(result != VA_STATUS_SUCCESS && id == VA_INVALID_ID);
        assert(formats == old_formats && allocations == old_allocations);
    }
    assert(controls == capability_controls && !queues);
    printf("{\"case\":\"context\",\"profile\":%d,\"width\":%u,\"height\":%u,"
           "\"accepted\":%s,\"status\":%u,\"format_calls\":%u,\"request_allocations\":%u}\n",
           profile, width, height, valid ? "true" : "false", result,
           formats - old_formats, allocations - old_allocations);
}

static void picture(VADisplay display, VAConfigID config, int profile,
                    unsigned int width, unsigned int height, bool valid)
{
    VAContextID id = VA_INVALID_ID;
    VASurfaceID surface = VA_INVALID_ID;
    VABufferID buffer = VA_INVALID_ID;
    unsigned int format = profile ? VA_RT_FORMAT_YUV420_10 : VA_RT_FORMAT_YUV420;
    check(vaCreateContext(display, config, 64, 64, VA_PROGRESSIVE, NULL, 0, &id));
    check(vaCreateSurfaces(display, format, 64, 64, &surface, 1, NULL, 0));
    VADecPictureParameterBufferVP9 pic = {
        .frame_width = width, .frame_height = height,
        .profile = profile, .bit_depth = profile ? 10 : 8,
    };
    check(vaCreateBuffer(display, id, VAPictureParameterBufferType,
                          sizeof(pic), 1, &pic, &buffer));
    check(vaBeginPicture(display, id, surface));
    unsigned int old_allocations = allocations;
    VAStatus result = vaRenderPicture(display, id, &buffer, 1);
    assert(result == (valid ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_BUFFER));
    /* Incomplete even for the positive control: no bitstream was provided. */
    VAStatus end = vaEndPicture(display, id);
    assert(end != VA_STATUS_SUCCESS);
    assert(allocations == old_allocations && controls == capability_controls && !queues);
    check(vaDestroyBuffer(display, buffer));
    check(vaDestroyContext(display, id));
    check(vaDestroySurfaces(display, &surface, 1));
    printf("{\"case\":\"picture\",\"profile\":%d,\"width\":%u,\"height\":%u,"
           "\"accepted\":%s,\"status\":%u,\"end_status\":%u,\"submissions\":0}\n",
           profile, width, height, valid ? "true" : "false", result, end);
}

int main(void)
{
    if (!getenv("LIBVA_HW_GUARD_LEASE") || !getenv("LIBVA_DRIVERS_PATH") ||
        !getenv("LIBVA_DRIVER_NAME") ||
        strcmp(getenv("LIBVA_DRIVER_NAME"), "v4l2_request")) {
        fputs("use hwguard with an explicitly selected v4l2_request build\n", stderr);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    assert(fd >= 0);
    VADisplay display = vaGetDisplayDRM(fd);
    assert(display);
    int major, minor;
    check(vaInitialize(display, &major, &minor));
    selected_library();
    assert(queries);
    /* Driver discovery probes control capabilities before any VA admission.
     * Keep those calls visible, but separate from the operations under test. */
    capability_controls = controls;
    assert(!queues);
    for (int profile = 0; profile <= 2; profile += 2) {
        VAConfigID config = VA_INVALID_ID;
        check(vaCreateConfig(display, profile ? VAProfileVP9Profile2 : VAProfileVP9Profile0,
                             VAEntrypointVLD, NULL, 0, &config));
        attributes(display, config, profile);
        const unsigned int rejected[][2] = {{8,8}, {63,64}, {64,63},
                                            {4097,64}, {64,4097}};
        for (unsigned int i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
            context(display, config, profile, rejected[i][0], rejected[i][1], false);
        context(display, config, profile, 64, 64, true);
        context(display, config, profile, 66, 66, true);
        for (unsigned int i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
            picture(display, config, profile, rejected[i][0], rejected[i][1], false);
        picture(display, config, profile, 64, 64, true);
        picture(display, config, profile, 66, 66, true);
        check(vaDestroyConfig(display, config));
    }
    check(vaTerminate(display));
    assert(!close(fd));
    assert(controls == capability_controls && !queues);
    printf("{\"case\":\"summary\",\"passed\":true,\"query_calls\":%u,"
           "\"dimension_enumerations\":%u,\"control_submissions\":%u,\"request_queues\":%u,"
           "\"initial_capability_control_calls\":%u}\n",
           queries, enumerations, controls - capability_controls, queues, capability_controls);
    return 0;
}
