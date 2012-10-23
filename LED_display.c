/******************************************************************************* 
 * File: LED_display.c
 * Description: A LED display driver for the beaglebone using GPIO. 
 * The driver expose the display through a char-device and ioctl.
 * Userspace programs calls for ex. ioctl(fd,DISPLAY_SET, &output) or by writing 
 * directy to the chardevice. ex. echo 1 > /dev/LED_display
 * Date: 24/9 2012
 * Author: Dennis Johansson, dennis.johansson@gmail.com
 *******************************************************************************/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/ioport.h> 
#include <asm/system.h>		/* cli(), *_flags */
#include <asm/uaccess.h>	/* copy_*_user */
#include <asm/io.h>
#include <linux/gpio.h>

#include "LED_display.h"

MODULE_LICENSE("GPL");

struct cdev LED_display_cdev;
int LED_display_var;
int devno;

struct omap_mux pin_mux[] = {
	{GPMC_AD6, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD7, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD2, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD3, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_ADVN_ALE, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_OEN_REN, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_BEN0_CLE, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_WEN, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD13, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD12, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD9, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD10, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD15, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD14, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD11, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_CLK, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_AD8, MUX_MODE_7_GPIO_OUTPUT},
	{GPMC_CSN2, MUX_MODE_7_GPIO_OUTPUT},
	{.reg_offset = OMAP_MUX_TERMINATOR},
};

unsigned char led_masks[] ={192,249,164,176,153,146,130,248,128,152};

static struct gpio leds_gpios[] = {
		{ 38, GPIOF_OUT_INIT_LOW, "LED1_1" }, // 3 GPIO1_6
		{ 39, GPIOF_OUT_INIT_LOW, "LED1_2" }, // 4 GPIO1_7
		{ 34, GPIOF_OUT_INIT_LOW, "LED1_3" }, // 5 GPIO1_2
		{ 35, GPIOF_OUT_INIT_LOW, "LED1_4" }, // 6 GPIO1_3
		{ 66, GPIOF_OUT_INIT_LOW, "LED1_5" }, // 7 GPIO2_2
		{ 67, GPIOF_OUT_INIT_LOW, "LED1_6" }, // 8 GPIO2_3
		{ 69, GPIOF_OUT_INIT_LOW, "LED1_7" }, // 9 GPIO2_5
		{ 68, GPIOF_OUT_INIT_LOW, "LED1_8" }, // 10 GPIO2_4
		{ 45, GPIOF_OUT_INIT_LOW, "LED2_1" }, // 11 GPIO1_13
		{ 44, GPIOF_OUT_INIT_LOW, "LED2_2" }, // 12 GPIO1_12
		{ 23, GPIOF_OUT_INIT_LOW, "LED2_3" }, // 13 GPIO0_23
		{ 26, GPIOF_OUT_INIT_LOW, "LED2_4" }, // 14 GPIO0_26
		{ 47, GPIOF_OUT_INIT_LOW, "LED2_5" }, // 15 GPIO1_15
		{ 46, GPIOF_OUT_INIT_LOW, "LED2_6" }, // 16 GPIO1_14
		{ 27, GPIOF_OUT_INIT_LOW, "LED2_7" }, //17 GPIO0_27
		{ 65, GPIOF_OUT_INIT_LOW, "LED2_8" }, //18 GPIO2_1	
		{ 22, GPIOF_OUT_INIT_HIGH, "LED1_SWITCH" }, //19 GPIO0_22
		{ 63, GPIOF_OUT_INIT_HIGH, "LED2_SWITCH" }, //20 GPIO1_31
		};

static int setup_pin_mux(void)
{	
	void* adress;
	int i;
	if (request_mem_region(AM33XX_CONTROL_PADCONF_MUX_PBASE, AM33XX_CONTROL_PADCONF_MUX_SIZE,"MuxMem") == NULL)
			printk(KERN_WARNING "Error requesting mem reqion MUX_PBASE\n");
	
	adress = (void *)ioremap (AM33XX_CONTROL_PADCONF_MUX_PBASE,AM33XX_CONTROL_PADCONF_MUX_SIZE);
	
	if (adress == NULL)
	{
		printk(KERN_WARNING "Error ioremap: MUX_PBASE\n");
		return -1;
	}

	for (i = 0; pin_mux[i].reg_offset != OMAP_MUX_TERMINATOR; i++)
			iowrite16(pin_mux[i].value, adress+pin_mux[i].reg_offset);
	
	iounmap(adress);
	release_mem_region(AM33XX_CONTROL_PADCONF_MUX_PBASE,AM33XX_CONTROL_PADCONF_MUX_SIZE);
	
	return 0;
}


static void set_led_number(int i, unsigned char led_mask)
{
	int max = i + 8; 
	for (; i < max; i ++)
		{
			if (led_mask & 1)
				gpio_set_value(leds_gpios[i].gpio, 1);
			else
				gpio_set_value(leds_gpios[i].gpio, 0);		
			
			led_mask = (led_mask >> 1) ;
		}
}

static void display_number(int number)
{	

	unsigned int digit1 = (number % 10);
	unsigned int digit10 = (number / 10);
	
	set_led_number(8, led_masks[digit1]);

	if (digit10 > 0)
		set_led_number(0, led_masks[digit10]);
	else
		set_led_number(0, 255);
	
}		

long LED_display_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	
	int err = 0;
	unsigned int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != GPIO_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > GPIO_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
        
	case DISPLAY_SET: /* Get: arg points to the value */
		retval = __get_user(LED_display_var, (int __user *)arg);
		display_number(LED_display_var);
		break;
	
	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}

int LED_display_open(struct inode *inode, struct file *filp) {
	return 0;          
}

int LED_display_release(struct inode *inode, struct file *filp) {
	return 0;
}

ssize_t LED_display_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	ssize_t retval;
	char chardigit[4]={};
	long result;	
	
	if (count > 4)
		{
			printk(KERN_WARNING "Max two digits!\n");
			return -1;
		}
	
	if (copy_from_user(&chardigit, buf, count))
		retval = -EFAULT;
	else 
		retval = count;
	
	result = simple_strtol(chardigit,0,0); // str to int 

	if (result > 99)
		result = 99;	

	display_number(result);
	
	return retval;

}

struct file_operations LED_display_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = LED_display_ioctl,
	.open =     LED_display_open,
	.release =  LED_display_release,
	.write =    LED_display_write,
};

static int __init LED_display_init(void)
{
	int err = 0, result = 0, major = 0;
	dev_t dev = 0;
	setup_pin_mux();

	err = gpio_request_array(leds_gpios, ARRAY_SIZE(leds_gpios));

	if (err)
		printk(KERN_WARNING "Could not allocate GPIO");

	result = alloc_chrdev_region(&dev, 0, 1,"LED_display");
	major = MAJOR(dev);


	if (result < 0) {
		printk(KERN_WARNING "LED_display: can't get major %d\n", major);
		return result;
	}

	display_number(0);
	devno = MKDEV(major, 0);
	cdev_init(&LED_display_cdev, &LED_display_fops);
	LED_display_cdev.owner = THIS_MODULE;
	LED_display_cdev.ops = &LED_display_fops;
	err = cdev_add (&LED_display_cdev, devno, 1);

	if (err)
		printk(KERN_NOTICE "Error %d adding char-device",err);

	return 0;
}

static void __exit LED_display_exit(void)
{
	gpio_free_array(leds_gpios, ARRAY_SIZE(leds_gpios));
	cdev_del (&LED_display_cdev);
	unregister_chrdev_region(devno, 1);

}

module_init (LED_display_init);
module_exit (LED_display_exit);


