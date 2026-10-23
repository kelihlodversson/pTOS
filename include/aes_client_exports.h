/* One ordered export list shared by the stub, image and loader resolver. */
#ifndef AES_CLIENT_EXPORTS_H
#define AES_CLIENT_EXPORTS_H
#define AES_CLIENT_EXPORT_COUNT 30
#define AES_CLIENT_EXPORTS(X) \
    X(0, aes_appl_init) \
    X(1, aes_appl_exit) \
    X(2, aes_appl_find) \
    X(3, aes_appl_write) \
    X(4, aes_appl_read) \
    X(5, aes_evnt_poll) \
    X(6, aes_wind_update) \
    X(7, inside) \
    X(8, rc_constrain) \
    X(9, rc_equal) \
    X(10, rc_intersect) \
    X(11, rc_union) \
    X(12, aes_evnt_wait) \
    X(13, aes_appl_yield) \
    X(14, aes_evnt_multi) \
    X(15, aes_wind_create) \
    X(16, aes_wind_open) \
    X(17, aes_wind_close) \
    X(18, aes_wind_delete) \
    X(19, aes_wind_get) \
    X(20, aes_wind_set_rect) \
    X(21, aes_wind_set_text) \
    X(22, aes_wind_top) \
    X(23, aes_wind_slider) \
    X(24, aes_wind_rect) \
    X(25, aes_wind_calc) \
    X(26, aes_wind_find) \
    X(27, aes_object) \
    X(28, aes_form_do) \
    X(29, aes_tree_validate)
#endif
