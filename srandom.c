#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>		/* For kalloc */
#include <asm/uaccess.h>        /* For copy_to_user */
#include <linux/miscdevice.h>   /* For misc_register (the /dev/srandom) device */
#include <linux/time.h>         /* For getnstimeofday */
#include <linux/proc_fs.h>      /* For /proc filesystem */
#include <linux/seq_file.h>	/* For seq_print */
#include <linux/interrupt.h>	/* For Tasklet */

#define DRIVER_AUTHOR "Jonathan Senkerik <josenk@jintegrate.co>" 
#define DRIVER_DESC   "Improved random number generator."
#define arr_RND_SIZE 65         // Size of Array
#define sDEVICE_NAME "srandom"	// Dev name as it appears in /proc/devices
#define AppVERSION "1.10"
#define PAID 0
#define SUCCESS 0


/*  Prototypes - this would normally go in a .h file */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t sdevice_read(struct file *, char *, size_t, loff_t *);
static uint64_t xorshft64(void);
static uint64_t xorshft128(void);
static void update_sarray(void);
static void seed_PRND(void);
static int proc_read(struct seq_file *m, void *v);
static int proc_open(struct inode *inode, struct  file *file);



/* Global variables are declared as static, so are global within the file.  */


static struct file_operations sfops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.read = sdevice_read,
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



// Global variables
//uint8_t  PAID;
uint64_t x;                    // Used for xorshft64
uint64_t s[ 2 ];               // Used for xorshft128
uint64_t *sarr_RND;            // Array of SECURE RND numbers
int16_t  reSEEDx;              // reseed x for xorshft64
int16_t  reSEEDs;              // reseed s for xorshft128

uint64_t tm_seed;
struct timespec ts;

// Global counters
int16_t   sdev_open;           // srandom device current open count`.
int32_t   sdev_openCount;      // srandom device total open count`.
uint32_t  reSEEDxCount;        // Counter x
uint32_t  reSEEDsCount;        // Counter s
uint64_t  PRNGCount;           // Total generated (512byte)



/*  This function is called when the module is loaded */
int mod_init(void)
{
  int16_t C;
  int ret;

  //PAID=AppPAID;
  sdev_open=0;
  sdev_openCount=0;
  PRNGCount=0;
  reSEEDxCount=1;
  reSEEDsCount=1;
  reSEEDx=2;
  reSEEDs=2;

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
    printk(KERN_INFO "'mknod /dev/%s c 10 %i'.\n", sDEVICE_NAME, srandom_dev.minor);
  }

  // Create /proc/srandom
  if (! proc_create("srandom",0,NULL,&proc_fops)) {
    printk(KERN_INFO "/proc/srandom registion failed..\n");
  } else {
    printk(KERN_INFO "/proc/srandom registion regisered..\n");
  }

  printk("Module version         : "AppVERSION"\n");
  if ( PAID==0){
    printk("-----------------------:----------------------\n");
    printk("Please support my work and efforts contributing\n");
    printk("to the Linux community.  A $25 payment per\n");
    printk("server would be highly appreciated.\n");
  }
  printk("-----------------------:----------------------\n");
  printk("Author                 : Jonathan Senkerik\n");
  printk("Website                : http://www.jintegrate.co\n");
  printk("github                 : http://github.com/josenk/srandom\n");
  if ( PAID==0){
    printk("Paypal                 : josenk@jintegrate.co\n");
    printk("Bitcoin                : 1MTNg7SqcEWs5uwLKwNiAfYqBfnKFJu65p\n");
    printk("Commercial Invoice     : Avail on request.\n");
  }


  sarr_RND = kmalloc(arr_RND_SIZE * sizeof(uint64_t), __GFP_WAIT | __GFP_IO | __GFP_FS);

  //Entropy Initialize #2
  getnstimeofday(&ts);
  x=(x<<32) ^ (uint64_t)ts.tv_nsec;
  seed_PRND();

  //  Init the sarray
  for (C=0;C<=arr_RND_SIZE;C++){
     sarr_RND[C] = xorshft128();
  }
  update_sarray();

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


/*  Called when a process tries to open the device file, like  "cat /dev/srandom" */
static int device_open(struct inode *inode, struct file *file)
{
  sdev_open++;
  sdev_openCount++;
  #ifdef DEBUG
    printk(KERN_INFO "Called device_open (current open) :%d\n",sdev_open);
    printk(KERN_INFO "Called device_open (total open)   :%d\n",sdev_openCount);
  #endif

  return SUCCESS;
}


/* Called when a process closes the device file.  */
static int device_release(struct inode *inode, struct file *file)
{
  sdev_open--;
  #ifdef DEBUG
    printk(KERN_INFO "Called device_release :%d\n",sdev_open); 
  #endif

  //  reSEEDx
  reSEEDx--;
  if ( reSEEDx <= 0) {
    reSEEDxCount++;
    reSEEDx = (int16_t) (xorshft64() & 255);

    // reseed x for xorshft64
    getnstimeofday(&ts);
    x=(x<<32) ^ (uint64_t)ts.tv_nsec;
  }


  // reSEEDs
  reSEEDs--;
  if ( reSEEDs <= 0) {
    reSEEDsCount++;
    reSEEDs = (int16_t) (xorshft64() & 255);

    // seed s[x] for xorshft128
    seed_PRND();
  }
  
  update_sarray();

  return 0;
}

static ssize_t sdevice_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
  int ret;

  #ifdef DEBUG
    printk(KERN_INFO "Called sdevice_read\n");
  #endif

  //  Send array to device
  ret = copy_to_user(buf, sarr_RND, count);

  // Get more RND numbers
  update_sarray();

  return count;
}

//  Update the sarray with new random numbers
void update_sarray(void){
  int16_t C;
  int64_t X,Y,Z1,Z2,Z3;

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
       sarr_RND[C]   = sarr_RND[C+1] ^ X ^ Y;
       sarr_RND[C+1] = sarr_RND[C+2] ^ Y ^ Z1;
       sarr_RND[C+2] = sarr_RND[C+3] ^ X ^ Z2;
       sarr_RND[C+3] = X ^ Y ^ Z3;
    }
  } else {
    #ifdef DEBUG
      printk(KERN_INFO "1\n");
    #endif

    for (C=0;C<(arr_RND_SIZE -4) ;C=C+4){
       X=xorshft128();
       Y=xorshft128();
       sarr_RND[C]   = sarr_RND[C+1] ^ X ^ Z2;
       sarr_RND[C+1] = sarr_RND[C+2] ^ X ^ Y;
       sarr_RND[C+2] = sarr_RND[C+3] ^ Y ^ Z3;
       sarr_RND[C+3] ^= X ^ Y ^ Z1;
    }
  }
}




//   Seed xorshft64 & xorshft128
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
  seq_printf(m, "PRNG1 reseed count     : %d\n",reSEEDsCount);
  seq_printf(m, "PRNG2 reseed count     : %d\n",reSEEDxCount);
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
