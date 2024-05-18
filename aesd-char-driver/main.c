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
#include "aesdchar.h"
#include "aesd_ioctl.h"

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
    size_t data_offset;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;

    if (mutex_lock_interruptible(&dev->lock)) {
        PDEBUG("FAILED %s to acquire lock", "read");
        return -ERESTARTSYS;
    }

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &data_offset);
    if (entry) {
        retval = entry->size - data_offset;
        if (copy_to_user(buf, entry->buffptr + data_offset, retval)) {
            retval = -EFAULT;
            goto on_exit;
        }        
        *f_pos += retval;
    }

    on_exit:
        mutex_unlock(&dev->lock);
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
    const char* old_data;
    size_t data_ind, i;
    struct aesd_buffer_entry entry;
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
 
    memset(&entry, 0, sizeof(struct aesd_buffer_entry));

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    PDEBUG("CB with size %ld", dev->circular_buffer.size);

    if (mutex_lock_interruptible(&dev->lock)) {
        PDEBUG("FAILED %s to acquire lock", "write");
        return -ERESTARTSYS;
    }

    data = kmalloc(count, GFP_KERNEL);

    if (copy_from_user(data, buf, count)) {
        retval = -EFAULT;
        kfree(data);
        goto on_exit;
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
        old_data = aesd_circular_buffer_add_entry(&dev->circular_buffer, &entry);
        if (old_data) {
            kfree(old_data);
        }
    }

    on_exit:
        mutex_unlock(&dev->lock);
        *f_pos = 0;
        return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    loff_t retval;
    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock)) {
        PDEBUG("FAILED %s to acquire lock", "llseek");
        return -ERESTARTSYS;
    }

    retval = fixed_size_llseek(filp, offset, whence, dev->circular_buffer.size);
    mutex_unlock(&dev->lock);

    return retval;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch (cmd)
    {
    case AESDCHAR_IOCSEEKTO:
    {
        struct aesd_seekto seekto;
        long long pos;
        long retval = 0;
        struct aesd_dev *dev = filp->private_data;

        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
            return -EFAULT;
        }

        if (mutex_lock_interruptible(&dev->lock)) {
            return -ERESTARTSYS;
        }

        pos = aesd_circular_buffer_jmp_entry_offset(&dev->circular_buffer, seekto.write_cmd, seekto.write_cmd_offset);
        if (pos < 0) {
            retval = pos;
        } else {
            filp->f_pos = pos;
        }

        mutex_unlock(&dev->lock);
        return retval;
    }
    default:
        return -ENOTTY;
    }
}

struct file_operations aesd_fops = {
    .owner =            THIS_MODULE,
    .read =             aesd_read,
    .write =            aesd_write,
    .open =             aesd_open,
    .release =          aesd_release,
    .llseek =           aesd_llseek,
    .unlocked_ioctl =   aesd_unlocked_ioctl,
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
    mutex_init(&(aesd_device.lock));

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