#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#define BUFF_SIZE 80
MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

DECLARE_WAIT_QUEUE_HEAD(readQ);
DECLARE_WAIT_QUEUE_HEAD(writeQ);
struct semaphore sem;


uint fifo[16];
int el_in_fifo = 0;
int endRead = 0;

int fful=0;
int fempty=0;
int flag=0;


int fifo_open(struct inode *pinode, struct file *pfile);
int fifo_close(struct inode *pinode, struct file *pfile);
ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = fifo_open,
	.read = fifo_read,
	.write = fifo_write,
	.release = fifo_close,
};


int fifo_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened fifo\n");
		return 0;
}

int fifo_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed fifo\n");
		return 0;
}


ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	char buff[BUFF_SIZE];
	long int len = 0;
	if (endRead){
	  endRead = 0;
	  return 0;
	}

	if(down_interruptible(&sem))
	  return -ERESTARTSYS;
	
	while (el_in_fifo == 0) {
	  printk(KERN_WARNING "Usao je u red cekanja za ispis zbog el=0");    
	  up(&sem);
	  if(wait_event_interruptible(writeQ, (el_in_fifo > 0)))
	    return -ERESTARTSYS;
	  if(down_interruptible(&sem))
	    return -ERESTARTSYS;
	}

	if(flag){
	  len = scnprintf(buff, BUFF_SIZE, "%d ", fifo[fful]);
	}
	else{
	  len = scnprintf(buff, BUFF_SIZE, "%x ", fifo[fful]);
	}
	ret = copy_to_user(buffer, buff, len);
	if(ret)
	  return -EFAULT;
	printk(KERN_INFO "Succesfully read\n");
	endRead = 1;
	fful++;
	if(fful==16){
	  fful=0;
	}

	el_in_fifo--;
		
       	up(&sem);
	wake_up_interruptible(&writeQ);

	return len;
}


ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	char buff_new[BUFF_SIZE];     
	int ret,done,i,i_count=0;

	ret = copy_from_user(buff, buffer, length);
	if(ret)
	  return -EFAULT;
	
       	buff[length-1] = '\0';

	if(down_interruptible(&sem))
	  return -ERESTARTSYS;
	
       	strreplace(buff,',','\0');
	

	if(strncmp(buff,"dec\0,",4)==0){
	  flag=1;
	}
	else if(strncmp(buff,"hex\0,",4)==0){
	  flag=0;
	}
	else{
	  for(i=0;i<=length;i++){
	    i_count++;	  
	    if(buff[i]=='\0' ){
	      strncpy(buff_new,buff+i-i_count+1,i_count); 
	      done = kstrtouint(buff_new, 16, &fifo[fempty]);
	      if(done==0)
		printk(KERN_WARNING "Uspesno je upisao");
	      if(i_count>2)
		printk(KERN_WARNING "Pogresan unos broja");
	      i_count=0;
	      fempty++;	    
	      el_in_fifo++;
	      if(fempty==16){
		fempty=0;
	      }

	      while (el_in_fifo == 16) {
		printk(KERN_WARNING "Usao je u red cekanja za upis zbog el_in_fifo == 16"); 
		up(&sem);
		if(wait_event_interruptible(writeQ, (el_in_fifo < 16)))
		  return -ERESTARTSYS;
		if(down_interruptible(&sem))
		  return -ERESTARTSYS;
	      }	    
	    }
	  }
	} 	
	up(&sem);
	wake_up_interruptible(&readQ);
		
	return length;
}


static int __init fifo_init(void)
{
   int ret = 0;
	int i=0;

	sema_init(&sem,1);

	//Initialize array
	for (i=0; i<16; i++)
		fifo[i] = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fifo");
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "fifo_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, "fifo");
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   return 0;

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit fifo_exit(void)
{
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(fifo_init);
module_exit(fifo_exit);
