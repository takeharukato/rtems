/*
 *  $Id$
 */

#include <libcpu/io.h>
#include <libcpu/spr.h>

#include <bsp.h>
#include <bsp/pci.h>
#include <bsp/consoleIo.h>
#include <bsp/residual.h>
#include <bsp/openpic.h>

#include <rtems/bspIo.h>
#include <libcpu/cpuIdent.h>

#define RAVEN_MPIC_IOSPACE_ENABLE  0x0001
#define RAVEN_MPIC_MEMSPACE_ENABLE 0x0002
#define RAVEN_MASTER_ENABLE        0x0004
#define RAVEN_PARITY_CHECK_ENABLE  0x0040
#define RAVEN_SYSTEM_ERROR_ENABLE  0x0100
#define RAVEN_CLEAR_EVENTS_MASK    0xf9000000

#define RAVEN_MPIC_MEREN    ((volatile unsigned *)0xfeff0020)
#define RAVEN_MPIC_MERST    ((volatile unsigned *)0xfeff0024)
/* enable machine check on all conditions */
#define MEREN_VAL           0x2f00

#define pci BSP_pci_configuration
extern unsigned int EUMBBAR;

extern const pci_config_access_functions pci_direct_functions;
extern const pci_config_access_functions pci_indirect_functions;

unsigned long
_BSP_clear_hostbridge_errors(int enableMCP, int quiet)
{
unsigned merst;

    merst = in_be32(RAVEN_MPIC_MERST);
    /* write back value to clear status */
    out_be32(RAVEN_MPIC_MERST, merst);

    if (enableMCP) {
	/* disallow MCP for now; (pci config access to empty slot faults :-() */
	return -1;
      if (!quiet)
        printk("Enabling MCP generation on hostbridge errors\n");
      out_be32(RAVEN_MPIC_MEREN, MEREN_VAL);
    } else {
      out_be32(RAVEN_MPIC_MEREN, 0);
      if ( !quiet && enableMCP ) {
        printk("leaving MCP interrupt disabled\n");
      }
    }
    return (merst & 0xffff);
}

void detect_host_bridge()
{
#if (defined(mpc8240) || defined(mpc8245))
  /*
   * If the processor is an 8240 or an 8245 then the PIC is built
   * in instead of being on the PCI bus. The MVME2100 is using Processor
   * Address Map B (CHRP) although the Programmer's Reference Guide says
   * it defaults to Map A.
   */
  /* We have an EPIC Interrupt Controller  */
  OpenPIC = (volatile struct OpenPIC *) (EUMBBAR + BSP_OPEN_PIC_BASE_OFFSET);
  pci.pci_functions = &pci_indirect_functions;
  pci.pci_config_addr = (volatile unsigned char *) 0xfec00000;
  pci.pci_config_data = (volatile unsigned char *) 0xfee00000;
#else

  PPC_DEVICE *hostbridge;
  unsigned int id0;
  unsigned int tmp;

  /*
   * This code assumes that the host bridge is located at
   * bus 0, dev 0, func 0 AND that the old pre PCI 2.1
   * standart devices detection mecahnism that was used on PC
   * (still used in BSD source code) works.
   */
  hostbridge=residual_find_device(&residualCopy, PROCESSORDEVICE, NULL,
            BridgeController,
            PCIBridge, -1, 0);

  if (hostbridge) {
    if (hostbridge->DeviceId.Interface==PCIBridgeIndirect) {
      pci.pci_functions=&pci_indirect_functions;
      /* Should be extracted from residual data,
       * indeed MPC106 in CHRP mode is different,
       * but we should not use residual data in
       * this case anyway.
       */
      pci.pci_config_addr = ((volatile unsigned char *)
             (ptr_mem_map->io_base+0xcf8));
      pci.pci_config_data = ptr_mem_map->io_base+0xcfc;
    } else if(hostbridge->DeviceId.Interface==PCIBridgeDirect) {
      pci.pci_functions=&pci_direct_functions;
      pci.pci_config_data=(unsigned char *) 0x80800000;
    } else {
    }
  } else {
    /* Let us try by experimentation at our own risk! */
    pci.pci_functions = &pci_direct_functions;
    /* On all direct bridges I know the host bridge itself
     * appears as device 0 function 0.
     */
    pci_read_config_dword(0, 0, 0, PCI_VENDOR_ID, &id0);
    if (id0==~0U) {
      pci.pci_functions = &pci_indirect_functions;
      pci.pci_config_addr = (volatile unsigned char*)
              (ptr_mem_map->io_base+0xcf8);
      pci.pci_config_data = (volatile unsigned char*)
                              (ptr_mem_map->io_base+0xcfc);
    }
    /* Here we should check that the host bridge is actually
     * present, but if it not, we are in such a desperate
     * situation, that we probably can't even tell it.
     */
  }
  pci_read_config_dword(0, 0, 0, 0, &id0);

  if(id0 == PCI_VENDOR_ID_MOTOROLA +
     (PCI_DEVICE_ID_MOTOROLA_RAVEN<<16)) {
    /*
     * We have a Raven bridge. We will get information about its settings
     */
    pci_read_config_dword(0, 0, 0, PCI_COMMAND, &id0);
#ifdef SHOW_RAVEN_SETTING
    printk("RAVEN PCI command register = %x\n",id0);
#endif
    id0 |= RAVEN_CLEAR_EVENTS_MASK;
    pci_write_config_dword(0, 0, 0, PCI_COMMAND, id0);
    pci_read_config_dword(0, 0, 0, PCI_COMMAND, &id0);
#ifdef SHOW_RAVEN_SETTING
    printk("After error clearing RAVEN PCI command register = %x\n",id0);
#endif

    if (id0 & RAVEN_MPIC_IOSPACE_ENABLE) {
      pci_read_config_dword(0, 0, 0,PCI_BASE_ADDRESS_0, &tmp);
#ifdef SHOW_RAVEN_SETTING
      printk("Raven MPIC is accessed via IO Space Access at address : %x\n",
          (tmp & ~0x1));
#endif
    }
    if (id0 & RAVEN_MPIC_MEMSPACE_ENABLE) {
      pci_read_config_dword(0, 0, 0,PCI_BASE_ADDRESS_1, &tmp);
#ifdef SHOW_RAVEN_SETTING
      printk("Raven MPIC is accessed via memory Space Access"
             "at address : %x\n", tmp)
#endif
      OpenPIC=(volatile struct OpenPIC *) (tmp + PREP_ISA_MEM_BASE);
      printk("OpenPIC found at %x.\n", OpenPIC);
    }
  }
#endif
  if (OpenPIC == (volatile struct OpenPIC *)0) {
    BSP_panic("OpenPic Not found\n");
  }
}
