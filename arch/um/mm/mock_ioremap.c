#include <linux/mm.h>
#include <linux/io.h>
#include <linux/kernel.h>

void __iomem *__maybe_mock_ioremap(phys_addr_t phys_addr, size_t size)
{
	return (void __iomem *) phys_addr;
}

void __iomem *ioremap(resource_size_t res_cookie, size_t size)
{
	return __maybe_mock_ioremap(res_cookie, size);
}

void __iomem *ioremap_wc(resource_size_t res_cookie, size_t size)
{
	return __maybe_mock_ioremap(res_cookie, size);
}

void __iomem *ioremap_wt(resource_size_t res_cookie, size_t size)
{
	return __maybe_mock_ioremap(res_cookie, size);
}

void iounmap(volatile void __iomem *addr)
{
}
