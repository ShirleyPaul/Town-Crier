/* * Copyright (c) 2016-2017 by Cornell University.  All Rights Reserved.
 *
 * Permission to use the "TownCrier" software ("TownCrier"), officially docketed at
 * the Center for Technology Licensing at Cornell University as D-7364, developed
 * through research conducted at Cornell University, and its associated copyrights
 * solely for educational, research and non-profit purposes without fee is hereby
 * granted, provided that the user agrees as follows:
 *
 * The permission granted herein is solely for the purpose of compiling the
 * TowCrier source code. No other rights to use TownCrier and its associated
 * copyrights for any other purpose are granted herein, whether commercial or
 * non-commercial.
 *
 * Those desiring to incorporate TownCrier software into commercial products or use
 * TownCrier and its associated copyrights for commercial purposes must contact the
 * Center for Technology Licensing at Cornell University at 395 Pine Tree Road,
 * Suite 310, Ithaca, NY 14850; email: ctl-connect@cornell.edu; Tel: 607-254-4698;
 * FAX: 607-254-5454 for a commercial license.
 *
 * IN NO EVENT SHALL CORNELL UNIVERSITY BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS, EVEN IF
 * CORNELL UNIVERSITY MAY HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE WORK PROVIDED HEREIN IS ON AN "AS IS" BASIS, AND CORNELL UNIVERSITY HAS NO
 * OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.  CORNELL UNIVERSITY MAKES NO REPRESENTATIONS AND EXTENDS NO
 * WARRANTIES OF ANY KIND, EITHER IMPLIED OR EXPRESS, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR
 * PURPOSE, OR THAT THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS WILL NOT
 * INFRINGE ANY PATENT, TRADEMARK OR OTHER RIGHTS.
 *
 * TownCrier was developed with funding in part by the National Science Foundation
 * (NSF grants CNS-1314857, CNS-1330599, CNS-1453634, CNS-1518765, CNS-1514261), a
 * Packard Fellowship, a Sloan Fellowship, Google Faculty Research Awards, and a
 * VMWare Research Award.
 */

//
// Created by sgx on 2/7/17.
//

#ifndef TOWN_CRIER_UTILS_H
#define TOWN_CRIER_UTILS_H
#define __STDC_FORMAT_MACROS // non needed in C, only in C++

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* #include <sstream> *>/ 

/* allows to differentiate between User error and server-side error */
enum err_code {
  NO_ERROR = 0,
  INVALID_PARAMS, /* user supplied invalid parameters to the function */
  WEB_ERROR,      /* Unable to get web request */
  UNKNOWN_ERROR,
};

/* Convers uint64 types to their string representation */
inline uint32_t swap_uint32(uint32_t num) {
  return ((num >> 24) & 0xff) | // move byte 3 to byte 0
      ((num << 8) & 0xff0000) | // move byte 1 to byte 2
      ((num >> 8) & 0xff00) | // move byte 2 to byte 1
      ((num << 24) & 0xff000000); // byte 0 to byte 3
}

inline uint64_t swap_uint64(uint64_t num) {
  return ((static_cast<uint64_t>(swap_uint32(num & 0xffffffff))) << 32) |
      (static_cast<uint64_t>(swap_uint32((num >> 32) & 0xffffffff)));
}

#endif //TOWN_CRIER_UTILS_H
