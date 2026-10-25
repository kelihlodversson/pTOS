/*
 * graph.c - bounded dependency discovery, scope and lifetime (#660)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "graph.h"

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int bare_name(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;

    if (!p || !*p || equal(name, ".") || equal(name, ".."))
        return 0;
    while (*p) {
        if (*p < 32 || *p == 127 || *p == '/' || *p == '\\' || *p == ':')
            return 0;
        p++;
    }
    return 1;
}

/* Alias requests do not necessarily equal a retained object's first name.
 * Reuse completed edges as the bounded name cache; strings remain owned by
 * the corresponding parent mapping until close. */
static uint32_t find_name(const DL_GRAPH *g, const char *name,
                          uint32_t node, uint32_t edge)
{
    uint32_t i, j;

    for (i = 0; i < g->count; i++)
        if (g->nodes[i].name && equal(g->nodes[i].name, name))
            return i;
    for (i = 0; i <= node; i++) {
        uint32_t limit = i == node ? edge : g->nodes[i].mapping.object.needed_count;

        for (j = 0; j < limit; j++) {
            const char *old = dl_string(g->scope[i], g->scope[i]->needed[j]);

            if (old && equal(old, name))
                return g->nodes[i].edges[j];
        }
    }
    return g->count;
}

static int admit(const DL_OBJECT *o)
{
    if (!o->image || o->needed_count > DL_MAX_NEEDED)
        return DL_BAD_ELF;
    if (o->features || (o->flags & ~8U) || (o->flags1 & ~0x08000001U))
        return DL_UNSUPPORTED;
    return DL_OK;
}

void dl_graph_close(DL_GRAPH *g, const DL_GRAPH_OPS *ops, void *context)
{
    while (g->count > 1) {
        g->count--;
        ops->release(context, &g->nodes[g->count].mapping);
        g->scope[g->count] = 0;
    }
    g->scope[0] = 0;
    g->count = g->order_count = 0;
}

/* Iterative DFS avoids recursive stack growth on long chains or cycles. */
static void schedule(DL_GRAPH *g)
{
    unsigned char state[DL_MAX_OBJECTS] = { 0 };
    uint16_t stack[DL_MAX_OBJECTS], next[DL_MAX_OBJECTS] = { 0 };
    uint32_t depth = 1;

    stack[0] = 0;
    state[0] = 1;
    while (depth) {
        uint32_t node = stack[depth - 1];

        if (next[node] < g->nodes[node].mapping.object.needed_count) {
            uint32_t child = g->nodes[node].edges[next[node]++];

            if (!state[child]) {
                state[child] = 1;
                stack[depth++] = child;
            }
        } else {
            state[node] = 2;
            g->order[g->order_count++] = node;
            depth--;
        }
    }
}

int dl_graph_open(DL_GRAPH *g, const DL_MAPPING *root,
                    const DL_GRAPH_OPS *ops, void *context)
{
    uint32_t node, edge, i;
    int rc;

    if (!g)
        return DL_BAD_ELF;
    g->count = g->order_count = 0;
    if (!root || !ops || !ops->acquire || !ops->release)
        return DL_BAD_ELF;
    rc = admit(&root->object);
    if (rc)
        return rc;
    g->nodes[0].mapping = *root;
    g->nodes[0].name = root->object.has_soname ?
                        dl_string(&root->object, root->object.soname) : 0;
    g->scope[0] = &g->nodes[0].mapping.object;
    g->count = 1;
    /* Appending as each object's children are visited gives BFS scope. */
    for (node = 0; node < g->count; node++) {
        const DL_OBJECT *o = g->scope[node];

        for (edge = 0; edge < o->needed_count; edge++) {
            const char *name = dl_string(o, o->needed[edge]);
            DL_MAPPING acquired;

            if (!bare_name(name)) {
                rc = DL_BAD_ELF;
                goto fail;
            }
            i = find_name(g, name, node, edge);
            if (i == g->count) {
                if (g->count == DL_MAX_OBJECTS) {
                    rc = DL_NO_SPACE;
                    goto fail;
                }
                rc = ops->acquire(context, name, &acquired);
                if (rc)
                    goto fail;
                rc = admit(&acquired.object);
                if (!rc && acquired.object.has_soname) {
                    const char *soname = dl_string(&acquired.object, acquired.object.soname);

                    if (!soname || !equal(soname, name))
                        rc = DL_BAD_ELF;
                }
                if (rc) {
                    ops->release(context, &acquired);
                    goto fail;
                }
                /* Aliases without SONAME may refer to the same file. Drop
                 * the extra mapping reference and retain the original view. */
                for (i = 0; i < g->count; i++)
                    if (g->nodes[i].mapping.identity == acquired.identity)
                        break;
                if (i != g->count) {
                    ops->release(context, &acquired);
                } else {
                    g->nodes[i].mapping = acquired;
                    g->nodes[i].name = name;
                    g->scope[i] = &g->nodes[i].mapping.object;
                    g->count++;
                }
            }
            g->nodes[node].edges[edge] = i;
        }
    }
    schedule(g);
    return DL_OK;
fail:
    dl_graph_close(g, ops, context);
    return rc;
}
