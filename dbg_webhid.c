/*
 * Copyright (c) 2013-2021, Alex Taradov <alex@taradov.com>
 *                          ooxi <violetland@mail.ru>
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

/*- Includes ----------------------------------------------------------------*/
#include "dbg.h"

#include <emscripten.h>
#include <stddef.h>

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void dbg_webhid_set_debugger(debugger_t *debuggers, size_t debugger) {
  debuggers[debugger].path = "/path";
  debuggers[debugger].serial = "serial";
  debuggers[debugger].wserial = 0;
  debuggers[debugger].manufacturer = "manufacturer";
  debuggers[debugger].product = "product";
  debuggers[debugger].vid = 0;
  debuggers[debugger].pid = 0;
}

//-----------------------------------------------------------------------------
EM_JS(int, dbg_enumerate, (debugger_t *debuggers, int size), {
  return Asyncify.handleAsync(async () => {
    if (!Module.edbg.isHidAvailable())
    {
      throw new Error("WebHID not available");
    }

    console.error("dbg_enumerate stubbed, only one CMSIS-DAP adapter supported concurrently!");
    const dbg_webhid_set_debugger = Module.cwrap("dbg_webhid_set_debugger", "void", ["number", "number"]);
    dbg_webhid_set_debugger(debuggers, 0);

    /* Return the value as you normally would.
     */
    return 1;
  });
});

//-----------------------------------------------------------------------------
// @warning Argument must not be named {@code debugger} since this is a fixed
//     JavaScript statement
//
// @see https://developer.mozilla.org/de/docs/Web/JavaScript/Reference/Statements/debugger
EM_JS(void, dbg_open, (debugger_t *debugger_), {
  if (!Module.edbg.isHidAvailable())
  {
    throw new Error("WebHID not available");
  }

  console.error("dbg_open stubbed, will always use the first device!");
  Module.edbg.device = Module.edbg.devices[0];
});

//-----------------------------------------------------------------------------
// @warning I don't know how to get the report size from the WebHID device.
//     However I also don't know how to access quite a lot of other information
//     besides sending and receiving reports, thus it's quite likely that I'm
//     just missing something
EM_JS(int, dbg_get_report_size, (void), {
  if (!Module.edbg.isHidAvailable())
  {
    throw new Error("WebHID not available");
  }

  return 64;  // TODO 128 is too big
});

//-----------------------------------------------------------------------------
EM_JS(void, dbg_close, (void), {
  if (!Module.edbg.isHidAvailable())
  {
    throw new Error("WebHID not available");
  }

  Module.edbg.device = null;
  Module.edbg.close();
});

//-----------------------------------------------------------------------------
EM_JS(int, dbg_dap_cmd, (uint8_t *data, int resp_size, int req_size), {
  return Asyncify.handleAsync(async () => {
    if (!Module.edbg.isHidAvailable())
    {
      throw new Error("WebHID not available");
    }

    if (null === Module.edbg.device)
    {
      throw new Error("No device opended by `dbg_open'");
    }


    /* We will temporarly set the {@code inputreport} event listener
     * to fullfill exactly one Promise, which is the response to our
     * request.
     */
    let onInputReport = null;

    try {
      /* {@code responsePromise} will be waited on after the
       * request has been sent.
       *
       * It will automatically be rejected, if sending the
       * request and receiving the response will be too slow.
       *
       * Otherwise it will be resolved as soon as the response
       * has been received from the device
       *
       * TODO setTimeout + reject
       */
      const responsePromise = new Promise((resolve, reject) => {
        const timeoutId = window.setTimeout(() => {
          reject(new Error("Device did not respond to request"));
        }, 2000);

        onInputReport = (e) => {
          window.clearTimeout(timeoutId);

          const inputReport = new Uint8Array(e.data.buffer);

          /* First value in response seems to be
           * the request id? However I'm not 100%
           * sure on that.
           *
           * It would mirror the behaviour of other
           * dbg interfaces, however there one has
           * to also specify the report id as part
           * of the request data and not as a
           * separate parameter.
           */
          writeArrayToMemory(inputReport.subarray(1), data);
          resolve(inputReport.length - 1);
        };

        Module.edbg.device.addEventListener("inputreport", onInputReport);
      });

      /* @see https://github.com/ataradov/edbg/blob/master/dbg_lin.c#L198
       */
      const outputReportId = 0x00;
      const outputReport = new Uint8Array(Module.HEAPU8.subarray(data, data + req_size));

      await Module.edbg.device.sendReport(outputReportId, outputReport);
      return await responsePromise;

    } finally {
      Module.edbg.device.removeEventListener("inputreport", onInputReport);
    }
  });
});

