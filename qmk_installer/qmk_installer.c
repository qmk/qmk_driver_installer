/*
* wdi-simple.c: Console Driver Installer for a single USB device
* Copyright (c) 2010-2016 Pete Batard <pete@akeo.ie>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 3 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libwdi.h"
#include <stdbool.h>

#if defined(_PREFAST_)
/* Disable "Banned API Usage:" errors when using WDK's OACR/Prefast */
#pragma warning(disable:28719)
/* Disable "Consider using 'GetTickCount64' instead of 'GetTickCount'" when using WDK's OACR/Prefast */
#pragma warning(disable:28159)
#endif

#define oprintf(...) do {if (!opt_silent) printf(__VA_ARGS__);} while(0)

#define INF_NAME    "usb_device.inf"

void delete_directory(const char* dir)
{
    // The buffer need to be terminated by double zeros
    char buffer[MAX_PATH + 1];
    strcpy_s(buffer, MAX_PATH, dir);
    buffer[strnlen_s(buffer, MAX_PATH) + 1] = 0;
    SHFILEOPSTRUCT file_op = {
        NULL,
        FO_DELETE,
        buffer,
        "",
        FOF_NOCONFIRMATION |
        FOF_NOERRORUI |
        FOF_SILENT,
        false,
        0,
    ""
    };
    SHFileOperationA(&file_op);
}

static const int opt_silent = 0;

int install_drivers(const char* temp_path) {
    char line[1024];
    const char* delimiters = ",";
    int log_level = WDI_LOG_LEVEL_WARNING;
    int opt_extract = 0;
    wdi_set_log_level(log_level);
    FILE* file = fopen("drivers.txt", "r");
    if (!file) {
        oprintf("Could not open drivers.txt\n");
        return 1;
    }

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#')
            continue;

        struct wdi_device_info *ldev, dev = { NULL, 0, 0, FALSE, 0, "", NULL, NULL, NULL };
        struct wdi_options_create_list ocl = { 0 };
        struct wdi_options_prepare_driver opd = { 0 };
        struct wdi_options_install_driver oid = { 0 };
        struct wdi_options_install_cert oic = { 0 };
        char original_line[sizeof(line)];
        strcpy(original_line, line);
        BOOL matching_device_found;
        int r;
        char *inf_name = INF_NAME;
        char *ext_dir = temp_path;
        char *cert_name = NULL;

        ocl.list_all = TRUE;
        ocl.list_hubs = TRUE;
        ocl.trim_whitespaces = TRUE;
        char* driver_type = strtok(line, delimiters);
        if (strcmp(driver_type, "winusb") == 0) {
            opd.driver_type = WDI_WINUSB;
        } else if(strcmp(driver_type, "libusb") == 0) {
            opd.driver_type = WDI_LIBUSB0;
        }
        else if (strcmp(driver_type, "libusbk") == 0) {
            opd.driver_type = WDI_LIBUSBK;
        } else { 
            oprintf("Invalid driver type \"%s\"\n", driver_type);
            oprintf("On the line:\n%s\n", original_line);
            return 1;
        }

        char* desc = strtok(NULL, delimiters);
        if (!desc) {
            oprintf("Description missing\n");
            oprintf("On the line:\n%s\n", original_line);
            return 1;
        }

        char* str_vid = strtok(NULL, delimiters);
        if (!str_vid) {
            oprintf("vid missing\n");
            oprintf("On the line:\n%s\n", original_line);
            return 1;
        }

        char* str_pid = strtok(NULL, delimiters);
        if (!str_pid) {
            oprintf("pid missing\n");
            oprintf("On the line:\n%s\n", original_line);
            return 1;
        }
        
        char* guid = strtok(NULL, delimiters);
        if (!guid) {
            oprintf("guid missing\n");
            oprintf("On the line:\n%s\n", original_line);
            return 1;
        }

        unsigned short vid = (unsigned short)strtol(str_vid, NULL, 16);
        unsigned short pid = (unsigned short)strtol(str_pid, NULL, 16);

        dev.vid = vid;
        dev.pid = pid;
        dev.desc = desc;
        opd.device_guid = guid;

        oprintf("Extracting driver files for %s...\n", desc);
        r = wdi_prepare_driver(&dev, ext_dir, inf_name, &opd);
        oprintf("  %s\n", wdi_strerror(r));
        if ((r != WDI_SUCCESS) || (opt_extract))
            return r;

        if (cert_name != NULL) {
            oprintf("Installing certificate '%s' as a Trusted Publisher...\n", cert_name);
            r = wdi_install_trusted_certificate(cert_name, &oic);
            oprintf("  %s\n", wdi_strerror(r));
        }

        oprintf("Installing driver for %s...\n", desc);
        oprintf("  This can take a while, please, don't close this window!\n");

        // Try to match against a plugged device to avoid device manager prompts
        matching_device_found = FALSE;
        if (wdi_create_list(&ldev, &ocl) == WDI_SUCCESS) {
            r = WDI_SUCCESS;
            for (; (ldev != NULL) && (r == WDI_SUCCESS); ldev = ldev->next) {
                if ((ldev->vid == dev.vid) && (ldev->pid == dev.pid) && (ldev->mi == dev.mi)) {
                    dev.hardware_id = ldev->hardware_id;
                    dev.device_id = ldev->device_id;
                    matching_device_found = TRUE;
                    oprintf("  %s: ", dev.hardware_id);
                    fflush(stdout);
                    r = wdi_install_driver(&dev, ext_dir, inf_name, &oid);
                    oprintf("%s\n", wdi_strerror(r));
                }
            }
        }

        // No plugged USB device matches this one -> install driver
        if (!matching_device_found) {
            r = wdi_install_driver(&dev, ext_dir, inf_name, &oid);
            oprintf("  %s\n", wdi_strerror(r));
        }
    }
    return 0;
}

int __cdecl main(int argc, char** argv) {
    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    strcat_s(temp_path, MAX_PATH, "qmk_driver");

    int ret = install_drivers(temp_path);

    oprintf("Cleaning up...\n");
    delete_directory(temp_path);

    return ret;
}
