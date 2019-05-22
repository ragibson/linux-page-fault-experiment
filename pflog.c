#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "pflog.h" // shared module-user program information

int file_value;
struct dentry *dir, *file; // used to set up debugfs file name

struct vma_info {
  struct vm_area_struct *vma;
  int (*fault)(struct vm_area_struct *vma, struct vm_fault *vmf);
};

static struct vma_info *vma_storage[MAX_VMA];
static struct vm_operations_struct pflog_vm_ops;
static int ownerPID = 0;

static int fault_pflog(struct vm_area_struct *vma, struct vm_fault *vmf) {
  int i, rc;
  u64 start, elapsed;

  // locate the relevant VMA
  for (i = 0; i < MAX_VMA && vma_storage[i] != NULL; i++) {
    if (vma == vma_storage[i]->vma) {
      break;
    }
  }
  // could not find relevant VMA
  if (i == MAX_VMA || vma_storage[i] == NULL) {
    printk(KERN_DEBUG "pflog: returning VM_FAULT_SIGBUS!\n");
    return VM_FAULT_SIGBUS;
  }

  start = ktime_to_us(ktime_get());
  rc = vma_storage[i]->fault(vma, vmf);
  elapsed = ktime_to_us(ktime_get()) - start;

  // log page fault data
  if (vmf != NULL) {
    printk(KERN_DEBUG "pflog: %p %lu %lu %lu %llu %d\n", vma,
           ((unsigned long)(vmf->virtual_address)) >> 12, vmf->pgoff,
           page_to_pfn(vmf->page), elapsed, vmf->page == NULL);
  } else {
    printk(KERN_DEBUG "pflog: unknown page faulted!\n");
  }
  return rc;
}

// Emulates the handling of a blocking system call through debugfs
// buf points to a user space buffer with maximum size count.
static ssize_t pflog_call(struct file *file, const char __user *buf,
                          size_t count, loff_t *ppos) {
  char callbuf[MAX_CALL];
  struct vm_area_struct *vma;
  int i = 0;

  // set the owner of this module on first call
  if (ownerPID == 0) {
    ownerPID = current->pid;
  } else if (ownerPID != current->pid) {
    // reject all calls from non-owners
    return -1;
  }

  copy_from_user(callbuf, buf, count); // move from user to kernel space
  callbuf[MAX_CALL - 1] = '\0';        /* null-terminate string */

  if (count >= MAX_CALL || current->mm == NULL || current->mm->mmap == NULL ||
      strcmp(callbuf, "log_faults") != 0) {
    return -1;
  }

  down_read(&current->mm->mmap_sem);

  vma = current->mm->mmap;
  while (vma->vm_next != NULL && i < MAX_VMA) {
    if (vma->vm_ops != NULL && vma->vm_ops->fault != NULL) {
      // store identifying VMA information
      if ((vma_storage[i] = kmalloc(sizeof(struct vma_info), GFP_ATOMIC)) ==
          NULL) {
        return -1;
      }
      vma_storage[i]->vma = vma;
      vma_storage[i]->fault = vma->vm_ops->fault;

      // override this VMA's fault function
      memcpy(&pflog_vm_ops, vma->vm_ops, sizeof(pflog_vm_ops));
      pflog_vm_ops.fault = fault_pflog;
      vma->vm_ops = &pflog_vm_ops;

      printk(KERN_DEBUG "pflog: stored VMA %d\n", i);
      i++;
    }
    vma = vma->vm_next;
  }

  up_read(&current->mm->mmap_sem);

  *ppos = 0; /* reset the offset to zero */
  return 0;
}

// we never return anything -- this is a NOOP
static ssize_t pflog_return(struct file *file, char __user *userbuf,
                            size_t count, loff_t *ppos) {
  return 0;
}

// redirect debugfs read() and write() to our functions
static const struct file_operations my_fops = {
    .read = pflog_return,
    .write = pflog_call,
};

// creates debugfs user-kernel communication channel
static int __init pflog_module_init(void) {
  // create directory
  dir = debugfs_create_dir(dir_name, NULL);
  if (dir == NULL) {
    return -ENODEV;
  }

  // create file and give everyone rw access
  file = debugfs_create_file(file_name, 0666, dir, &file_value, &my_fops);
  if (file == NULL) {
    return -ENODEV;
  }

  return 0;
}

// clean up when module is removed
static void __exit pflog_module_exit(void) {
  int i;
  for (i = 0; i < MAX_VMA && vma_storage[i] != NULL; i++) {
    kfree(vma_storage[i]);
  }

  debugfs_remove(file);
  debugfs_remove(dir);
}

/* Declarations required in building a module */
module_init(pflog_module_init);
module_exit(pflog_module_exit);
MODULE_LICENSE("GPL");
