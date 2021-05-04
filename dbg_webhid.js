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

Module.edbg = {
    isHidAvailable: () => {
        return "hid" in navigator;
    },


    device: null,
    devices: [],


    /* Let the user select which devices should be opened
     *
     * @warning Google Chrome requires requesting devices to be called in
     *     response to a user action
     * @warning https://wicg.github.io/webhid/#dom-hid-getdevices will not tell
     *     you any devices in Google Chrome, you have to request filter them
     */
    refreshDevices: async () => {
        if (!Module.edbg.isHidAvailable()) {
            throw new Error("WebHID not available");
        }

        /* @warning https://wicg.github.io/webhid/#dom-hid-getdevices will not
         *     tell you any devices in Google Chrome, you have to request filter
         *     them
         */
        const allDevices = await navigator.hid.requestDevice({
            "filters": []
        });
        let openedDevices = [];

        for (const device of allDevices) {
            if (!device.opened) {

                /* @warning This will on Ubuntu Mate 21.04 since
                 *     `/dev/hidrawX` is not accessible to the
                 *     user by default.
                 *
                 *     chowing the device will solve the problem.
                 */
                await device.open();
            }

            openedDevices.push(device);
        };

        Module.edbg.devices = openedDevices;
    },


    /* Will be resolved by {@code dbg_close} in order to notify external code
     * that edbg has finished.
     *
     * @warning There ought to be a better way, however I'm not familiar with
     *     one
     */
    close: null,
    running: null,
};


Module.edbg.running = new Promise((resolve) => {
    Module.edbg.close = resolve;
});

