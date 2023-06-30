#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <drm/drm_fourcc.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/videodev2.h>

enum drm_error_code {
    DRM_DEVICE_OPEN_FAILED = 1,
    DRM_HAS_NO_DUMP,
    DRM_SET_MASTER_FAILED,
    DRM_GET_RESOURCE_FAILED,
    DRM_GET_CONNECTOR_FAILED,
    DMR_GET_CRTC_ID_FAILED,
    DRM_FRAME_BUFFER_INIT_FAILED,
};

struct drm_frame_buffer {
    int fd = -1;
    uint32_t bits_per_pixel;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t pitch = 0;
	uint32_t handle = 0;
	uint32_t size = 0;
	uint8_t *vaddr = nullptr;
	uint32_t fb_id = 0;
    const uint32_t frame_cache_depth = 24;    // bits

    drm_frame_buffer() = default;
    virtual ~drm_frame_buffer() {
        struct drm_mode_destroy_dumb destroy = {};
        drmModeRmFB(fd, fb_id);
        munmap(vaddr, size);
        destroy.handle = handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    bool init() {
        if (fd < 0) {
            printf("error fd:%d\n", fd);
            return false;
        }
        struct drm_mode_create_dumb create = {};
        struct drm_mode_map_dumb map = {};
    	create.width = width;
        create.height = height;
        create.bpp = bits_per_pixel;
        int ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
        if (ret) {
            printf("drm ioctl for DRM_IOCTL_MODE_CREATE_DUMB failed!\n");
            return false;
        }
        pitch = create.pitch;
        size = create.size;
        handle = create.handle;
        ret = drmModeAddFB(fd, width, height, frame_cache_depth, create.bpp, pitch, handle, &fb_id);
        if (ret) {
            printf("drm mode add fb failed!\n");
            return false;
        }
        map.handle = handle;
        ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
        if (ret) {
            printf("drm ioctl for DRM_IOCTL_MODE_MAP_DUMB failed!\n");
            return false;        
        }
        vaddr = (uint8_t*)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
        return true;
    }
};

struct drm_output {
private:
    int drm_fd = -1;
    const char* dev_name = "/dev/dri/card0";
    drmModeConnector *drm_mode_conn = nullptr;
    drmModeRes *drm_mode_res = nullptr;
    uint32_t drm_mode_conn_id = -1;
    uint32_t drm_crtc_id = -1;
    drmModeEncoder *drm_mode_encoder = nullptr;
    uint32_t bits_per_pixel = 0;
    uint32_t image_fmt = V4L2_PIX_FMT_XRGB32;
    drm_frame_buffer drm_fb;
private:
    drm_output() = default;
    virtual ~drm_output() {
        close_drm_device();
    }
public:
    static drm_output& get_instance() {
        static drm_output instance;
        return instance;
    }
    inline void set_dev_name(const char* path) {
        dev_name = path;
    }
    inline void set_image_fmt(uint32_t fmt) {
        image_fmt = fmt;
    }
    int init() {
        if (false == open_drm_device()) {
            printf("open %s failed!\n", dev_name);
            return DRM_DEVICE_OPEN_FAILED;
        }
        if (false == drm_has_dumb())  {
            printf("device:%s has no dump!\n", dev_name);
            return DRM_HAS_NO_DUMP;
        }
        int ret = drmSetMaster(drm_fd);
        if (ret < 0) {
            printf("device:%s set master failed!\n", dev_name);
            return DRM_SET_MASTER_FAILED;
        }
        drm_mode_res = drmModeGetResources(drm_fd);
        if (nullptr == drm_mode_res) {
            printf("device:%s cannot get resource!\n", dev_name);
            return DRM_GET_RESOURCE_FAILED;
        }
        if (false == find_connector()) {
            printf("device:%s cannot get connector!\n", dev_name);
            return DRM_GET_CONNECTOR_FAILED;
        }
        if (false == find_crtc()) {
            printf("device:%s cannot get crtc id!\n", dev_name);
            return DMR_GET_CRTC_ID_FAILED;
        }
        get_bpp();
        drm_fb.fd = drm_fd;
        drm_fb.bits_per_pixel = bits_per_pixel;
        drm_fb.width = drm_mode_conn->modes[0].hdisplay;    // 800
        drm_fb.height = drm_mode_conn->modes[0].vdisplay;   // 600
        if (false == drm_fb.init()) {
            printf("device:%s init frame buffer failed!\n", dev_name);
            return DRM_FRAME_BUFFER_INIT_FAILED;
        }
        return 0;
    }
    bool draw_image(const char* file_path) {
        FILE *fp = fopen(file_path, "rb");
        static int i = 0;
        if (nullptr == fp) {
            printf("open file:%s failed!\n", file_path);
            return false;
        }
        fseek(fp, 0, SEEK_END);
        size_t file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (file_size > drm_fb.size) {
            fread(drm_fb.vaddr, drm_fb.size, 1, fp);
        }
        else {
            fread(drm_fb.vaddr, file_size, 1, fp);
        }
        fclose(fp);
        drm_fb.vaddr[i] = i % 256;
        i = (i + 1) % drm_fb.size;
        return 0 == drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb.fb_id, 0, 0, &drm_mode_conn_id, 1, &drm_mode_conn->modes[0]);
    }
    void test_drm() {
        for (int i = 0;i < 5;i++) {
            if (i % 2)  {
                memset(drm_fb.vaddr, 0x00, drm_fb.size);
            }
            else {
                memset(drm_fb.vaddr, 0xFF, drm_fb.size);
            }
            drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb.fb_id, 0, 0, &drm_mode_conn_id, 1, &drm_mode_conn->modes[0]);
            sleep(1);
        }
    }
private:
    inline void get_bpp() {
        switch(image_fmt) {
            case V4L2_PIX_FMT_XRGB32:
            case V4L2_PIX_FMT_XBGR32:
            case V4L2_PIX_FMT_ARGB32:
            case V4L2_PIX_FMT_ABGR32:
                bits_per_pixel = 32;
                break;
            case V4L2_PIX_FMT_RGB565:
            case V4L2_PIX_FMT_YUYV:
                bits_per_pixel = 16;
                break;
            default:
                bits_per_pixel = 32;
        }
    }
    inline bool open_drm_device() {
        drm_fd = open(dev_name, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        return drm_fd >= 0;
    }
    inline void close_drm_device() {
        if (drm_mode_conn != nullptr) {
            drmModeFreeConnector(drm_mode_conn);
            drm_mode_conn = nullptr;
        }
        if (drm_mode_res != nullptr) {
            drmModeFreeResources(drm_mode_res);
            drm_mode_res = nullptr;
        }
        if (drm_mode_encoder != nullptr) {
            drmModeFreeEncoder(drm_mode_encoder);
            drm_mode_encoder = nullptr;
        }
        if (drm_fd >= 0) {
            drmDropMaster(drm_fd);
            close(drm_fd);
            drm_fd = -1;
        }
    }
    inline bool drm_has_dumb() {
        uint64_t has_dumb = 0;
        return drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) >= 0 && has_dumb;
    }
    bool find_connector() {
        for (int i = 0;i < drm_mode_res->count_connectors;i++) {
            drm_mode_conn = drmModeGetConnector(drm_fd, drm_mode_res->connectors[i]);
            if (nullptr == drm_mode_conn) {
                continue;
            }
            if (drm_mode_conn->connection != DRM_MODE_CONNECTED || (0 == drm_mode_conn->count_modes)) {
                drmModeFreeConnector(drm_mode_conn);
                drm_mode_conn = nullptr;
                continue;
            }
            drm_mode_conn_id = drm_mode_conn->connector_id;
            return true;
        }
        return false;
    }
    bool find_crtc() {
        int i = 0, j = 0;
        for (i = 0;i < drm_mode_conn->count_encoders;i++) {
            drm_mode_encoder = drmModeGetEncoder(drm_fd, drm_mode_conn->encoders[i]);
            if (nullptr == drm_mode_encoder) {
                continue;
            }
            for (j = 0;j < drm_mode_res->count_crtcs;j++) {
                if (drm_mode_encoder->possible_crtcs & (1 << j)) {
                    drm_crtc_id = drm_mode_res->crtcs[j];
                    if (drm_crtc_id > 0) {
                        return true;
                    }
                }
            }
            if (j == drm_mode_res->count_crtcs) {
                drmModeFreeEncoder(drm_mode_encoder);
                drm_mode_encoder = nullptr;
            }
        }
        return false;
    }
};
#define GLOBAL_DRM_OUTPUT drm_output::get_instance()