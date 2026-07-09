/* ************************************************************************
> File Name:     hellomodule.c
> Author:        Komorebi
> Created Time:  2026年07月03日 星期五 09时23分40秒
> Description:   
 ************************************************************************/
#include <linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>

static int __init hello_init(void)
{
        printk(KERN_EMERG "{KERN_EMERG} Hello Module Init!\n");
        printk("{default} Hello Module Init!\n");
        return 0;
}

static void __exit hello_exit(void)
{
    printk("{default} Hello Module Exit!\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL2");
MODULE_AUTHOR("Komorebi");
MODULE_DESCRIPTION("hello module");
MODULE_ALIAS("test_module");
