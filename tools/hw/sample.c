/* sample.c -- prints the report of a FICTIONAL Dell XPS 13 9343 (Broadcom Wi-Fi, no cable socket)
 * through the real renderer. docs/results/hardware/SAMPLE-REPORT.txt is this output;
 * tools/hw/hwtest.sh fails when the two differ, so the sample never goes stale. */
#include "hwcompat.h"
#include <stdio.h>
#include <string.h>
int main(void){
  hw_inv *v=hwinv_new();
  strcpy(v->dmi_vendor,"Dell Inc."); strcpy(v->dmi_model,"XPS 13 9343"); strcpy(v->dmi_family,"XPS");
  strcpy(v->bios_vendor,"Dell Inc."); strcpy(v->bios_version,"A13"); strcpy(v->bios_date,"09/20/2018");
  strcpy(v->cpu,"Intel(R) Core(TM) i5-5200U CPU @ 2.20GHz"); v->cpu_threads=4; v->ram_bytes=8ULL<<30;
  strcpy(v->os_name,"Windows 10 Home"); strcpy(v->os_display,"22H2"); v->os_build=19045;
  v->is_uefi=1; v->secure_boot=1; v->has_battery=1; strcpy(v->sys_disk_model,"LITEON CV1-8B256"); v->sys_disk_bytes=256ULL<<30; v->sys_disk_kind=1; v->sys_free_bytes=121ULL<<30; v->bitlocker=0; v->bitlocker_any=0;
  v->pf_go=1; v->pf_blocks=0; v->pf_warns=1; strcpy(v->pf_find[0].id,"fast-startup"); strcpy(v->pf_find[0].sev,"WARN"); strcpy(v->pf_find[0].title,"Fast Startup is on; the installer will switch it off"); v->pf_n_find=1;
  int k;
  k=hwinv_add_dev(v,"Net","PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03","PCI\\VEN_14E4&DEV_43B1&CC_028000","Dell Wireless 1560 802.11ac","Broadcom","bcmwl63a",0,71); v->devs[k].in_use=1;
  hwinv_add_dev(v,"Bluetooth","USB\\VID_0A5C&PID_216F&REV_0112","USB\\Class_E0&SubClass_01&Prot_01","Dell Wireless 1560 Bluetooth","","",0,0);
  hwinv_add_dev(v,"Camera","USB\\VID_0C45&PID_6A0A&MI_00","USB\\Class_0E&SubClass_01&Prot_00","Integrated Webcam","","usbvideo",0,0);
  hwinv_add_dev(v,"MEDIA","PCI\\VEN_8086&DEV_9CA0&SUBSYS_06791028&REV_03","PCI\\VEN_8086&DEV_9CA0&CC_040300","Intel(R) Display Audio","","",0,0);
  hwinv_add_dev(v,"Display","PCI\\VEN_8086&DEV_1616&SUBSYS_06791028&REV_09","PCI\\VEN_8086&DEV_1616&CC_030000","Intel(R) HD Graphics 5500","","igdkmd64",0,0);
  hwinv_add_dev(v,"System","PCI\\VEN_8086&DEV_9CB1&SUBSYS_06791028","PCI\\VEN_8086&DEV_9CB1&CC_0C0330","Intel USB xHCI","","",0,0);
  hwinv_add_dev(v,"Unknown","PCI\\VEN_8086&DEV_9CA4&SUBSYS_06791028","","PCI Device","","",28,0);
  int p=hwinv_add_printer(v,"HP DeskJet 2540","HP DeskJet 2540 series","USB001",1);
  hwinv_printer_usb(v,p,0x03F0,0x9411,"HPDeskJet_2540_seriesA1B2");
  hwcompat_judge(v);
  char *t=hwinv_text(v,"AurOS Setup (PC report, format 1)","2026-11-02 14:05 (this PC's clock)");
  fputs("FICTIONAL: a made-up Dell XPS 13 9343, to show what a report looks like.\r\nThe real file is AurOS-report.txt, written by \"AurOS-Setup.exe --report\".\r\n\r\n", stdout);
  fputs(t,stdout); 
  return 0;}
