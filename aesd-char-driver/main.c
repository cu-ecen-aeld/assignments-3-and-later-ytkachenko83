/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h> // kmalloc, kfree
#include <linux/uaccess.h> // copy_*_user
#include <linux/spinlock.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Yuri Tkachenko");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

#define NEWLINE '\n'
#define END_OF_STR '\0'

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    size_t data_offset, index;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;

    read_lock(&dev->lock);

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->circular_buffer, index) {
        PDEBUG("Record[%zu], size: %zu = %s", index, entry->size, entry->buffptr);
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &data_offset);
    if (entry) {
        retval = entry->size - data_offset;
        if (copy_to_user(buf, entry->buffptr + data_offset, retval)) {
            retval = -EFAULT;
            PDEBUG("Exitting with %ld code.", retval);
            goto on_exit;
        }
        if (retval == 0) {
            *f_pos = 0;
        } else {
            *f_pos += entry->size - data_offset;
        }
        PDEBUG("Entry {retval:%zu, data_offset:%zu, f_pos:%lld}", retval, data_offset, *f_pos);
    }

    PDEBUG("retval => %zu", retval);

    on_exit:
    read_unlock(&dev->lock);

    return retval;
}

/*
* return 1 - found, 0 - no
*/
int find_char(const char *data, char ch, size_t *index, size_t size) {
    size_t pos;

    for (pos = 0; pos < size || data[pos] != END_OF_STR; pos++) {
        if (data[pos] == ch) break;
    }

    if (pos < size) {
        *index = pos;
        return 1;
    } else {
        return 0;
    }
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    char *data;
    size_t data_ind, i;
    struct aesd_buffer_entry entry;
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
 
    write_lock(&dev->lock);
    memset(&entry, 0, sizeof(struct aesd_buffer_entry));

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    data = kmalloc(count, GFP_KERNEL);

    if (copy_from_user(data, buf, count)) {
        retval = -EFAULT;
        goto on_cleanup;
    }

    if (!find_char(data, NEWLINE, &data_ind, count)) {
        data_ind = count - 1; // not found, copy everything
    }

    dev->tmpbuf = krealloc(dev->tmpbuf, dev->tmpbuf_size + data_ind + 1, GFP_KERNEL);
    for (i = 0; i <= data_ind; i++) {
        dev->tmpbuf[dev->tmpbuf_size++] = data[i];
    }
    kfree(data);

    retval = data_ind + 1;

    if (dev->tmpbuf[dev->tmpbuf_size-1] == NEWLINE) {
        entry.buffptr = dev->tmpbuf;
        entry.size = dev->tmpbuf_size;
        dev->tmpbuf = NULL;
        dev->tmpbuf_size = 0;
        // push entry
        data = aesd_circular_buffer_add_entry(&dev->circular_buffer, &entry);
        if (data) {
            goto on_cleanup;
        }
    }

    goto on_ok;

    on_cleanup:
        kfree(data);
    on_ok:
        *f_pos = 0;
        write_unlock(&dev->lock);    
        return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    rwlock_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        kfree(entry->buffptr);
    }
    if (aesd_device.tmpbuf) {
        kfree(aesd_device.tmpbuf);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);