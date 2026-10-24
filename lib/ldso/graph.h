/* Bounded userspace ELF dependency discovery (#660). */
#ifndef LDSO_GRAPH_H
#define LDSO_GRAPH_H

#include "dynamic.h"

/* One successfully acquired mapping, with a validated dl_read_dynamic view.
 * Identity is a stable mapper object identity, never a load address. The
 * mapper documents filesystem alias/replacement rules and retains immutable
 * snapshots; inode-capable filesystems should use stable file identities.
 * Its view and cookie remain valid until release, including across acquires. */
typedef struct {
    DL_OBJECT object;
    uint64_t identity;
    void *cookie;
} DL_MAPPING;

typedef struct {
    int (*acquire)(void *context, const char *name, DL_MAPPING *mapping);
    void (*release)(void *context, const DL_MAPPING *mapping);
} DL_GRAPH_OPS;

typedef struct {
    DL_MAPPING mapping;
    const char *name;
    uint16_t edges[DL_MAX_NEEDED];
} DL_GRAPH_NODE;

typedef struct {
    DL_GRAPH_NODE nodes[DL_MAX_OBJECTS];
    const DL_OBJECT *scope[DL_MAX_OBJECTS];
    uint16_t order[DL_MAX_OBJECTS];
    uint32_t count, order_count;
} DL_GRAPH;

/* Build the executable-first breadth-first scope, in DT_NEEDED order.
 * graph must be fresh or closed; its storage must remain at a stable address.
 * root is borrowed, never released. Dependencies are acquired once by exact,
 * case-sensitive bare name. Names containing path separators, colon, control
 * bytes, "." or ".." are rejected; searching the system directory belongs to
 * acquire, never the current directory or environment. A dependency SONAME,
 * when present, must match the requested name exactly. Path/unsupported
 * feature tags are refused before discovering that object's children.
 *
 * Successful acquires transfer a reference, including duplicate identities;
 * release drops each such reference exactly once. On acquire failure the
 * callback cleans up its own partial work. Any failure unwinds all acquired
 * references and leaves count/order_count zero, without releasing root.
 *
 * order is deterministic DFS postorder: visit children in DT_NEEDED order,
 * skip edges to active ancestors to break cycles, append each object once.
 * It is an initialization schedule, not a constructor executor. No callback
 * invokes user code or relocates an object during discovery.
 */
int dl_graph_open(DL_GRAPH *graph, const DL_MAPPING *root,
                    const DL_GRAPH_OPS *ops, void *context);
void dl_graph_close(DL_GRAPH *graph, const DL_GRAPH_OPS *ops, void *context);

#endif
