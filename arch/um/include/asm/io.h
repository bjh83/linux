#ifndef __ASM_UM_IO_H
#define __ASM_UM_IO_H

#include <linux/types.h>
#include <asm/byteorder.h>
// #include <asm/memory.h>
#include <asm-generic/io.h>

void __iomem *ioremap(resource_size_t res_cookie, size_t size);
#define ioremap_nocache ioremap

void __iomem *ioremap_wc(resource_size_t res_cookie, size_t size);

void __iomem *ioremap_wt(resource_size_t res_cookie, size_t size);

void iounmap(volatile void __iomem *addr);

#endif /* __ASM_UM_IO_H */
