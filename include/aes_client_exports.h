/* One ordered export list shared by the stub, image and loader resolver. */
#ifndef AES_CLIENT_EXPORTS_H
#define AES_CLIENT_EXPORTS_H
#define AES_CLIENT_EXPORT_COUNT 14
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
    X(13, aes_appl_yield)
#endif
