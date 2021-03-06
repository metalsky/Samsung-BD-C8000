/*
 *	Fusion Kernel Module
 *
 *	(c) Copyright 2002-2003  Convergence GmbH
 *
 *      Written by Denis Oliver Kropp <dok@directfb.org>
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifdef HAVE_LINUX_CONFIG_H
#include <linux/config.h>
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>

#include <linux/fusion.h>

#include "fusiondev.h"
#include "fusionee.h"
#include "list.h"
#include "reactor.h"

typedef struct {
     FusionLink         link;

     int                fusion_id;

     int                count;     /* number of attach calls */
} ReactorNode;

typedef struct {
     FusionEntry        entry;

     FusionLink        *nodes;

     int                dispatch_count;

     bool               destroyed;
} FusionReactor;

/******************************************************************************/

static int  fork_node     ( FusionReactor *reactor,
                            FusionID       fusion_id,
                            FusionID       from_id );

static void free_all_nodes( FusionReactor *reactor );

/******************************************************************************/

static inline ReactorNode *
get_node (FusionReactor *reactor,
          FusionID       fusion_id)
{
     ReactorNode *node;

     fusion_list_foreach (node, reactor->nodes) {
          if (node->fusion_id == fusion_id)
               return node;
     }

     return NULL;
}

/******************************************************************************/

static void
fusion_reactor_destruct( FusionEntry *entry,
                         void        *ctx )
{
     FusionReactor *reactor = (FusionReactor*) entry;

     free_all_nodes( reactor );
}

static int
fusion_reactor_print( FusionEntry *entry,
                      void        *ctx,
                      char        *buf )
{
     int            num     = 0;
     FusionReactor *reactor = (FusionReactor*) entry;
     FusionLink    *node    = reactor->nodes;

     fusion_list_foreach (node, reactor->nodes) {
          num++;
     }

     return sprintf( buf, "%5dx dispatch, %d nodes%s\n", reactor->dispatch_count, num,
                     reactor->destroyed ? "  DESTROYED" : "" );
}


FUSION_ENTRY_CLASS( FusionReactor, reactor, NULL,
                    fusion_reactor_destruct, fusion_reactor_print )

/******************************************************************************/

int
fusion_reactor_init (FusionDev *dev)
{
     fusion_entries_init( &dev->reactor, &reactor_class, dev );

     create_proc_read_entry( "reactors", 0, dev->proc_dir,
                             fusion_entries_read_proc, &dev->reactor );

     return 0;
}

void
fusion_reactor_deinit (FusionDev *dev)
{
     remove_proc_entry ("reactors", dev->proc_dir);

     fusion_entries_deinit( &dev->reactor );
}

/******************************************************************************/

int
fusion_reactor_new (FusionDev *dev, int *ret_id)
{
     return fusion_entry_create( &dev->reactor, ret_id, NULL );
}

int
fusion_reactor_attach (FusionDev *dev, int id, FusionID fusion_id)
{
     int            ret;
     ReactorNode   *node;
     FusionReactor *reactor;

     ret = fusion_reactor_lock( &dev->reactor, id, false, &reactor );
     if (ret)
          return ret;

     if (reactor->destroyed) {
          fusion_reactor_unlock( reactor );
          return -EIDRM;
     }

     dev->stat.reactor_attach++;

     node = get_node (reactor, fusion_id);
     if (!node) {
          node = kmalloc (sizeof(ReactorNode), GFP_KERNEL);
          if (!node) {
               fusion_reactor_unlock( reactor );
               return -ENOMEM;
          }

          node->fusion_id = fusion_id;
          node->count     = 1;

          fusion_list_prepend (&reactor->nodes, &node->link);
     }
     else
          node->count++;

     fusion_reactor_unlock( reactor );

     return 0;
}

int
fusion_reactor_detach (FusionDev *dev, int id, FusionID fusion_id)
{
     int            ret;
     ReactorNode   *node;
     FusionReactor *reactor;

     ret = fusion_reactor_lock( &dev->reactor, id, true, &reactor );
     if (ret)
          return ret;

     dev->stat.reactor_detach++;

     node = get_node (reactor, fusion_id);
     if (!node) {
          fusion_reactor_unlock( reactor );
          up( &dev->reactor.lock );
          return -EIO;
     }

     if (! --node->count) {
          fusion_list_remove (&reactor->nodes, &node->link);
          kfree (node);
     }

     if (reactor->destroyed && !reactor->nodes)
          fusion_entry_destroy_locked( &dev->reactor, &reactor->entry );
     else
          fusion_reactor_unlock( reactor );

     up( &dev->reactor.lock );

     return 0;
}

int
fusion_reactor_dispatch (FusionDev *dev, int id, Fusionee *fusionee,
                         int msg_size, const void *msg_data)
{
     int            ret;
     FusionLink    *l;
     FusionReactor *reactor;
     FusionID       fusion_id = fusionee ? fusionee_id( fusionee ) : 0;

     ret = fusion_reactor_lock( &dev->reactor, id, false, &reactor );
     if (ret)
          return ret;

     if (reactor->destroyed) {
          fusion_reactor_unlock( reactor );
          return -EIDRM;
     }

     reactor->dispatch_count++;

     fusion_list_foreach (l, reactor->nodes) {
          ReactorNode *node = (ReactorNode *) l;

          if (node->fusion_id == fusion_id)
               continue;

          fusionee_send_message (dev, fusionee, node->fusion_id, FMT_REACTOR,
                                 reactor->entry.id, msg_size, msg_data);
     }

     fusion_reactor_unlock( reactor );

     return 0;
}

int
fusion_reactor_destroy (FusionDev *dev, int id)
{
     int            ret;
     FusionReactor *reactor;

     ret = fusion_reactor_lock( &dev->reactor, id, true, &reactor );
     if (ret)
          return ret;

     if (reactor->destroyed) {
          fusion_reactor_unlock( reactor );
          up( &dev->reactor.lock );
          return -EIDRM;
     }

     reactor->destroyed = true;

     if (!reactor->nodes)
          fusion_entry_destroy_locked( &dev->reactor, &reactor->entry );
     else
          fusion_reactor_unlock( reactor );

     up( &dev->reactor.lock );

     return 0;
}

void
fusion_reactor_detach_all (FusionDev *dev, FusionID fusion_id)
{
     FusionLink *l, *n;

     down (&dev->reactor.lock);

     fusion_list_foreach_safe (l, n, dev->reactor.list) {
          ReactorNode   *node;
          FusionReactor *reactor = (FusionReactor *) l;

          down (&reactor->entry.lock);

          fusion_list_foreach (node, reactor->nodes) {
               if (node->fusion_id == fusion_id) {
                    fusion_list_remove (&reactor->nodes, &node->link);
                    kfree (node);
                    break;
               }
          }

          if (reactor->destroyed && !reactor->nodes)
               fusion_entry_destroy_locked( &dev->reactor, &reactor->entry );
          else
               up (&reactor->entry.lock);
     }

     up (&dev->reactor.lock);
}

int
fusion_reactor_fork_all (FusionDev *dev, FusionID fusion_id, FusionID from_id)
{
     FusionLink *l;
     int         ret = 0;

     down (&dev->reactor.lock);

     fusion_list_foreach (l, dev->reactor.list) {
          FusionReactor *reactor = (FusionReactor *) l;

          ret = fork_node (reactor, fusion_id, from_id);
          if (ret)
               break;
     }

     up (&dev->reactor.lock);

     return ret;
}

/******************************************************************************/

static int
fork_node (FusionReactor *reactor, FusionID fusion_id, FusionID from_id)
{
     ReactorNode *node;

     down (&reactor->entry.lock);

     fusion_list_foreach (node, reactor->nodes) {
          if (node->fusion_id == from_id) {
               ReactorNode *new_node;

               new_node = kmalloc (sizeof(ReactorNode), GFP_KERNEL);
               if (!new_node) {
                    up (&reactor->entry.lock);
                    return -ENOMEM;
               }

               new_node->fusion_id = fusion_id;
               new_node->count     = node->count;

               fusion_list_prepend (&reactor->nodes, &new_node->link);

               break;
          }
     }

     up (&reactor->entry.lock);

     return 0;
}

static void
free_all_nodes (FusionReactor *reactor)

{
     FusionLink  *n;
     ReactorNode *node;

     fusion_list_foreach_safe (node, n, reactor->nodes) {
          kfree (node);
     }

     reactor->nodes = NULL;
}
