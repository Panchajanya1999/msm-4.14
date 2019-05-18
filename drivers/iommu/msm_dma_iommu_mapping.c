// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <asm/barrier.h>

/**
 * struct msm_iommu_map - represents a mapping of an ion buffer to an iommu
 * @lnode - List node to exist in the buffer's list of iommu mappings.
 * @dev - Device this is mapped to. Used as key.
 * @sgl - The scatterlist for this mapping.
 * @nents - Number of entries in sgl.
 * @dir - The direction for the map.
 * @meta - Backpointer to the meta this guy belongs to.
 * @ref - For reference counting this mapping.
 * @map_attrs - dma mapping attributes
 * @buf_start_addr - address of start of buffer
 *
 * Represents a mapping of one dma_buf buffer to a particular device and address
 * range. There may exist other mappings of this buffer in different devices.
 * All mappings have the same cacheability and security.
 */
struct msm_iommu_map {
	struct list_head lnode;
	struct device *dev;
	struct scatterlist *sgl;
	unsigned int nents;
	enum dma_data_direction dir;
	struct msm_iommu_meta *meta;
	struct kref ref;
	unsigned long map_attrs;
	dma_addr_t buf_start_addr;
};

struct msm_iommu_meta {
	struct rb_node node;
	struct list_head maps;
	struct kref ref;
	rwlock_t lock;
	void *buffer;
};

static struct rb_root iommu_root;
static DEFINE_RWLOCK(rb_tree_lock);

static struct scatterlist *clone_sgl(struct scatterlist *sg, int nents)
{
	struct scatterlist *next, *s;
	int i;
	struct sg_table table;

	if (sg_alloc_table(&table, nents, GFP_KERNEL))
		return NULL;
	next = table.sgl;
	for_each_sg(sg, s, nents, i) {
		*next = *s;
		next = sg_next(next);
	}
	return table.sgl;
}

static void msm_iommu_meta_add(struct msm_iommu_meta *meta)
{
	struct rb_root *root = &iommu_root;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct msm_iommu_meta *entry;

	write_lock(&rb_tree_lock);
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, typeof(*entry), node);
		if (meta->buffer < entry->buffer)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&meta->node, parent, p);
	rb_insert_color(&meta->node, root);
	write_unlock(&rb_tree_lock);
}

static struct msm_iommu_meta *msm_iommu_meta_lookup_get(void *buffer)
{
	struct rb_root *root = &iommu_root;
	struct rb_node **p = &root->rb_node;
	struct msm_iommu_meta *entry;

	read_lock(&rb_tree_lock);
	while (*p) {
		entry = rb_entry(*p, typeof(*entry), node);
		if (buffer < entry->buffer) {
			p = &(*p)->rb_left;
		} else if (buffer > entry->buffer) {
			p = &(*p)->rb_right;
		} else {
			kref_get(&entry->ref);
			read_unlock(&rb_tree_lock);
			return entry;
		}
	}
	read_unlock(&rb_tree_lock);

	return NULL;
}

static void msm_iommu_add(struct msm_iommu_meta *meta,
			  struct msm_iommu_map *map)
{
	write_lock(&meta->lock);
	list_add(&map->lnode, &meta->maps);
	write_unlock(&meta->lock);
}

static struct msm_iommu_map *msm_iommu_lookup_get(struct msm_iommu_meta *meta,
						  struct device *dev)
{
	struct msm_iommu_map *entry;

	read_lock(&meta->lock);
	list_for_each_entry(entry, &meta->maps, lnode) {
		if (entry->dev == dev) {
			kref_get(&entry->ref);
			read_unlock(&meta->lock);
			return entry;
		}
	}
	read_unlock(&meta->lock);

	return NULL;
}

static void msm_iommu_meta_destroy(struct kref *kref)
{
	struct msm_iommu_meta *meta = container_of(kref, typeof(*meta), ref);
	struct rb_root *root = &iommu_root;

	write_lock(&rb_tree_lock);
	rb_erase(&meta->node, root);
	write_unlock(&rb_tree_lock);

	kfree(meta);
}

static void msm_iommu_map_destroy(struct kref *kref)
{
	struct msm_iommu_map *map = container_of(kref, typeof(*map), ref);
	struct msm_iommu_meta *meta = map->meta;
	struct sg_table table;

	table.nents = table.orig_nents = map->nents;
	table.sgl = map->sgl;

	write_lock(&meta->lock);
	list_del(&map->lnode);
	write_unlock(&meta->lock);

	/* Skip an additional cache maintenance on the dma unmap path */
	if (!(map->map_attrs & DMA_ATTR_SKIP_CPU_SYNC))
		map->map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	dma_unmap_sg_attrs(map->dev, map->sgl, map->nents, map->dir,
			map->map_attrs);
	sg_free_table(&table);
	kfree(map);
}

static void msm_iommu_map_destroy_noop(struct kref *kref)
{
	/* For when we need to unmap on our own terms */
}

static struct msm_iommu_meta *msm_iommu_meta_create(struct dma_buf *dma_buf)
{
	struct msm_iommu_meta *meta;

	meta = kmalloc(sizeof(*meta), GFP_KERNEL);
	if (!meta)
		return NULL;

	meta->buffer = dma_buf->priv;
	kref_init(&meta->ref);
	rwlock_init(&meta->lock);
	INIT_LIST_HEAD(&meta->maps);
	msm_iommu_meta_add(meta);

	return meta;
}

static int __msm_dma_map_sg(struct device *dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction dir,
			    struct dma_buf *dma_buf, unsigned long attrs)
{
	bool late_unmap = !(attrs & DMA_ATTR_NO_DELAYED_UNMAP);
	bool extra_meta_ref_taken = false;
	struct msm_iommu_meta *meta;
	struct msm_iommu_map *map;
	int ret;

	meta = msm_iommu_meta_lookup_get(dma_buf->priv);
	if (!meta) {
		meta = msm_iommu_meta_create(dma_buf);
		if (!meta)
			return -ENOMEM;

		if (late_unmap) {
			kref_get(&meta->ref);
			extra_meta_ref_taken = true;
		}
	}

	map = msm_iommu_lookup_get(meta, dev);
	if (map) {
		if (nents == map->nents &&
		    dir == map->dir &&
		    attrs == map->map_attrs &&
		    sg_phys(sg) == map->buf_start_addr) {
			struct scatterlist *sg_tmp = sg;
			struct scatterlist *map_sg;
			int i;
			for_each_sg(map->sgl, map_sg, nents, i) {
				sg_dma_address(sg_tmp) = sg_dma_address(map_sg);
				sg_dma_len(sg_tmp) = sg_dma_len(map_sg);
				if (sg_dma_len(map_sg) == 0)
					break;

				sg_tmp = sg_next(sg_tmp);
				if (sg_tmp == NULL)
					break;
			}

			if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
				dma_sync_sg_for_device(dev, map->sgl,
						map->nents, map->dir);

			/*
			 * Ensure all outstanding changes for coherent buffers
			 * are applied to the cache before any DMA occurs.
			 */
			if (is_device_dma_coherent(dev))
				dmb(ish);
			ret = nents;
		} else {
			bool start_diff = sg_phys(sg) != map->buf_start_addr;

			dev_err(dev, "lazy map request differs:\n"
				"req dir:%d, original dir:%d\n"
				"req nents:%d, original nents:%d\n"
				"req map attrs:%lu, original map attrs:%lu\n"
				"req buffer start address differs:%d\n",
				dir, map->dir, nents, map->nents, attrs,
				map->map_attrs, start_diff);
			ret = -EINVAL;
			goto release_meta;
		}
	} else {
		map = kmalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			ret = -ENOMEM;
			goto release_meta;
		}

		ret = dma_map_sg_attrs(dev, sg, nents, dir, attrs);
		if (!ret) {
			kfree(map);
			goto release_meta;
		}

		map->sgl = clone_sgl(sg, nents);
		if (!map->sgl) {
			kfree(map);
			ret = -ENOMEM;
			goto release_meta;
		}
		map->nents = nents;
		map->dev = dev;

		kref_init(&map->ref);
		if (late_unmap)
			kref_get(&map->ref);

		map->meta = meta;
		map->dir = dir;
		map->nents = nents;
		map->map_attrs = attrs;
		map->buf_start_addr = sg_phys(sg);
		INIT_LIST_HEAD(&map->lnode);
		msm_iommu_add(meta, map);
	}

	return nents;

release_meta:
	if (extra_meta_ref_taken)
		kref_put(&meta->ref, msm_iommu_meta_destroy);
	kref_put(&meta->ref, msm_iommu_meta_destroy);
	return ret;
}

/*
 * We are not taking a reference to the dma_buf here. It is expected that
 * clients hold reference to the dma_buf until they are done with mapping and
 * unmapping.
 */
int msm_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
			 enum dma_data_direction dir, struct dma_buf *dma_buf,
			 unsigned long attrs)
{
	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: dev pointer is invalid\n", __func__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(sg)) {
		pr_err("%s: sg table pointer is invalid\n", __func__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(dma_buf)) {
		pr_err("%s: dma_buf pointer is invalid\n", __func__);
		return -EINVAL;
	}

	return __msm_dma_map_sg(dev, sg, nents, dir, dma_buf, attrs);
}
EXPORT_SYMBOL(msm_dma_map_sg_attrs);

void msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl, int nents,
		      enum dma_data_direction dir, struct dma_buf *dma_buf, unsigned long attrs)
{
	struct msm_iommu_meta *meta;
	struct msm_iommu_map *map;

	meta = msm_iommu_meta_lookup_get(dma_buf->priv);
	if (!meta)
		return;

	map = msm_iommu_lookup_get(meta, dev);
	if (!map) {
		kref_put(&meta->ref, msm_iommu_meta_destroy);
		return;
	}

	if (dir != map->dir)
		WARN(1, "%s: (%pK) dir:%d differs from original dir:%d\n",
		     __func__, dma_buf, dir, map->dir);

	if (attrs && ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0))
		dma_sync_sg_for_cpu(dev, map->sgl, map->nents, dir);

	map->map_attrs = attrs;

	/* Do an extra put to undo msm_iommu_lookup_get */
	kref_put(&map->ref, msm_iommu_map_destroy);
	kref_put(&map->ref, msm_iommu_map_destroy);

	/* Do an extra put to undo msm_iommu_meta_lookup_get */
	kref_put(&meta->ref, msm_iommu_meta_destroy);
	kref_put(&meta->ref, msm_iommu_meta_destroy);
}
EXPORT_SYMBOL(msm_dma_unmap_sg);

int msm_dma_unmap_all_for_dev(struct device *dev)
{
	struct msm_iommu_map *map, *map_next;
	struct rb_root *root = &iommu_root;
	struct msm_iommu_meta *meta;
	struct rb_node *meta_node;
	LIST_HEAD(unmap_list);
	int ret = 0;

	read_lock(&rb_tree_lock);
	meta_node = rb_first(root);
	while (meta_node) {
		meta = rb_entry(meta_node, typeof(*meta), node);
		write_lock(&meta->lock);
		list_for_each_entry_safe(map, map_next, &meta->maps, lnode) {
			if (map->dev != dev)
				continue;

			/* Do the actual unmapping outside of the locks */
			if (kref_put(&map->ref, msm_iommu_map_destroy_noop))
				list_move_tail(&map->lnode, &unmap_list);
			else
				ret = -EINVAL;
		}
		write_unlock(&meta->lock);
		meta_node = rb_next(meta_node);
	}
	read_unlock(&rb_tree_lock);

	list_for_each_entry_safe(map, map_next, &unmap_list, lnode) {
		dma_unmap_sg(map->dev, map->sgl, map->nents, map->dir);
		kfree(map);
	}

	return ret;
}

/* Only to be called by ION code when a buffer is freed */
void msm_dma_buf_freed(void *buffer)
{
	struct msm_iommu_map *map, *map_next;
	struct msm_iommu_meta *meta;
	LIST_HEAD(unmap_list);

	meta = msm_iommu_meta_lookup_get(buffer);
	if (!meta)
		return;

	write_lock(&meta->lock);
	list_for_each_entry_safe(map, map_next, &meta->maps, lnode) {
		/* Do the actual unmapping outside of the lock */
		if (kref_put(&map->ref, msm_iommu_map_destroy_noop))
			list_move_tail(&map->lnode, &unmap_list);
	}
	write_unlock(&meta->lock);

	list_for_each_entry_safe(map, map_next, &unmap_list, lnode) {
		dma_unmap_sg(map->dev, map->sgl, map->nents, map->dir);
		kfree(map);
	}

	/* Do an extra put to undo msm_iommu_meta_lookup_get */
	kref_put(&meta->ref, msm_iommu_meta_destroy);
	kref_put(&meta->ref, msm_iommu_meta_destroy);
}
