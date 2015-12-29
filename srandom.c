#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>		/* For kalloc */
#include <asm/uaccess.h>        /* For copy_to_user */
#include <linux/miscdevice.h>   /* For misc_register (the /dev/srandom) device */
#include <linux/time.h>         /* For getnstimeofday */
#include <linux/proc_fs.h>      /* For /proc filesystem */
#include <linux/seq_file.h>	/* For seq_print */
#include <linux/mutex.h>

#define DRIVER_AUTHOR "Jonathan Senkerik <josenk@jintegrate.co>" 
#define DRIVER_DESC   "Improved random number generator."
#define arr_RND_SIZE 67         // Size of Array
#define num_arr_RND  16         // Number of 512b Array (Must be power of 2)
#define sDEVICE_NAME "srandom"	// Dev name as it appears in /proc/devices
#define AppVERSION "1.30"
#define PAID 0
#define SUCCESS 0


/*
Copyright (C) 2015 Jonathan Senkerik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*  Prototypes */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t sdevice_read(struct file *, char *, size_t, loff_t *);
static ssize_t sdevice_write(struct file *, const char *, size_t, loff_t *);
static uint64_t xorshft64(void);
static uint64_t xorshft128(void);
static void update_sarray(int);
static void seed_PRND(void);
static int proc_read(struct seq_file *m, void *v);
static int proc_open(struct inode *inode, struct  file *file);


/* Global variables are declared as static, so are global within the file.  */
static struct file_operations sfops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.read = sdevice_read,
	.write = sdevice_write,
	.release = device_release
};

static struct miscdevice srandom_dev = {
	MISC_DYNAMIC_MINOR,
	"srandom",
	&sfops
};

static const struct file_operations proc_fops = {
	.owner = THIS_MODULE,
	.read =  seq_read,
	.open =  proc_open,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct mutex UpArr_mutex;
static struct mutex Open_mutex;


// Global variables
uint64_t x;                         // Used for xorshft64
uint64_t s[ 2 ];                    // Used for xorshft128
uint64_t (*sarr_RND)[num_arr_RND];  // Array of Array of SECURE RND numbers
uint64_t tm_seed;
struct timespec ts;

// Global counters
int16_t   sdev_open;                // srandom device current open count`.
int32_t   sdev_openCount;           // srandom device total open count`.
uint64_t  PRNGCount;                // Total generated (512byte)



/*  This function is called when the module is loaded */
int mod_init(void)
{
  int16_t C,CC;
  int ret;

  // Init variables
  sdev_open=0;
  sdev_openCount=0;
  PRNGCount=0;

  mutex_init(&UpArr_mutex);
  mutex_init(&Open_mutex);

  //Entropy Initialize #1
  getnstimeofday(&ts);
  x=(uint64_t)ts.tv_nsec;
  s[0]=xorshft64();
  s[1]=xorshft64();

  // Register char device
  ret = misc_register(&srandom_dev);
  if (ret) {
    printk(KERN_INFO "/dev/srandom driver registion failed..\n");
  } else {
    printk(KERN_INFO "/dev/srandom driver registered..\n");
  }

  // Create /proc/srandom
  if (! proc_create("srandom",0,NULL,&proc_fops)) {
    printk(KERN_INFO "/proc/srandom registion failed..\n");
  } else {
    printk(KERN_INFO "/proc/srandom registion regisered..\n");
  }

  printk(KERN_INFO "Module version         : "AppVERSION"\n");
  if ( PAID==0){
    printk(KERN_INFO "-----------------------:----------------------\n");
    printk(KERN_INFO "Please support my work and efforts contributing\n");
    printk(KERN_INFO "to the Linux community.  A $25 payment per\n");
    printk(KERN_INFO "server would be highly appreciated.\n");
  }
  printk(KERN_INFO "-----------------------:----------------------\n");
  printk(KERN_INFO "Author                 : Jonathan Senkerik\n");
  printk(KERN_INFO "Website                : http://www.jintegrate.co\n");
  printk(KERN_INFO "github                 : http://github.com/josenk/srandom\n");
  if ( PAID==0){
    printk(KERN_INFO "Paypal                 : josenk@jintegrate.co\n");
    printk(KERN_INFO "Bitcoin                : 1MTNg7SqcEWs5uwLKwNiAfYqBfnKFJu65p\n");
    printk(KERN_INFO "Commercial Invoice     : Avail on request.\n");
  }


  sarr_RND = kmalloc(num_arr_RND * arr_RND_SIZE * sizeof(uint64_t), __GFP_WAIT | __GFP_IO | __GFP_FS);

  //Entropy Initialize #2
  getnstimeofday(&ts);
  x=(x<<32) ^ (uint64_t)ts.tv_nsec;
  seed_PRND();

  //  Init the sarray
  for (CC=0;CC<num_arr_RND;CC++) {
    for (C=0;C<=arr_RND_SIZE;C++){
     sarr_RND[CC][C] = xorshft128();
    }
    update_sarray(CC);
  }

  return SUCCESS;
}

/* This function is called when the module is unloaded */
void mod_exit(void)
{
  int ret;

  ret = misc_deregister(&srandom_dev);
  if (ret) {
    printk(KERN_INFO "/dev/srandom driver unregistion failed..\n");
  } else {
    printk(KERN_INFO "/dev/srandom driver unregistered..\n");
  }

  // Remove /proc/srandom
  remove_proc_entry("srandom", NULL);
}


/*  Called when a process tries to open the device file, like  "dd if=/dev/srandom" */
static int device_open(struct inode *inode, struct file *file)
{
  if(mutex_lock_interruptible(&Open_mutex)) return -ERESTARTSYS;
  sdev_open++;
  sdev_openCount++;
  mutex_unlock(&Open_mutex);

  #ifdef DEBUG
    printk(KERN_INFO "Called device_open (current open) :%d\n",sdev_open);
    printk(KERN_INFO "Called device_open (total open)   :%d\n",sdev_openCount);
  #endif

  return SUCCESS;
}


/* Called when a process closes the device file.  */
static int device_release(struct inode *inode, struct file *file)
{
  if(mutex_lock_interruptible(&Open_mutex)) return -ERESTARTSYS;
  sdev_open--;
  mutex_unlock(&Open_mutex);

  #ifdef DEBUG
    printk(KERN_INFO "Called device_release :%d\n",sdev_open); 
  #endif

  return 0;
}

static ssize_t sdevice_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
  char *new_buf;                 // Buffer to hold numbers to send
  int ret,counter;
  int CC;
  size_t src_counter;

  #ifdef DEBUG
    printk(KERN_INFO "Called sdevice_read count:%zu\n",count);
  #endif

  if(count <= 512)
  {
    //  Send array to device
    CC = s[0] & (num_arr_RND -1);
    ret = copy_to_user(buf, sarr_RND[CC], count);

    // Get more RND numbers
    update_sarray(CC);
  }
  else
  {
    //  Allocate memory for new_buf
    new_buf = kmalloc((count + 512) * sizeof(uint8_t), __GFP_WAIT | __GFP_IO | __GFP_FS);
  
    // Init some variables for the loop
    counter = 0;
    src_counter = 512;
    ret = 0;

    //  Loop until we reach count size. (block size)
    while(counter < (int)count)
    {
      //  Select a RND array
      CC = s[0] & (num_arr_RND -1);

      //  Copy RND numbers to new_buf
      memcpy(new_buf+counter,sarr_RND[CC],src_counter);
      update_sarray(CC);

      #ifdef DEBUG2
        printk(KERN_INFO "Called sdevice_read: COPT_TO_USER counter:%d count:%zu \n", counter, count);
      #endif

      //  Increment counter
      counter += 512;
    }


    //  Send new_buf to device
    ret = copy_to_user(buf, new_buf, count);
  
    //  Free allocated memory
    kfree(new_buf);
  }

  #ifdef DEBUG2
    printk(KERN_INFO "Called sdevice_read: COPT_TO_USER counter:%d count:%zu \n", counter, count);
  #endif

  //  return how many chars we sent
  return count;
}


//  Called when someone tries to write to /dev/srandom device
static ssize_t sdevice_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
  ssize_t retval;
  char *newdata;

  #ifdef DEBUG
    printk(KERN_INFO "Called sdevice_write count:%zu\n",count);
  #endif

  //  Allocate memory to read from device
  newdata = kmalloc(count, GFP_KERNEL);
  if (!newdata) {
      retval = -ENOMEM;
  }

  else if(copy_from_user(newdata, buf, count)) {
      retval = -EFAULT;
  }

  retval = count;

  //  Free memory
  kfree(newdata);

  #ifdef DEBUG2
    printk(KERN_INFO "Called sdevice_write: COPT_FROM_USER count:%zu \n", count);
  #endif

  return retval;
}



//  Update the sarray with new random numbers
void update_sarray(int CC){
  int16_t C;
  int64_t X,Y,Z1,Z2,Z3;

  // This function must run exclusivly
  if(mutex_lock_interruptible(&UpArr_mutex))
   { printk(KERN_INFO "Mutex Failed\n"); }

  PRNGCount++;

  Z1=xorshft64();
  Z2=xorshft64();
  Z3=xorshft64();
  if ( (Z1 & 1) == 0 ){
    #ifdef DEBUG
      printk(KERN_INFO "0\n");
    #endif

    for (C=0;C<(arr_RND_SIZE -4) ;C=C+4){
       X=xorshft128();
       Y=xorshft128();
       sarr_RND[CC][C]   = sarr_RND[CC][C+1] ^ X ^ Y;
       sarr_RND[CC][C+1] = sarr_RND[CC][C+2] ^ Y ^ Z1;
       sarr_RND[CC][C+2] = sarr_RND[CC][C+3] ^ X ^ Z2;
       sarr_RND[CC][C+3] = X ^ Y ^ Z3;
    }
  } else {
    #ifdef DEBUG
      printk(KERN_INFO "1\n");
    #endif

    for (C=0;C<(arr_RND_SIZE -4) ;C=C+4){
       X=xorshft128();
       Y=xorshft128();
       sarr_RND[CC][C]   = sarr_RND[CC][C+1] ^ X ^ Z2;
       sarr_RND[CC][C+1] = sarr_RND[CC][C+2] ^ X ^ Y;
       sarr_RND[CC][C+2] = sarr_RND[CC][C+3] ^ Y ^ Z3;
       sarr_RND[CC][C+3] = X ^ Y ^ Z1;

    }
  }

  mutex_unlock(&UpArr_mutex);

  #ifdef DEBUG
    printk(KERN_INFO "C:%d, CC:%d\n", C,CC);
    printk(KERN_INFO "X:%llu, Y:%llu, Z1:%llu, Z2:%llu, Z3:%llu,\n", X,Y,Z1,Z2,Z3);
  #endif
}




//   Seed xorshft128
void seed_PRND(void) {
  getnstimeofday(&ts);
  s[0]=(s[0]<<31) ^ (uint64_t)ts.tv_nsec;
  getnstimeofday(&ts);
  s[1]=(s[1]<<24) ^ (uint64_t)ts.tv_nsec;
  #ifdef DEBUG
    printk(KERN_INFO "x:%llu, s[0]:%llu, s[1]:%llu\n", x,s[0],s[1]);
  #endif
}

// PRNGs
uint64_t xorshft64(void) {
        uint64_t z = ( x += 0x9E3779B97F4A7C15ULL );
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
}
uint64_t xorshft128(void) {
        uint64_t s1 = s[ 0 ];
        const uint64_t s0 = s[ 1 ];
        s[ 0 ] = s0;
        s1 ^= s1 << 23;
        return ( s[ 1 ] = ( s1 ^ s0 ^ ( s1 >> 17 ) ^ ( s0 >> 26 ) ) ) + s0;
}


int proc_read(struct seq_file *m, void *v) {
  seq_printf(m, "-----------------------:----------------------\n");
  seq_printf(m, "Device                 : /dev/"sDEVICE_NAME"\n");
  seq_printf(m, "Module version         : "AppVERSION"\n");
  seq_printf(m, "Current open count     : %d\n",sdev_open);
  seq_printf(m, "Total open count       : %d\n",sdev_openCount);
  seq_printf(m, "Total K bytes          : %llu\n",PRNGCount / 2);
  if ( PAID==0){
    seq_printf(m, "-----------------------:----------------------\n");
    seq_printf(m, "Please support my work and efforts contributing\n");
    seq_printf(m, "to the Linux community.  A $25 payment per\n");
    seq_printf(m, "server would be highly appreciated.\n");
  }
  seq_printf(m, "-----------------------:----------------------\n");
  seq_printf(m, "Author                 : Jonathan Senkerik\n");
  seq_printf(m, "Website                : http://www.jintegrate.co\n");
  seq_printf(m, "github                 : http://github.com/josenk/srandom\n");
  if ( PAID==0){
    seq_printf(m, "Paypal                 : josenk@jintegrate.co\n");
    seq_printf(m, "Bitcoin                : 1MTNg7SqcEWs5uwLKwNiAfYqBfnKFJu65p\n");
    seq_printf(m, "Commercial Invoice     : Avail on request.\n");
  }
  return 0;
}

int proc_open(struct inode *inode, struct  file *file) {
  return single_open(file, proc_read, NULL);
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE("/dev/srandom");
