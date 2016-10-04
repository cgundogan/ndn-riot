/*
 * Copyright (C) 2016 Wentao Shang
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     net_ndn
 * @{
 *
 * @file
 *
 * @author  Wentao Shang <wentaoshang@gmail.com>
 */

#include "fib.h"
#include "face-table.h"
#include "encoding/name.h"

#include <debug.h>
#include <utlist.h>

#include <assert.h>
#include <stdlib.h>

static ndn_fib_entry_t _fib[NDN_FIB_ENTRIES_NUMOF];
static ndn_fib_face_entry_t _fib_faces[NDN_FIB_FACE_ENTRIES_NUMOF];

static bool _fib_entry_add_face(ndn_fib_entry_t* entry, ndn_face_entry_t *face)
{
    assert(entry != NULL);
    assert(face != NULL);

    ndn_fib_face_entry_t *tmp;
    LL_FOREACH(entry->fib_faces, tmp) {
        if (tmp->face == face) {
            DEBUG("ndn: same face exists in the fib entry\n");
            return true;
        }
    }

    for (int i = 0; i < NDN_FIB_FACE_ENTRIES_NUMOF; ++i) {
        /* search for an unused fib_face entry and add it to the fib entry */
        if (_fib_faces[i].face == NULL) {
            _fib_faces[i].face = face;
            LL_APPEND(entry->fib_faces, &_fib_faces[i]);
            return true;
        }
    }

    DEBUG("ndn: no space for new fib_face entry\n");
    return false;
}

int ndn_fib_add(ndn_shared_block_t* prefix, ndn_face_entry_t *face)
{
    assert(prefix != NULL);
    assert(face != NULL);

    int max_plen = -1;
    ndn_fib_entry_t *first_free = NULL, *entry, *max = NULL;
    bool match_found = false;
    for (int i = 0; i < NDN_FIB_ENTRIS_NUMOF; ++i) {
        entry = &_fib[i];
        if ((entry->prefix == NULL) && (first_free == NULL)) {
            first_free = entry;
            continue;
        }

        int r = ndn_name_compare_block(&prefix->block, &entry->prefix->block);
        if (r == 0) {
            // found an entry with identical name
            ndn_shared_block_release(prefix);
            // add face to fib entry
            if (!_fib_entry_add_face(entry, face)) {
                DEBUG("ndn: cannot add face %" PRIkernel_pid
                      " (type=%d) to existing fib entry\n",
                      face->id, face->type);
                return -1;
            }
            match_found = true;
        } else if (r == -2) {
            // the prefix to add is a shorter prefix of an existing prefix
            // the destination face should be added to the existing entry
            // (aka. child inherit)
            if (!_fib_entry_add_face(entry, face)) {
                DEBUG("ndn: cannot add face %" PRIkernel_pid
                      " (type=%d) to existing fib entry\n",
                      face->id, face->type);
                return -1;
            }
            // continue to check other entries
        } else if (r == 2) {
            // the existing prefix is a shorter prefix of the prefix to add
            // track the longest one of such prefixes
            if (entry->plen > max_plen) {
                max_plen = entry->plen;
                max = entry;
            }
        }
    }

    if (match_found) {
        // no need to create new entry
        return 0;
    }

    entry = first_free;

    if (entry == NULL) {
        DEBUG("ndn: cannot allocate fib entry\n");
        return -1;
    }

    entry->prefix = prefix;  // move semantics
    entry->plen = ndn_name_get_size_from_block(&prefix->block);
    entry->fib_faces = NULL;

    if (!_fib_entry_add_face(entry, face)) {
        ndn_shared_block_release(entry->prefix);
        memset(entry, 0, sizeof(*entry));
        return -1;
    }

    // inherit faces from the immediate parent (i.e., longest matching prefix)
    if (max != NULL) {
        ndn_fib_face_entry_t *tmp;
        LL_FOREACH(max->fib_faces, tmp) {
            if (!_fib_entry_add_face(entry, tmp)) {
                /* cannot inherit all faces from parent => delete fib entry */
                ndn_fib_remove(entry);
                return -1;
            }
        }
    }

    DEBUG("ndn: add new fib entry (face=%" PRIkernel_pid ","
          " face_list_size=%d)\n", face_id, entry->face_list_size);
    return 0;
}

ndn_fib_entry_t* ndn_fib_lookup(ndn_block_t* name)
{
    int max_plen = -1;
    ndn_fib_entry_t *entry, *max = NULL;
    for (int i = 0; i < NDN_FIB_ENTRIES_NUMOF; ++i) { {
        int r =
            ndn_name_compare_block(&_fib[i].prefix->block, name);
        if (r == 0 || r == -2) {
            // prefix in this entry matches the name
            if (_fib[i].plen > max_plen) {
                max_plen = _fib[i].plen;
                max = entry;
            }
        }
    }
    return max;
}

void ndn_fib_init(void)
{
    memset(&_fib, 0, sizeof(_fib));
}

void ndn_fib_remove(ndn_fib_entry_t *fib_entry)
{
    ndn_fib_face_entry_t *elt, *tmp;
    LL_FOREACH_SAFE(fib_entry->fib_faces, elt, tmp) {
        /* remove all faces from gib entry */
        LL_DELETE(fib_entry->fib_faces, elt);
        memset(elt, 0, sizeof(*elt));
    }
    /* remove fib entry */
    ndn_shared_block_release(fib_entry->prefix);
    memset(fib_entry, 0, sizeof(*fib_entry);
}

void ndn_fib_remove_face(ndn_face_entry_t *face)
{
    ndn_fib_face_entry_t *elt, *tmp;
    for (int i = 0; i < NDN_FIB_ENTRIES_NUMOF; ++i) {
        LL_FOREACH_SAFE(_fib[i].fib_faces, elt, tmp) {
            /* remove face from fib entry */
            if (elt->face == face) {
                LL_DELETE(_fib[i].fib_faces, elt);
                /* remove fib entry if no other face is available */
                if (_fib[i].fib_faces == NULL) {
                    ndn_fib_remove(&_fib[i]);
                }
            }
        }
    }
}

/** @} */
