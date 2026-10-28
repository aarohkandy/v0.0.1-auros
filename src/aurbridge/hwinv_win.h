/* hwinv_win.h -- what the wizard (stream B) calls. Windows only.
 *
 *   hw_inv *inv = hwinv_new();
 *   hwinv_scan(inv, &report);              // report: the pf_report the wizard already has
 *   for (int i = 0; i < inv->n_verdicts; i++)
 *       show(inv->verdicts[i].state, inv->verdicts[i].says);   // her words
 *   if (hwinv_blocks(inv)) ...             // refuse, before anything has changed
 *   char line[128]; hwinv_journal(inv, line, sizeof line);   // stream A2: the journal field
 *   hwinv_free(inv);
 *
 * hwinv_scan takes a second or two (SetupAPI and the print spooler), so
 * it belongs on the worker thread beside pf_run. Read-only. */
#ifndef AURBRIDGE_HWINV_WIN_H
#define AURBRIDGE_HWINV_WIN_H

#include "hwinv.h"
#include "preflight.h"

/* Devices, printers, the maker's strings; the drive, firmware and
 * BitLocker facts from `pf` (NULL: leave them unknown); then the
 * verdicts. Returns 0. */
int hwinv_scan(hw_inv *inv, const pf_report *pf);

/* 1 when any verdict blocks: AurOS would have no way to the internet. */
int hwinv_blocks(const hw_inv *inv);

#endif
