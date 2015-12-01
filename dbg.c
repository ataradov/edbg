/*
 * Copyright (c) 2015, Thibaut VIARD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "dbg.h"

/*- Variables ---------------------------------------------------------------*/
static const uint16_t dap_products_atmel[]=
{
  DBG_PID_ATMEL_EDBG, DBG_PID_ATMEL_MEDBG, DBG_PID_ATMEL_ICE, DBG_PID_ARDUINO_ZERO, 0
};

static const dap_vendor_t dap_vendors[]=
{
  { DBG_VID_ATMEL, dap_products_atmel },
  { 0, 0 } // End of array
};

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
int dbg_validate_dap(uint16_t vendorid, uint16_t productid)
{
  int vendor, product;

  for (vendor=0; dap_vendors[vendor].vendor_id != 0; vendor++)
  {
    if (vendorid == dap_vendors[vendor].vendor_id)
    {
      // Found vendor, looking for product
      for (product=0; dap_vendors[vendor].products[product] != 0; product++)
      {
        if (productid == dap_vendors[vendor].products[product])
        {
          return vendor<<16|product;
        }
      }
    }
  }

  return -1;
}
