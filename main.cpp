#include "drm_output.hpp"
int main() {
    int ret = GLOBAL_DRM_OUTPUT.init();
    if (ret) {
        printf("drm init failed, error code:%d\n", ret);
        return -1;
    }
    GLOBAL_DRM_OUTPUT.test_drm();
    getchar();
    
    return 0;
}