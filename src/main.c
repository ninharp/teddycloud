
// Platform-specific dependencies
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WIN32
#else
#include <unistd.h>
#endif

#include "error.h"
#include "debug.h"
#include "cJSON.h"
#include "esp32.h"

#include "version.h"

#include "tls_adapter.h"
#include "cloud_request.h"

#include "settings.h"
#include "esp32.h"

void platform_init(void);
void platform_deinit(void);
void server_init(void);
#define DEFAULT_HTTP_PORT 80
#define DEFAULT_HTTPS_PORT 443

typedef enum
{
    PROT_HTTP,
    PROT_HTTPS
} Protocol;

void get_directory_path(const char *filepath, char *dirpath, int maxLen)
{
    // Find the last occurrence of '/' or '\' in the file path
    int lastSlash = -1;
    for (int i = 0; filepath[i] != '\0'; i++)
    {
        if (filepath[i] == '/' || filepath[i] == '\\')
        {
            lastSlash = i;
        }
    }

    if (lastSlash == -1)
    {
        // No directory part found, use an empty string for the directory path
        dirpath[0] = '\0';
    }
    else
    {
        // Copy the characters before the last slash to the directory path buffer
        snprintf(dirpath, maxLen, "%.*s", lastSlash, filepath);
    }
}

bool parse_url(const char *url, char **hostname, uint16_t *port, char **uri, Protocol *protocol)
{
    if (strstr(url, "http://") == url)
    {
        *protocol = PROT_HTTP;
        url += strlen("http://");
    }
    else if (strstr(url, "https://") == url)
    {
        *protocol = PROT_HTTPS;
        url += strlen("https://");
    }
    else
    {
        TRACE_ERROR("Unknown protocol\r\n");
        return false;
    }

    char *port_start = strchr(url, ':');
    char *path_start = strchr(url, '/');
    if (path_start == NULL)
    {
        TRACE_ERROR("URL must contain a path\r\n");
        return false;
    }

    if (port_start != NULL)
    {
        // Port is specified
        int hostname_length = port_start - url;
        *hostname = (char *)malloc(hostname_length + 1);
        strncpy(*hostname, url, hostname_length);
        (*hostname)[hostname_length] = '\0';

        *port = (uint16_t)atoi(port_start + 1);
    }
    else
    {
        // Port is not specified, use default port based on protocol
        int hostname_length = path_start - url;
        *hostname = (char *)malloc(hostname_length + 1);
        strncpy(*hostname, url, hostname_length);
        (*hostname)[hostname_length] = '\0';

        *port = (*protocol == PROT_HTTP) ? DEFAULT_HTTP_PORT : DEFAULT_HTTPS_PORT;
    }

    *uri = strdup(path_start);

    return true;
}

int_t main(int argc, char *argv[])
{
    TRACE_PRINTF(BUILD_FULL_NAME_LONG "\r\n\r\n");

    error_t error = 0;

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        get_directory_path(argv[0], cwd, sizeof(cwd));
    }

    /* platform specific init */
    settings_init(cwd);
    platform_init();

    cJSON_Hooks hooks = {.malloc_fn = osAllocMem, .free_fn = osFreeMem};
    cJSON_InitHooks(&hooks);

    /* load certificates and TLS RNG */
    if (tls_adapter_init() != NO_ERROR)
    {
        TRACE_ERROR("tls_adapter_init() failed\r\n");
        return -1;
    }

    if (argc > 1)
    {
        const char *type = argv[1];

        if (!strcasecmp(type, "GENERIC"))
        {
            TRACE_WARNING("**********************************\r\n");
            TRACE_WARNING("***       Generic URL test     ***\r\n");
            TRACE_WARNING("**********************************\r\n");

            char *request = NULL;
            uint8_t *hash = NULL;

            if (argc < 3)
            {
                TRACE_ERROR("Usage: %s GENERIC <url> [hash]\r\n", argv[0]);
                return -1;
            }
            if (argc > 2)
            {
                request = argv[2];
                TRACE_WARNING("Request URL: %s\r\n", request);
            }
            if (argc > 3)
            {
                hash = (uint8_t *)argv[3];
                TRACE_WARNING("Hash: %s\r\n", hash);
            }

            char *hostname;
            uint16_t port;
            char *uri;
            Protocol protocol;

            if (!parse_url(request, &hostname, &port, &uri, &protocol))
            {
                return ERROR_FAILURE;
            }

            TRACE_WARNING("Hostname: %s\n", hostname);
            TRACE_WARNING("Port: %u\n", port);
            TRACE_WARNING("URI: %s\n", uri);
            TRACE_WARNING("Protocol: %s\n", protocol == PROT_HTTP ? "HTTP" : "HTTPS");

            settings_set_bool("cloud.enabled", true);

            error = cloud_request(hostname, port, protocol == PROT_HTTPS, uri, "", "GET", NULL, 0, hash, NULL);

            free(hostname);
            free(uri);
        }
        else if (!strcasecmp(type, "CLOUD"))
        {
            TRACE_WARNING("**********************************\r\n");
            TRACE_WARNING("***       Cloud API test       ***\r\n");
            TRACE_WARNING("**********************************\r\n");

            char *request = NULL;
            uint8_t *hash = NULL;

            if (argc < 3)
            {
                TRACE_ERROR("Usage: %s CLOUD <request> [hash]\r\n", argv[0]);
                return -1;
            }
            if (argc > 2)
            {
                request = argv[2];
                TRACE_WARNING("Request URI: %s\r\n", request);
            }
            if (argc > 3)
            {
                hash = (uint8_t *)argv[3];
                TRACE_WARNING("Hash: %s\r\n", hash);
            }

            TRACE_WARNING("\r\n");

            error = cloud_request_get(NULL, 0, request, "", hash, NULL);
        }
        else if (!strcasecmp(type, "ESP32CERT"))
        {
            if (argc < 5)
            {
                TRACE_ERROR("Usage: %s ESP32CERT (extract/inject) <esp32-image-bin> <source/target-dir>\r\n", argv[0]);
                return -1;
            }
            const char *cmd = argv[2];
            if (!strcasecmp(cmd, "inject"))
            {
                esp32_fat_inject((const char *)argv[3], "CERT", (const char *)argv[4]);
            }
            else if (!strcasecmp(cmd, "extract"))
            {
                esp32_fat_extract((const char *)argv[3], "CERT", (const char *)argv[4]);
            }
        }
        else if (!strcasecmp(type, "ESP32FIXUP"))
        {
            if (argc < 3)
            {
                TRACE_ERROR("Usage: %s ESP32FIXUP <esp32-image-bin>\r\n", argv[0]);
                return -1;
            }
            esp32_fixup((const char *)argv[2], true);
        }
    }
    else
    {
        server_init();
    }

    tls_adapter_deinit();
    platform_deinit();
    settings_deinit_all();

    return error;
}
